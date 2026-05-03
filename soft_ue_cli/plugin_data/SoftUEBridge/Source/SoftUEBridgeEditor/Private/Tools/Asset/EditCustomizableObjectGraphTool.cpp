// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/EditCustomizableObjectGraphTool.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "ScopedTransaction.h"
#include "SoftUEBridgeEditorModule.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"

namespace
{
	static bool ContainsToken(const FString& Source, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (Source.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static FString NormalizeMemberName(FString Name)
	{
		Name.ToLowerInline();
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		return Name;
	}

	static bool MatchesNormalizedName(const FString& Name, const TCHAR* Target)
	{
		const FString Normalized = NormalizeMemberName(Name);
		const FString TargetString(Target);
		return Normalized == TargetString || Normalized == (FString(TEXT("b")) + TargetString);
	}

	static bool LooksLikeCustomizableObject(const UObject* Object)
	{
		return Object && Object->GetClass() &&
			ContainsToken(Object->GetClass()->GetName(), {TEXT("CustomizableObject"), TEXT("Mutable")});
	}

	static void CollectGraphs(UObject* AssetObject, TArray<UEdGraph*>& OutGraphs)
	{
		OutGraphs.Reset();
		if (!AssetObject)
		{
			return;
		}

		if (UEdGraph* DirectGraph = Cast<UEdGraph>(AssetObject))
		{
			OutGraphs.AddUnique(DirectGraph);
		}

		TArray<UObject*> InnerObjects;
		GetObjectsWithOuter(AssetObject, InnerObjects, true);
		for (UObject* InnerObject : InnerObjects)
		{
			if (UEdGraph* Graph = Cast<UEdGraph>(InnerObject))
			{
				OutGraphs.AddUnique(Graph);
			}
		}
	}

	static UEdGraph* ResolveGraph(UObject* AssetObject, const FString& GraphName)
	{
		TArray<UEdGraph*> Graphs;
		CollectGraphs(AssetObject, Graphs);
		if (Graphs.Num() == 0)
		{
			return nullptr;
		}

		if (!GraphName.IsEmpty())
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}
				if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase) ||
					Graph->GetPathName().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
			}
			return nullptr;
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(TEXT("Source"), ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetClass() &&
				ContainsToken(Graph->GetClass()->GetName(), {TEXT("CustomizableObject"), TEXT("Mutable")}))
			{
				return Graph;
			}
		}

		return Graphs[0];
	}

	static UEdGraphNode* FindNode(UObject* AssetObject, const FString& NodeRef, UEdGraph** OutGraph = nullptr)
	{
		if (OutGraph)
		{
			*OutGraph = nullptr;
		}
		if (!AssetObject || NodeRef.IsEmpty())
		{
			return nullptr;
		}

		FGuid ParsedGuid;
		const bool bHasGuid = FGuid::Parse(NodeRef, ParsedGuid);

		TArray<UEdGraph*> Graphs;
		CollectGraphs(AssetObject, Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				if ((bHasGuid && Node->NodeGuid == ParsedGuid) ||
					Node->GetPathName().Equals(NodeRef, ESearchCase::IgnoreCase) ||
					Node->GetName().Equals(NodeRef, ESearchCase::IgnoreCase) ||
					Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(NodeRef, ESearchCase::IgnoreCase))
				{
					if (OutGraph)
					{
						*OutGraph = Graph;
					}
					return Node;
				}
			}
		}

		return nullptr;
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static bool TryGetAssetPathFromJsonValue(const TSharedPtr<FJsonValue>& Value, FString& OutPath)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->TryGetString(OutPath))
		{
			return !OutPath.IsEmpty();
		}

		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if (Value->TryGetObject(ObjectValue) && ObjectValue && ObjectValue->IsValid())
		{
			for (const TCHAR* FieldName : {TEXT("asset_path"), TEXT("object_path"), TEXT("path"), TEXT("value")})
			{
				if ((*ObjectValue)->TryGetStringField(FieldName, OutPath) && !OutPath.IsEmpty())
				{
					return true;
				}
			}
		}
		return false;
	}

	static bool TryApplyObjectPinDefault(UEdGraphPin* Pin, const TSharedPtr<FJsonValue>& Value)
	{
		FString ObjectPath;
		if (!Pin || !TryGetAssetPathFromJsonValue(Value, ObjectPath))
		{
			return false;
		}

		UObject* LoadedObject = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!LoadedObject)
		{
			return false;
		}

		Pin->DefaultObject = LoadedObject;
		Pin->DefaultValue.Reset();
		Pin->DefaultTextValue = FText::GetEmpty();
		return true;
	}

	static bool IsCustomizableObjectNodeTable(const UEdGraphNode* Node)
	{
		return Node && Node->GetClass() &&
			Node->GetClass()->GetName().Contains(TEXT("CustomizableObjectNodeTable"), ESearchCase::IgnoreCase);
	}

	static bool RefreshCustomizableObjectNodePins(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		const int32 PinCountBefore = Node->Pins.Num();
		if (IsCustomizableObjectNodeTable(Node))
		{
			Node->PostEditChange();
		}
		Node->ReconstructNode();
		if (Node->Pins.Num() == 0)
		{
			Node->AllocateDefaultPins();
		}
		if (UEdGraph* Graph = Node->GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
		return Node->Pins.Num() != PinCountBefore;
	}

	static bool RefreshCustomizableObjectTableNodePins(UEdGraphNode* Node)
	{
		if (!IsCustomizableObjectNodeTable(Node))
		{
			return false;
		}
		return RefreshCustomizableObjectNodePins(Node);
	}

	static TArray<TSharedPtr<FJsonValue>> BuildPinList(const UEdGraphNode* Node)
	{
		TArray<TSharedPtr<FJsonValue>> PinValues;
		if (!Node)
		{
			return PinValues;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
			PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
			PinJson->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			PinJson->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
			PinJson->SetBoolField(TEXT("orphaned"), Pin->bOrphanedPin);
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			}
			if (Pin->DefaultObject)
			{
				PinJson->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
			}
			PinValues.Add(MakeShared<FJsonValueObject>(PinJson));
		}
		return PinValues;
	}

	static TArray<FString> ApplyReflectedProperties(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Properties)
	{
		TArray<FString> Errors;
		if (!Node || !Properties.IsValid())
		{
			return Errors;
		}

		TArray<FString> MissingPropertyNames;
		for (const auto& Pair : Properties->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString FindError;
			if (!FBridgeAssetModifier::FindPropertyByPath(Node, Pair.Key, Property, Container, FindError))
			{
				MissingPropertyNames.Add(Pair.Key);
				Errors.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
				continue;
			}

			Node->PreEditChange(Property);

			FString SetError;
			if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Pair.Value, SetError))
			{
				Node->PostEditChange();
				Errors.Add(FString::Printf(TEXT("Failed to set property %s: %s"), *Pair.Key, *SetError));
				continue;
			}

			FPropertyChangedEvent ChangeEvent(Property);
			Node->PostEditChangeProperty(ChangeEvent);
		}

		TArray<FString> ResolvedByPin;
		for (const FString& PropertyName : MissingPropertyNames)
		{
			UEdGraphPin* Pin = FindPin(Node, PropertyName);
			if (!Pin)
			{
				continue;
			}

			const TSharedPtr<FJsonValue>* ValuePtr = Properties->Values.Find(PropertyName);
			if (!ValuePtr || !ValuePtr->IsValid())
			{
				continue;
			}

			if (TryApplyObjectPinDefault(Pin, *ValuePtr))
			{
				ResolvedByPin.Add(PropertyName);
			}
			else if ((*ValuePtr)->Type == EJson::Number)
			{
				Pin->DefaultValue = FString::Printf(TEXT("%g"), (*ValuePtr)->AsNumber());
				ResolvedByPin.Add(PropertyName);
			}
			else if ((*ValuePtr)->Type == EJson::Boolean)
			{
				Pin->DefaultValue = (*ValuePtr)->AsBool() ? TEXT("true") : TEXT("false");
				ResolvedByPin.Add(PropertyName);
			}
			else if ((*ValuePtr)->Type == EJson::String)
			{
				Pin->DefaultValue = (*ValuePtr)->AsString();
				ResolvedByPin.Add(PropertyName);
			}
			else
			{
				continue;
			}
		}

		for (const FString& PropertyName : ResolvedByPin)
		{
			Errors.Remove(FString::Printf(TEXT("Property not found: %s"), *PropertyName));
		}

		RefreshCustomizableObjectTableNodePins(Node);
		return Errors;
	}

	static UClass* ResolveNodeClass(const FString& NodeClassName, FString& OutError)
	{
		UClass* NodeClass = FBridgePropertySerializer::ResolveClass(NodeClassName, OutError);
		if (!NodeClass && !NodeClassName.StartsWith(TEXT("CustomizableObjectNode")))
		{
			NodeClass = FBridgePropertySerializer::ResolveClass(TEXT("CustomizableObjectNode") + NodeClassName, OutError);
		}
		if (!NodeClass)
		{
			return nullptr;
		}
		if (!NodeClass->IsChildOf<UEdGraphNode>())
		{
			OutError = FString::Printf(TEXT("Class '%s' is not a UEdGraphNode subclass"), *NodeClassName);
			return nullptr;
		}
		return NodeClass;
	}

	static TSharedPtr<FJsonObject> BuildNodeResult(
		const FString& AssetPath,
		UEdGraph* Graph,
		UEdGraphNode* Node,
		const TArray<FString>& PropertyWarnings)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset"), AssetPath);
		Result->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Result->SetStringField(TEXT("node_name"), Node->GetName());
		Result->SetStringField(TEXT("node_path"), Node->GetPathName());
		Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		if (Graph)
		{
			Result->SetStringField(TEXT("graph"), Graph->GetName());
			Result->SetStringField(TEXT("graph_path"), Graph->GetPathName());
		}
		TSharedPtr<FJsonObject> PositionJson = MakeShared<FJsonObject>();
		PositionJson->SetNumberField(TEXT("x"), Node->NodePosX);
		PositionJson->SetNumberField(TEXT("y"), Node->NodePosY);
		Result->SetObjectField(TEXT("position"), PositionJson);
		Result->SetBoolField(TEXT("needs_compile"), true);
		Result->SetBoolField(TEXT("needs_save"), true);
		if (PropertyWarnings.Num() > 0)
		{
			Result->SetStringField(TEXT("property_warnings"), FString::Join(PropertyWarnings, TEXT("; ")));
		}
		Result->SetStringField(TEXT("node_creation_path"), TEXT("UEdGraph::CreateUserInvokedNode"));
		return Result;
	}

	static FProperty* FindReturnProperty(UFunction* Function)
	{
		if (!Function)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return Property;
			}
		}
		return nullptr;
	}

	static bool SetCompileParamsBoolField(
		UScriptStruct* CompileParamsStruct,
		void* CompileParamsMemory,
		std::initializer_list<const TCHAR*> NormalizedNames,
		bool bValue,
		TArray<FString>& OutConfiguredFields)
	{
		if (!CompileParamsStruct || !CompileParamsMemory)
		{
			return false;
		}

		for (TFieldIterator<FBoolProperty> It(CompileParamsStruct); It; ++It)
		{
			FBoolProperty* BoolProperty = *It;
			if (!BoolProperty)
			{
				continue;
			}

			for (const TCHAR* Name : NormalizedNames)
			{
				if (MatchesNormalizedName(BoolProperty->GetName(), Name))
				{
					BoolProperty->SetPropertyValue(
						BoolProperty->ContainerPtrToValuePtr<void>(CompileParamsMemory),
						bValue);
					OutConfiguredFields.AddUnique(BoolProperty->GetName());
					return true;
				}
			}
		}
		return false;
	}

	static bool ConfigureCompileParams(FStructProperty* StructProperty, void* StructMemory, TArray<FString>& OutConfiguredFields)
	{
		if (!StructProperty || !StructProperty->Struct || !StructMemory ||
			!StructProperty->Struct->GetName().Contains(TEXT("CompileParams"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		bool bConfiguredAny = false;
		bConfiguredAny |= SetCompileParamsBoolField(
			StructProperty->Struct,
			StructMemory,
			{TEXT("async")},
			false,
			OutConfiguredFields);
		bConfiguredAny |= SetCompileParamsBoolField(
			StructProperty->Struct,
			StructMemory,
			{TEXT("gatherreferences")},
			true,
			OutConfiguredFields);
		bConfiguredAny |= SetCompileParamsBoolField(
			StructProperty->Struct,
			StructMemory,
			{TEXT("skipifcompiled")},
			false,
			OutConfiguredFields);
		bConfiguredAny |= SetCompileParamsBoolField(
			StructProperty->Struct,
			StructMemory,
			{TEXT("skipifnotoutofdate")},
			false,
			OutConfiguredFields);
		return bConfiguredAny;
	}

	static bool TryCallNoParamBool(UObject* Target, const FName FunctionName, bool& OutValue)
	{
		if (!Target)
		{
			return false;
		}
		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		FProperty* ReturnProperty = FindReturnProperty(Function);
		FBoolProperty* BoolReturnProperty = CastField<FBoolProperty>(ReturnProperty);
		if (!BoolReturnProperty)
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* ParamBuffer = Params.GetStructMemory();
		Target->ProcessEvent(Function, ParamBuffer);
		OutValue = BoolReturnProperty->GetPropertyValue(
			BoolReturnProperty->ContainerPtrToValuePtr<void>(ParamBuffer));
		return true;
	}

	static bool TryCallNoParamInt(UObject* Target, const FName FunctionName, int32& OutValue)
	{
		if (!Target)
		{
			return false;
		}
		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		FProperty* ReturnProperty = FindReturnProperty(Function);
		FNumericProperty* NumericReturnProperty = CastField<FNumericProperty>(ReturnProperty);
		if (!NumericReturnProperty || !NumericReturnProperty->IsInteger())
		{
			return false;
		}

		FStructOnScope Params(Function);
		uint8* ParamBuffer = Params.GetStructMemory();
		Target->ProcessEvent(Function, ParamBuffer);
		OutValue = static_cast<int32>(NumericReturnProperty->GetSignedIntPropertyValue(
			NumericReturnProperty->ContainerPtrToValuePtr<void>(ParamBuffer)));
		return true;
	}

	static bool TryCompileWithAssetMethod(
		UObject* AssetObject,
		FString& OutState,
		bool& bOutCompileSucceeded,
		int32& OutParameterCount,
		TArray<FString>& OutConfiguredFields,
		FString& OutError)
	{
		bOutCompileSucceeded = false;
		OutParameterCount = INDEX_NONE;
		if (!AssetObject)
		{
			OutError = TEXT("Asset is unavailable");
			return false;
		}

		UFunction* CompileFunction = AssetObject->FindFunction(FName(TEXT("Compile")));
		if (!CompileFunction)
		{
			OutError = TEXT("Compile function was not found on the CustomizableObject asset");
			return false;
		}

		FStructOnScope Params(CompileFunction);
		uint8* ParamBuffer = Params.GetStructMemory();
		FProperty* ReturnProperty = nullptr;
		bool bConfiguredCompileParams = false;

		for (TFieldIterator<FProperty> It(CompileFunction); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnProperty = Property;
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ParamBuffer);
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				bConfiguredCompileParams |= ConfigureCompileParams(StructProperty, ValuePtr, OutConfiguredFields);
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				if (MatchesNormalizedName(BoolProperty->GetName(), TEXT("async")))
				{
					BoolProperty->SetPropertyValue(ValuePtr, false);
					OutConfiguredFields.AddUnique(BoolProperty->GetName());
				}
				else if (MatchesNormalizedName(BoolProperty->GetName(), TEXT("gatherreferences")))
				{
					BoolProperty->SetPropertyValue(ValuePtr, true);
					OutConfiguredFields.AddUnique(BoolProperty->GetName());
				}
				else if (MatchesNormalizedName(BoolProperty->GetName(), TEXT("skipifcompiled")) ||
					MatchesNormalizedName(BoolProperty->GetName(), TEXT("skipifnotoutofdate")))
				{
					BoolProperty->SetPropertyValue(ValuePtr, false);
					OutConfiguredFields.AddUnique(BoolProperty->GetName());
				}
			}
		}

		AssetObject->ProcessEvent(CompileFunction, ParamBuffer);

		if (ReturnProperty)
		{
			void* ReturnValuePtr = ReturnProperty->ContainerPtrToValuePtr<void>(ParamBuffer);
			ReturnProperty->ExportText_Direct(OutState, ReturnValuePtr, ReturnValuePtr, AssetObject, PPF_None);
		}

		bool bIsCompiled = true;
		if (TryCallNoParamBool(AssetObject, FName(TEXT("IsCompiled")), bIsCompiled))
		{
			bOutCompileSucceeded = bIsCompiled;
		}
		else
		{
			bOutCompileSucceeded = true;
		}
		TryCallNoParamInt(AssetObject, FName(TEXT("GetParameterCount")), OutParameterCount);

		if (OutState.IsEmpty())
		{
			OutState = bOutCompileSucceeded ? TEXT("Compiled") : TEXT("NotCompiled");
		}
		if (!bConfiguredCompileParams && OutConfiguredFields.Num() == 0)
		{
			OutError = TEXT("Compile was called, but no CompileParams-compatible fields were reflected.");
		}
		return true;
	}

	static bool TryCompileWithFunctionLibrary(UObject* AssetObject, FString& OutState, bool& bOutCompileSucceeded, FString& OutError)
	{
		bOutCompileSucceeded = false;
		UClass* LibraryClass = FindFirstObject<UClass>(
			TEXT("CustomizableObjectEditorFunctionLibrary"),
			EFindFirstObjectOptions::ExactClass);
		if (!LibraryClass)
		{
			LibraryClass = LoadClass<UObject>(
				nullptr,
				TEXT("/Script/CustomizableObjectEditor.CustomizableObjectEditorFunctionLibrary"));
		}
		if (!LibraryClass)
		{
			OutError = TEXT("CustomizableObjectEditorFunctionLibrary is not loaded");
			return false;
		}

		UFunction* CompileFunction = LibraryClass->FindFunctionByName(TEXT("CompileCustomizableObjectSynchronously"));
		if (!CompileFunction)
		{
			OutError = TEXT("CompileCustomizableObjectSynchronously was not found");
			return false;
		}

		FStructOnScope Params(CompileFunction);
		uint8* ParamBuffer = Params.GetStructMemory();
		FProperty* ReturnProperty = nullptr;
		bool bAssignedObjectParameter = false;

		for (TFieldIterator<FProperty> It(CompileFunction); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnProperty = Property;
				continue;
			}

			if (!bAssignedObjectParameter)
			{
				if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					if (AssetObject->IsA(ObjectProperty->PropertyClass))
					{
						ObjectProperty->SetObjectPropertyValue(
							Property->ContainerPtrToValuePtr<void>(ParamBuffer),
							AssetObject);
						bAssignedObjectParameter = true;
					}
				}
			}
		}

		if (!bAssignedObjectParameter)
		{
			OutError = TEXT("Compile function did not expose a compatible CustomizableObject parameter");
			return false;
		}

		UObject* LibraryObject = LibraryClass->GetDefaultObject();
		if (!LibraryObject)
		{
			OutError = TEXT("CustomizableObject editor function library default object is unavailable");
			return false;
		}

		LibraryObject->ProcessEvent(CompileFunction, ParamBuffer);

		if (ReturnProperty)
		{
			void* ReturnValuePtr = ReturnProperty->ContainerPtrToValuePtr<void>(ParamBuffer);
			ReturnProperty->ExportText_Direct(OutState, ReturnValuePtr, ReturnValuePtr, LibraryObject, PPF_None);
			bOutCompileSucceeded = !OutState.Contains(TEXT("Failed"), ESearchCase::IgnoreCase);
		}
		else
		{
			bOutCompileSucceeded = true;
		}
		return true;
	}

	static TMap<FString, FBridgeSchemaProperty> CommonCustomizableObjectAssetSchema()
	{
		TMap<FString, FBridgeSchemaProperty> Schema;
		FBridgeSchemaProperty AssetPath;
		AssetPath.Type = TEXT("string");
		AssetPath.Description = TEXT("Asset path to the CustomizableObject asset");
		AssetPath.bRequired = true;
		Schema.Add(TEXT("asset_path"), AssetPath);
		return Schema;
	}
}

FString UAddCustomizableObjectNodeTool::GetToolDescription() const
{
	return TEXT("Add a reflected UEdGraphNode subclass to a Mutable/CustomizableObject graph. "
		"Accepts node class names such as CustomizableObjectNodeFloatParameter or CustomizableObjectNodeSkeletalMesh.");
}

TMap<FString, FBridgeSchemaProperty> UAddCustomizableObjectNodeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty NodeClass;
	NodeClass.Type = TEXT("string");
	NodeClass.Description = TEXT("UEdGraphNode class name or class path");
	NodeClass.bRequired = true;
	Schema.Add(TEXT("node_class"), NodeClass);

	FBridgeSchemaProperty GraphName;
	GraphName.Type = TEXT("string");
	GraphName.Description = TEXT("Optional graph name or object path. Defaults to the source graph.");
	Schema.Add(TEXT("graph_name"), GraphName);

	FBridgeSchemaProperty Position;
	Position.Type = TEXT("array");
	Position.Description = TEXT("Optional [X, Y] node position");
	Schema.Add(TEXT("position"), Position);

	FBridgeSchemaProperty Properties;
	Properties.Type = TEXT("object");
	Properties.Description = TEXT("Optional reflected properties to set after node creation");
	Schema.Add(TEXT("properties"), Properties);

	return Schema;
}

TArray<FString> UAddCustomizableObjectNodeTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("node_class")};
}

FBridgeToolResult UAddCustomizableObjectNodeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString NodeClassName = GetStringArgOrDefault(Arguments, TEXT("node_class"));
	const FString GraphName = GetStringArgOrDefault(Arguments, TEXT("graph_name"));
	if (AssetPath.IsEmpty() || NodeClassName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and node_class are required"));
	}

	FVector2D Position(0.0, 0.0);
	const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
	{
		Position.X = (*PositionArray)[0]->AsNumber();
		Position.Y = (*PositionArray)[1]->AsNumber();
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr))
	{
		Properties = *PropertiesPtr;
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	UEdGraph* TargetGraph = ResolveGraph(AssetObject, GraphName);
	if (!TargetGraph)
	{
		return FBridgeToolResult::Error(GraphName.IsEmpty()
			? TEXT("No graph found on CustomizableObject asset")
			: FString::Printf(TEXT("CustomizableObject graph not found: %s"), *GraphName));
	}

	FString ClassError;
	UClass* NodeClass = ResolveNodeClass(NodeClassName, ClassError);
	if (!NodeClass)
	{
		return FBridgeToolResult::Error(ClassError);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "AddCustomizableObjectNode", "Add {0} node to {1}"),
			FText::FromString(NodeClassName),
			FText::FromString(AssetPath)));

	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(TargetGraph);

	FGraphNodeCreator<UEdGraphNode> NodeCreator(*TargetGraph);
	UEdGraphNode* NewNode = NodeCreator.CreateUserInvokedNode(true, NodeClass);
	if (!NewNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create node of class %s"), *NodeClass->GetName()));
	}

	NewNode->NodePosX = FMath::RoundToInt(Position.X);
	NewNode->NodePosY = FMath::RoundToInt(Position.Y);
	NodeCreator.Finalize();

	TArray<FString> PropertyWarnings = ApplyReflectedProperties(NewNode, Properties);
	NewNode->ReconstructNode();
	TargetGraph->NotifyGraphChanged();
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-customizable-object-node: Added %s to %s"),
		*NewNode->GetClass()->GetName(), *AssetPath);

	return FBridgeToolResult::Json(BuildNodeResult(AssetPath, TargetGraph, NewNode, PropertyWarnings));
}

FString USetCustomizableObjectNodePropertyTool::GetToolDescription() const
{
	return TEXT("Set reflected properties on a Mutable/CustomizableObject graph node found by GUID, name, path, or title.");
}

TMap<FString, FBridgeSchemaProperty> USetCustomizableObjectNodePropertyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty Node;
	Node.Type = TEXT("string");
	Node.Description = TEXT("Node GUID, object path, object name, or title");
	Node.bRequired = true;
	Schema.Add(TEXT("node"), Node);

	FBridgeSchemaProperty Properties;
	Properties.Type = TEXT("object");
	Properties.Description = TEXT("Reflected properties to set");
	Properties.bRequired = true;
	Schema.Add(TEXT("properties"), Properties);

	return Schema;
}

TArray<FString> USetCustomizableObjectNodePropertyTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("node"), TEXT("properties")};
}

FBridgeToolResult USetCustomizableObjectNodePropertyTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString NodeRef = GetStringArgOrDefault(Arguments, TEXT("node"));
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (AssetPath.IsEmpty() || NodeRef.IsEmpty() || !Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr))
	{
		return FBridgeToolResult::Error(TEXT("asset_path, node, and properties are required"));
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNode(AssetObject, NodeRef, &Graph);
	if (!Node)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("CustomizableObject node not found: %s"), *NodeRef));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "SetCustomizableObjectNodeProperty", "Set properties on {0}"),
			FText::FromString(NodeRef)));

	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(Node);
	TArray<FString> PropertyWarnings = ApplyReflectedProperties(Node, *PropertiesPtr);
	Node->ReconstructNode();
	if (Graph)
	{
		Graph->NotifyGraphChanged();
	}
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	return FBridgeToolResult::Json(BuildNodeResult(AssetPath, Graph, Node, PropertyWarnings));
}

FString UConnectCustomizableObjectPinsTool::GetToolDescription() const
{
	return TEXT("Connect two pins in a Mutable/CustomizableObject graph. Nodes may be referenced by GUID, name, path, or title.");
}

TMap<FString, FBridgeSchemaProperty> UConnectCustomizableObjectPinsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	for (const TCHAR* Name : {TEXT("source_node"), TEXT("source_pin"), TEXT("target_node"), TEXT("target_pin")})
	{
		FBridgeSchemaProperty Prop;
		Prop.Type = TEXT("string");
		Prop.Description = TEXT("Node reference or pin name");
		Prop.bRequired = true;
		Schema.Add(Name, Prop);
	}

	FBridgeSchemaProperty AutoRegenerate;
	AutoRegenerate.Type = TEXT("boolean");
	AutoRegenerate.Description = TEXT("Regenerate each owning node once if a requested pin is missing before failing.");
	Schema.Add(TEXT("auto_regenerate"), AutoRegenerate);

	return Schema;
}

TArray<FString> UConnectCustomizableObjectPinsTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("source_node"), TEXT("source_pin"), TEXT("target_node"), TEXT("target_pin")};
}

FBridgeToolResult UConnectCustomizableObjectPinsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString SourceNodeRef = GetStringArgOrDefault(Arguments, TEXT("source_node"));
	const FString SourcePinName = GetStringArgOrDefault(Arguments, TEXT("source_pin"));
	const FString TargetNodeRef = GetStringArgOrDefault(Arguments, TEXT("target_node"));
	const FString TargetPinName = GetStringArgOrDefault(Arguments, TEXT("target_pin"));
	const bool bAutoRegenerate = GetBoolArgOrDefault(Arguments, TEXT("auto_regenerate"), true);
	if (AssetPath.IsEmpty() || SourceNodeRef.IsEmpty() || SourcePinName.IsEmpty() ||
		TargetNodeRef.IsEmpty() || TargetPinName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, source_node, source_pin, target_node, and target_pin are required"));
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	UEdGraph* SourceGraph = nullptr;
	UEdGraph* TargetGraph = nullptr;
	UEdGraphNode* SourceNode = FindNode(AssetObject, SourceNodeRef, &SourceGraph);
	UEdGraphNode* TargetNode = FindNode(AssetObject, TargetNodeRef, &TargetGraph);
	if (!SourceNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeRef));
	}
	if (!TargetNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeRef));
	}
	if (SourceGraph != TargetGraph)
	{
		return FBridgeToolResult::Error(TEXT("CustomizableObject pin connections must be within the same graph"));
	}

	UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName);
	UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName);
	bool bSourceRegenerated = false;
	bool bTargetRegenerated = false;
	TArray<UEdGraphNode*> RegeneratedNodes;
	TSharedPtr<FScopedTransaction> Transaction;
	auto EnsureTransaction = [&Transaction]()
	{
		if (!Transaction.IsValid())
		{
			Transaction = FBridgeAssetModifier::BeginTransaction(
				NSLOCTEXT("SoftUEBridge", "ConnectCustomizableObjectPins", "Connect CustomizableObject pins"));
		}
	};
	auto RegeneratePinsOnce = [&](UEdGraphNode* Node) -> bool
	{
		if (!Node)
		{
			return false;
		}
		EnsureTransaction();
		FBridgeAssetModifier::MarkModified(AssetObject);
		FBridgeAssetModifier::MarkModified(Node);
		if (UEdGraph* OwningGraph = Node->GetGraph())
		{
			FBridgeAssetModifier::MarkModified(OwningGraph);
		}
		if (!RegeneratedNodes.Contains(Node))
		{
			RefreshCustomizableObjectNodePins(Node);
			RegeneratedNodes.Add(Node);
		}
		return true;
	};

	if (bAutoRegenerate && !SourcePin)
	{
		bSourceRegenerated = RegeneratePinsOnce(SourceNode);
		SourcePin = FindPin(SourceNode, SourcePinName);
	}
	if (bAutoRegenerate && !TargetPin)
	{
		bTargetRegenerated = RegeneratePinsOnce(TargetNode);
		TargetPin = FindPin(TargetNode, TargetPinName);
	}
	if (bSourceRegenerated || (bTargetRegenerated && SourceNode == TargetNode))
	{
		SourcePin = FindPin(SourceNode, SourcePinName);
	}
	if (bTargetRegenerated || (bSourceRegenerated && SourceNode == TargetNode))
	{
		TargetPin = FindPin(TargetNode, TargetPinName);
	}
	if (!SourcePin)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("%s: %s"),
			bAutoRegenerate ? TEXT("Source pin not found after regenerate") : TEXT("Source pin not found"),
			*SourcePinName));
	}
	if (!TargetPin)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("%s: %s"),
			bAutoRegenerate ? TEXT("Target pin not found after regenerate") : TEXT("Target pin not found"),
			*TargetPinName));
	}

	const UEdGraphSchema* Schema = SourceGraph ? SourceGraph->GetSchema() : nullptr;
	if (!Schema)
	{
		return FBridgeToolResult::Error(TEXT("Source graph has no schema"));
	}

	const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Cannot connect pins: %s"), *Response.Message.ToString()));
	}

	EnsureTransaction();
	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(SourceGraph);
	FBridgeAssetModifier::MarkModified(SourceNode);
	FBridgeAssetModifier::MarkModified(TargetNode);
	if (!Schema->TryCreateConnection(SourcePin, TargetPin))
	{
		return FBridgeToolResult::Error(TEXT("Failed to connect pins"));
	}

	SourceGraph->NotifyGraphChanged();
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("source_node"), SourceNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("source_pin"), SourcePin->PinName.ToString());
	Result->SetStringField(TEXT("target_node"), TargetNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("target_pin"), TargetPin->PinName.ToString());
	Result->SetBoolField(TEXT("auto_regenerate"), bAutoRegenerate);
	Result->SetBoolField(TEXT("source_regenerated"), bSourceRegenerated);
	Result->SetBoolField(TEXT("target_regenerated"), bTargetRegenerated);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	return FBridgeToolResult::Json(Result);
}

FString URegenerateCustomizableObjectNodePinsTool::GetToolDescription() const
{
	return TEXT("Regenerate pins for a single Mutable/CustomizableObject graph node and return its refreshed pin list.");
}

TMap<FString, FBridgeSchemaProperty> URegenerateCustomizableObjectNodePinsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty Node;
	Node.Type = TEXT("string");
	Node.Description = TEXT("Node GUID, object path, object name, or title");
	Node.bRequired = true;
	Schema.Add(TEXT("node"), Node);

	return Schema;
}

TArray<FString> URegenerateCustomizableObjectNodePinsTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("node")};
}

FBridgeToolResult URegenerateCustomizableObjectNodePinsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString NodeRef = GetStringArgOrDefault(Arguments, TEXT("node"));
	if (AssetPath.IsEmpty() || NodeRef.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and node are required"));
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNode(AssetObject, NodeRef, &Graph);
	if (!Node)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("CustomizableObject node not found: %s"), *NodeRef));
	}

	const int32 PinCountBefore = Node->Pins.Num();
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "RegenerateCustomizableObjectNodePins", "Regenerate pins on {0}"),
			FText::FromString(NodeRef)));

	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(Node);
	if (Graph)
	{
		FBridgeAssetModifier::MarkModified(Graph);
	}
	const bool bPinCountChanged = RefreshCustomizableObjectNodePins(Node);
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	if (Graph)
	{
		Result->SetStringField(TEXT("graph"), Graph->GetName());
		Result->SetStringField(TEXT("graph_path"), Graph->GetPathName());
	}
	Result->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("node_name"), Node->GetName());
	Result->SetStringField(TEXT("node_path"), Node->GetPathName());
	Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	Result->SetBoolField(TEXT("regenerated"), true);
	Result->SetBoolField(TEXT("pin_count_changed"), bPinCountChanged);
	Result->SetNumberField(TEXT("pin_count_before"), PinCountBefore);
	Result->SetNumberField(TEXT("pin_count"), Node->Pins.Num());
	Result->SetArrayField(TEXT("pins"), BuildPinList(Node));
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	return FBridgeToolResult::Json(Result);
}

FString UCompileCustomizableObjectTool::GetToolDescription() const
{
	return TEXT("Compile a Mutable/CustomizableObject asset through reflected editor compile APIs when available.");
}

TMap<FString, FBridgeSchemaProperty> UCompileCustomizableObjectTool::GetInputSchema() const
{
	return CommonCustomizableObjectAssetSchema();
}

TArray<FString> UCompileCustomizableObjectTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UCompileCustomizableObjectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	FString CompileState;
	FString CompileError;
	bool bCompileSucceeded = false;
	int32 ParameterCount = INDEX_NONE;
	TArray<FString> ConfiguredCompileFields;
	FString CompileMethod = TEXT("asset_compile");
	bool bCompileCalled = TryCompileWithAssetMethod(
		AssetObject,
		CompileState,
		bCompileSucceeded,
		ParameterCount,
		ConfiguredCompileFields,
		CompileError);
	if (!bCompileCalled)
	{
		CompileMethod = TEXT("editor_function_library");
		bCompileCalled = TryCompileWithFunctionLibrary(AssetObject, CompileState, bCompileSucceeded, CompileError);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bCompileCalled && bCompileSucceeded);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("loaded_class"), AssetObject->GetClass()->GetName());
	Result->SetBoolField(TEXT("compile_requested"), bCompileCalled);
	Result->SetStringField(TEXT("compile_method"), CompileMethod);
	if (ParameterCount != INDEX_NONE)
	{
		Result->SetNumberField(TEXT("parameter_count"), ParameterCount);
	}
	if (ConfiguredCompileFields.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Fields;
		for (const FString& Field : ConfiguredCompileFields)
		{
			Fields.Add(MakeShared<FJsonValueString>(Field));
		}
		Result->SetArrayField(TEXT("configured_compile_fields"), Fields);
	}
	if (!CompileState.IsEmpty())
	{
		Result->SetStringField(TEXT("compile_state"), CompileState);
	}
	if (!bCompileCalled)
	{
		Result->SetStringField(TEXT("status"), TEXT("compile_function_unavailable"));
		Result->SetStringField(TEXT("error"), CompileError);
		Result->SetBoolField(TEXT("needs_manual_compile"), true);
	}
	else if (!bCompileSucceeded)
	{
		Result->SetStringField(TEXT("status"), TEXT("compile_failed"));
		Result->SetStringField(TEXT("error"), TEXT("CustomizableObject compile returned a failed state"));
		Result->SetBoolField(TEXT("needs_manual_fix"), true);
	}
	else
	{
		Result->SetStringField(TEXT("status"), TEXT("compile_completed"));
	}
	return FBridgeToolResult::Json(Result);
}

FString URemoveCustomizableObjectNodeTool::GetToolDescription() const
{
	return TEXT("Remove a node from a Mutable/CustomizableObject graph by GUID, name, path, or title.");
}

TMap<FString, FBridgeSchemaProperty> URemoveCustomizableObjectNodeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty Node;
	Node.Type = TEXT("string");
	Node.Description = TEXT("Node GUID, object path, object name, or title");
	Node.bRequired = true;
	Schema.Add(TEXT("node"), Node);

	return Schema;
}

TArray<FString> URemoveCustomizableObjectNodeTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("node")};
}

FBridgeToolResult URemoveCustomizableObjectNodeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString NodeRef = GetStringArgOrDefault(Arguments, TEXT("node"));
	if (AssetPath.IsEmpty() || NodeRef.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and node are required"));
	}

	FString LoadError;
	UObject* AssetObject = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!AssetObject)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!LooksLikeCustomizableObject(AssetObject))
	{
		return FBridgeToolResult::Error(TEXT("Asset does not appear to be a Mutable/CustomizableObject asset."));
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNode(AssetObject, NodeRef, &Graph);
	if (!Node || !Graph)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("CustomizableObject node not found: %s"), *NodeRef));
	}

	const FString RemovedNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
	const FString RemovedNodeName = Node->GetName();
	const FString RemovedNodeClass = Node->GetClass() ? Node->GetClass()->GetName() : FString();

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "RemoveCustomizableObjectNode", "Remove CustomizableObject node {0}"),
			FText::FromString(NodeRef)));

	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(Graph);
	FBridgeAssetModifier::MarkModified(Node);
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);
	Graph->NotifyGraphChanged();
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("graph"), Graph->GetName());
	Result->SetStringField(TEXT("removed_node"), NodeRef);
	Result->SetStringField(TEXT("removed_node_guid"), RemovedNodeGuid);
	Result->SetStringField(TEXT("removed_node_name"), RemovedNodeName);
	Result->SetStringField(TEXT("removed_node_class"), RemovedNodeClass);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	return FBridgeToolResult::Json(Result);
}
