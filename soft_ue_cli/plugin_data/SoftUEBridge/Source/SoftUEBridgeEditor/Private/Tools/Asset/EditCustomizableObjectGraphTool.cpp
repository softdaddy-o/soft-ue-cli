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

	static TSharedPtr<FJsonObject> BuildCreatedNodeSummary(const FString& Role, UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetStringField(TEXT("role"), Role);
		if (!Node)
		{
			return Summary;
		}
		Summary->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Summary->SetStringField(TEXT("node_name"), Node->GetName());
		Summary->SetStringField(TEXT("node_path"), Node->GetPathName());
		if (Node->GetClass())
		{
			Summary->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		}
		return Summary;
	}

	static TSharedPtr<FJsonObject> BuildCreatedEdgeSummary(
		const FString& Role,
		UEdGraphNode* SourceNode,
		UEdGraphPin* SourcePin,
		UEdGraphNode* TargetNode,
		UEdGraphPin* TargetPin)
	{
		TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
		Edge->SetStringField(TEXT("role"), Role);
		if (SourceNode)
		{
			Edge->SetStringField(TEXT("source_node"), SourceNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}
		if (SourcePin)
		{
			Edge->SetStringField(TEXT("source_pin"), SourcePin->PinName.ToString());
		}
		if (TargetNode)
		{
			Edge->SetStringField(TEXT("target_node"), TargetNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}
		if (TargetPin)
		{
			Edge->SetStringField(TEXT("target_pin"), TargetPin->PinName.ToString());
		}
		return Edge;
	}

	static bool TryGetStringFieldAlias(
		const TSharedPtr<FJsonObject>& Object,
		std::initializer_list<const TCHAR*> Names,
		FString& OutValue)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		for (const TCHAR* Name : Names)
		{
			if (Object->TryGetStringField(Name, OutValue) && !OutValue.IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	static FVector2D ReadSpecNodePosition(const TSharedPtr<FJsonObject>& NodeObject, int32 NodeIndex)
	{
		if (!NodeObject.IsValid())
		{
			return FVector2D(NodeIndex * 300.0, 0.0);
		}

		double X = 0.0;
		double Y = 0.0;
		if (NodeObject->TryGetNumberField(TEXT("x"), X) && NodeObject->TryGetNumberField(TEXT("y"), Y))
		{
			return FVector2D(X, Y);
		}

		const TSharedPtr<FJsonObject>* PositionObject = nullptr;
		if (NodeObject->TryGetObjectField(TEXT("position"), PositionObject) && PositionObject && PositionObject->IsValid() &&
			(*PositionObject)->TryGetNumberField(TEXT("x"), X) && (*PositionObject)->TryGetNumberField(TEXT("y"), Y))
		{
			return FVector2D(X, Y);
		}

		const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
		if (NodeObject->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray && PositionArray->Num() >= 2)
		{
			double ArrayX = 0.0;
			double ArrayY = 0.0;
			if ((*PositionArray)[0]->TryGetNumber(ArrayX) && (*PositionArray)[1]->TryGetNumber(ArrayY))
			{
				return FVector2D(ArrayX, ArrayY);
			}
		}

		return FVector2D(NodeIndex * 300.0, 0.0);
	}

	static UEdGraphNode* CreateCustomizableObjectGraphNode(
		UObject* AssetObject,
		UEdGraph* TargetGraph,
		const FString& NodeClassName,
		const FVector2D& Position,
		const TSharedPtr<FJsonObject>& Properties,
		TArray<FString>& OutPropertyWarnings,
		FString& OutError)
	{
		if (!AssetObject || !TargetGraph)
		{
			OutError = TEXT("CustomizableObject graph is unavailable");
			return nullptr;
		}

		FString ClassError;
		UClass* NodeClass = ResolveNodeClass(NodeClassName, ClassError);
		if (!NodeClass)
		{
			OutError = ClassError;
			return nullptr;
		}

		FGraphNodeCreator<UEdGraphNode> NodeCreator(*TargetGraph);
		UEdGraphNode* NewNode = NodeCreator.CreateUserInvokedNode(true, NodeClass);
		if (!NewNode)
		{
			OutError = FString::Printf(TEXT("Failed to create node of class %s"), *NodeClass->GetName());
			return nullptr;
		}

		NewNode->NodePosX = FMath::RoundToInt(Position.X);
		NewNode->NodePosY = FMath::RoundToInt(Position.Y);
		NodeCreator.Finalize();

		OutPropertyWarnings = ApplyReflectedProperties(NewNode, Properties);
		RefreshCustomizableObjectNodePins(NewNode);
		NewNode->ReconstructNode();
		return NewNode;
	}

	static FString DescribePins(UEdGraphNode* Node)
	{
		TArray<FString> PinNames;
		if (Node)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					PinNames.Add(Pin->PinName.ToString());
				}
			}
		}
		PinNames.Sort();
		return FString::Join(PinNames, TEXT(", "));
	}

	static UEdGraphPin* FindPinFromCandidates(UEdGraphNode* Node, const TArray<FString>& CandidateNames)
	{
		for (const FString& CandidateName : CandidateNames)
		{
			if (UEdGraphPin* Pin = FindPin(Node, CandidateName))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static bool ConnectCustomizableObjectPinsOrError(
		UEdGraph* Graph,
		UEdGraphNode* SourceNode,
		const TArray<FString>& SourcePinCandidates,
		UEdGraphNode* TargetNode,
		const TArray<FString>& TargetPinCandidates,
		const FString& EdgeRole,
		TArray<TSharedPtr<FJsonValue>>& OutCreatedEdges,
		FString& OutError)
	{
		UEdGraphPin* SourcePin = FindPinFromCandidates(SourceNode, SourcePinCandidates);
		UEdGraphPin* TargetPin = FindPinFromCandidates(TargetNode, TargetPinCandidates);
		if (!SourcePin)
		{
			OutError = FString::Printf(
				TEXT("Source pin not found for %s. Tried [%s]. Available pins: %s"),
				*EdgeRole,
				*FString::Join(SourcePinCandidates, TEXT(", ")),
				*DescribePins(SourceNode));
			return false;
		}
		if (!TargetPin)
		{
			OutError = FString::Printf(
				TEXT("Target pin not found for %s. Tried [%s]. Available pins: %s"),
				*EdgeRole,
				*FString::Join(TargetPinCandidates, TEXT(", ")),
				*DescribePins(TargetNode));
			return false;
		}

		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		if (!Schema)
		{
			OutError = TEXT("CustomizableObject graph has no schema");
			return false;
		}

		const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = FString::Printf(TEXT("Cannot connect %s: %s"), *EdgeRole, *Response.Message.ToString());
			return false;
		}
		if (!Schema->TryCreateConnection(SourcePin, TargetPin))
		{
			OutError = FString::Printf(TEXT("Failed to connect %s"), *EdgeRole);
			return false;
		}

		OutCreatedEdges.Add(MakeShared<FJsonValueObject>(
			BuildCreatedEdgeSummary(EdgeRole, SourceNode, SourcePin, TargetNode, TargetPin)));
		return true;
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

	static bool ConfigureCompileParams(
		FStructProperty* StructProperty,
		void* StructMemory,
		bool bGatherReferences,
		TArray<FString>& OutConfiguredFields)
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
			bGatherReferences,
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
		bool bGatherReferences,
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
				bConfiguredCompileParams |= ConfigureCompileParams(
					StructProperty,
					ValuePtr,
					bGatherReferences,
					OutConfiguredFields);
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
					BoolProperty->SetPropertyValue(ValuePtr, bGatherReferences);
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
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty GatherReferences;
	GatherReferences.Type = TEXT("boolean");
	GatherReferences.Description = TEXT("Request Compile and Gather References mode when compile params expose it");
	GatherReferences.bRequired = false;
	Schema.Add(TEXT("gather_references"), GatherReferences);

	return Schema;
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

	const bool bGatherReferences = GetBoolArgOrDefault(Arguments, TEXT("gather_references"), true);
	FString CompileState;
	FString CompileError;
	bool bCompileSucceeded = false;
	int32 ParameterCount = INDEX_NONE;
	TArray<FString> ConfiguredCompileFields;
	FString CompileMethod = TEXT("asset_compile");
	bool bCompileCalled = TryCompileWithAssetMethod(
		AssetObject,
		bGatherReferences,
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
	Result->SetBoolField(TEXT("gather_references_requested"), bGatherReferences);
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

FString UCreateCustomizableObjectFromSpecTool::GetToolDescription() const
{
	return TEXT("Create multiple Mutable/CustomizableObject graph nodes and edges from one JSON specification.");
}

TMap<FString, FBridgeSchemaProperty> UCreateCustomizableObjectFromSpecTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	FBridgeSchemaProperty Spec;
	Spec.Type = TEXT("object");
	Spec.Description = TEXT("Graph spec with nodes and edges arrays");
	Spec.bRequired = true;
	Schema.Add(TEXT("spec"), Spec);

	return Schema;
}

TArray<FString> UCreateCustomizableObjectFromSpecTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("spec")};
}

FBridgeToolResult UCreateCustomizableObjectFromSpecTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	const TSharedPtr<FJsonObject>* SpecObject = nullptr;
	if (!Arguments->TryGetObjectField(TEXT("spec"), SpecObject) || !SpecObject || !SpecObject->IsValid())
	{
		return FBridgeToolResult::Error(TEXT("spec object is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (!(*SpecObject)->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray)
	{
		return FBridgeToolResult::Error(TEXT("spec.nodes array is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* EdgesArray = nullptr;
	(*SpecObject)->TryGetArrayField(TEXT("edges"), EdgesArray);

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

	FString GraphName;
	(*SpecObject)->TryGetStringField(TEXT("graph_name"), GraphName);
	UEdGraph* TargetGraph = ResolveGraph(AssetObject, GraphName);
	if (!TargetGraph)
	{
		return FBridgeToolResult::Error(GraphName.IsEmpty()
			? TEXT("No graph found on CustomizableObject asset")
			: FString::Printf(TEXT("CustomizableObject graph not found: %s"), *GraphName));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "CreateCustomizableObjectFromSpec", "Create CustomizableObject graph from spec in {0}"),
			FText::FromString(AssetPath)));
	FBridgeAssetModifier::MarkModified(AssetObject);

	TMap<FString, UEdGraphNode*> NodesBySpecId;
	TArray<TSharedPtr<FJsonValue>> CreatedNodes;
	TArray<TSharedPtr<FJsonValue>> CreatedEdges;
	TArray<FString> PropertyWarnings;

	for (int32 NodeIndex = 0; NodeIndex < NodesArray->Num(); ++NodeIndex)
	{
		const TSharedPtr<FJsonObject>* NodeObject = nullptr;
		if (!(*NodesArray)[NodeIndex]->TryGetObject(NodeObject) || !NodeObject || !NodeObject->IsValid())
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("spec.nodes[%d] must be an object"), NodeIndex));
		}

		FString NodeId;
		if (!TryGetStringFieldAlias(*NodeObject, {TEXT("id"), TEXT("name")}, NodeId))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("spec.nodes[%d] must include id"), NodeIndex));
		}

		FString NodeClassName;
		if (!TryGetStringFieldAlias(*NodeObject, {TEXT("class"), TEXT("node_class")}, NodeClassName))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("spec.nodes[%d] must include class"), NodeIndex));
		}

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		TSharedPtr<FJsonObject> Properties;
		if ((*NodeObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && PropertiesObject->IsValid())
		{
			Properties = *PropertiesObject;
		}

		TArray<FString> NodePropertyWarnings;
		FString CreateError;
		UEdGraphNode* NewNode = CreateCustomizableObjectGraphNode(
			AssetObject,
			TargetGraph,
			NodeClassName,
			ReadSpecNodePosition(*NodeObject, NodeIndex),
			Properties,
			NodePropertyWarnings,
			CreateError);
		if (!NewNode)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create spec node %s: %s"), *NodeId, *CreateError));
		}

		NodesBySpecId.Add(NodeId, NewNode);
		for (const FString& Warning : NodePropertyWarnings)
		{
			PropertyWarnings.Add(FString::Printf(TEXT("%s: %s"), *NodeId, *Warning));
		}

		TSharedPtr<FJsonObject> NodeSummary = BuildCreatedNodeSummary(NodeId, NewNode);
		NodeSummary->SetStringField(TEXT("spec_node_id"), NodeId);
		if (NodePropertyWarnings.Num() > 0)
		{
			NodeSummary->SetStringField(TEXT("property_warnings"), FString::Join(NodePropertyWarnings, TEXT("; ")));
		}
		CreatedNodes.Add(MakeShared<FJsonValueObject>(NodeSummary));
	}

	if (EdgesArray)
	{
		for (int32 EdgeIndex = 0; EdgeIndex < EdgesArray->Num(); ++EdgeIndex)
		{
			const TSharedPtr<FJsonObject>* EdgeObject = nullptr;
			if (!(*EdgesArray)[EdgeIndex]->TryGetObject(EdgeObject) || !EdgeObject || !EdgeObject->IsValid())
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("spec.edges[%d] must be an object"), EdgeIndex));
			}

			FString SourceId;
			FString TargetId;
			FString SourcePin;
			FString TargetPin;
			if (!TryGetStringFieldAlias(*EdgeObject, {TEXT("source"), TEXT("source_node"), TEXT("from")}, SourceId) ||
				!TryGetStringFieldAlias(*EdgeObject, {TEXT("target"), TEXT("target_node"), TEXT("to")}, TargetId) ||
				!TryGetStringFieldAlias(*EdgeObject, {TEXT("source_pin"), TEXT("from_pin"), TEXT("output")}, SourcePin) ||
				!TryGetStringFieldAlias(*EdgeObject, {TEXT("target_pin"), TEXT("to_pin"), TEXT("input")}, TargetPin))
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("spec.edges[%d] must include source, source_pin, target, and target_pin"),
					EdgeIndex));
			}

			UEdGraphNode* SourceNode = NodesBySpecId.FindRef(SourceId);
			UEdGraphNode* TargetNode = NodesBySpecId.FindRef(TargetId);
			if (!SourceNode)
			{
				SourceNode = FindNode(AssetObject, SourceId);
			}
			if (!TargetNode)
			{
				TargetNode = FindNode(AssetObject, TargetId);
			}
			if (!SourceNode || !TargetNode)
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("spec.edges[%d] references unknown node(s): %s -> %s"),
					EdgeIndex,
					*SourceId,
					*TargetId));
			}

			FString EdgeError;
			const FString EdgeRole = FString::Printf(TEXT("spec edge %d"), EdgeIndex);
			if (!ConnectCustomizableObjectPinsOrError(
				TargetGraph,
				SourceNode,
				{SourcePin},
				TargetNode,
				{TargetPin},
				EdgeRole,
				CreatedEdges,
				EdgeError))
			{
				return FBridgeToolResult::Error(EdgeError);
			}
		}
	}

	if (TargetGraph)
	{
		TargetGraph->NotifyGraphChanged();
	}
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("graph"), TargetGraph->GetName());
	Result->SetNumberField(TEXT("node_count"), CreatedNodes.Num());
	Result->SetNumberField(TEXT("edge_count"), CreatedEdges.Num());
	Result->SetArrayField(TEXT("created_nodes"), CreatedNodes);
	Result->SetArrayField(TEXT("created_edges"), CreatedEdges);
	if (PropertyWarnings.Num() > 0)
	{
		Result->SetStringField(TEXT("property_warnings"), FString::Join(PropertyWarnings, TEXT("; ")));
	}
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), CreatedNodes.Num() > 0 || CreatedEdges.Num() > 0);
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

FString UWireCustomizableObjectSlotFromTableTool::GetToolDescription() const
{
	return TEXT("Create and wire a NodeTable -> Material -> ComponentMesh slot chain in a Mutable/CustomizableObject graph.");
}

TMap<FString, FBridgeSchemaProperty> UWireCustomizableObjectSlotFromTableTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = CommonCustomizableObjectAssetSchema();

	for (const TCHAR* Name : {
		TEXT("parameter_name"),
		TEXT("data_table_path"),
		TEXT("filter_column"),
		TEXT("material_asset"),
		TEXT("component_mesh_node"),
	})
	{
		FBridgeSchemaProperty Prop;
		Prop.Type = TEXT("string");
		Prop.bRequired = true;
		Schema.Add(Name, Prop);
	}

	Schema.FindChecked(TEXT("parameter_name")).Description = TEXT("NodeTable ParameterName and slot identifier");
	Schema.FindChecked(TEXT("data_table_path")).Description = TEXT("DataTable asset path to assign to the NodeTable");
	Schema.FindChecked(TEXT("filter_column")).Description = TEXT("DataTable column used by the NodeTable filter");
	Schema.FindChecked(TEXT("material_asset")).Description = TEXT("UMaterialInterface asset path for the material constant node");
	Schema.FindChecked(TEXT("component_mesh_node")).Description = TEXT("Existing ComponentMesh node GUID, object path, object name, or title");

	FBridgeSchemaProperty FilterValues;
	FilterValues.Type = TEXT("array");
	FilterValues.Description = TEXT("Filter values to assign to the NodeTable Filters array");
	FilterValues.bRequired = true;
	FilterValues.ItemsType = TEXT("string");
	Schema.Add(TEXT("filter_values"), FilterValues);

	FBridgeSchemaProperty FilterValue;
	FilterValue.Type = TEXT("string");
	FilterValue.Description = TEXT("Single filter value alias for filter_values");
	Schema.Add(TEXT("filter_value"), FilterValue);

	FBridgeSchemaProperty FilterOperation;
	FilterOperation.Type = TEXT("string");
	FilterOperation.Description = TEXT("Filter operation for multiple values: OR or AND (default: OR)");
	FilterOperation.Enum.Add(TEXT("OR"));
	FilterOperation.Enum.Add(TEXT("AND"));
	Schema.Add(TEXT("filter_operation"), FilterOperation);

	FBridgeSchemaProperty LodIndex;
	LodIndex.Type = TEXT("integer");
	LodIndex.Description = TEXT("LOD index to wire (default: 0)");
	Schema.Add(TEXT("lod_index"), LodIndex);

	FBridgeSchemaProperty MaterialIndex;
	MaterialIndex.Type = TEXT("integer");
	MaterialIndex.Description = TEXT("Material slot index used in the NodeTable output pin name (default: 0)");
	Schema.Add(TEXT("material_index"), MaterialIndex);

	FBridgeSchemaProperty NodePosition;
	NodePosition.Type = TEXT("array");
	NodePosition.Description = TEXT("Optional [X, Y] position for the NodeTable; related nodes are offset from this position");
	NodePosition.ItemsType = TEXT("integer");
	Schema.Add(TEXT("node_position"), NodePosition);

	FBridgeSchemaProperty AddNoneOption;
	AddNoneOption.Type = TEXT("boolean");
	AddNoneOption.Description = TEXT("Set bAddNoneOption on the NodeTable (default: false)");
	Schema.Add(TEXT("add_none_option"), AddNoneOption);

	return Schema;
}

TArray<FString> UWireCustomizableObjectSlotFromTableTool::GetRequiredParams() const
{
	return {
		TEXT("asset_path"),
		TEXT("parameter_name"),
		TEXT("data_table_path"),
		TEXT("filter_column"),
		TEXT("filter_values"),
		TEXT("material_asset"),
		TEXT("component_mesh_node"),
	};
}

FBridgeToolResult UWireCustomizableObjectSlotFromTableTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ParameterName = GetStringArgOrDefault(Arguments, TEXT("parameter_name"));
	const FString DataTablePath = GetStringArgOrDefault(Arguments, TEXT("data_table_path"));
	const FString FilterColumn = GetStringArgOrDefault(Arguments, TEXT("filter_column"));
	const FString MaterialAsset = GetStringArgOrDefault(Arguments, TEXT("material_asset"));
	const FString ComponentMeshNodeRef = GetStringArgOrDefault(Arguments, TEXT("component_mesh_node"));
	FString FilterOperation = GetStringArgOrDefault(Arguments, TEXT("filter_operation"), TEXT("OR"));
	FilterOperation.ToUpperInline();
	const int32 LodIndex = GetIntArgOrDefault(Arguments, TEXT("lod_index"), 0);
	const int32 MaterialIndex = GetIntArgOrDefault(Arguments, TEXT("material_index"), 0);
	const bool bAddNoneOption = GetBoolArgOrDefault(Arguments, TEXT("add_none_option"), false);

	TArray<FString> FilterValues;
	const TArray<TSharedPtr<FJsonValue>>* FilterValuesArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("filter_values"), FilterValuesArray) && FilterValuesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *FilterValuesArray)
		{
			FString FilterValueString;
			if (Value.IsValid() && Value->TryGetString(FilterValueString) && !FilterValueString.IsEmpty())
			{
				FilterValues.Add(FilterValueString);
			}
		}
	}
	FString SingleFilterValue;
	if (Arguments->TryGetStringField(TEXT("filter_value"), SingleFilterValue) && !SingleFilterValue.IsEmpty())
	{
		FilterValues.Add(SingleFilterValue);
	}

	if (AssetPath.IsEmpty() || ParameterName.IsEmpty() || DataTablePath.IsEmpty() ||
		FilterColumn.IsEmpty() || FilterValues.Num() == 0 || MaterialAsset.IsEmpty() ||
		ComponentMeshNodeRef.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, parameter_name, data_table_path, filter_column, filter_values, material_asset, and component_mesh_node are required"));
	}
	if (FilterOperation != TEXT("OR") && FilterOperation != TEXT("AND"))
	{
		return FBridgeToolResult::Error(TEXT("filter_operation must be OR or AND"));
	}
	if (LodIndex < 0 || MaterialIndex < 0)
	{
		return FBridgeToolResult::Error(TEXT("lod_index and material_index must be non-negative"));
	}

	FVector2D BasePosition(0.0, 0.0);
	const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("node_position"), PositionArray) && PositionArray && PositionArray->Num() >= 2)
	{
		BasePosition.X = (*PositionArray)[0]->AsNumber();
		BasePosition.Y = (*PositionArray)[1]->AsNumber();
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

	UEdGraph* TargetGraph = ResolveGraph(AssetObject, TEXT(""));
	if (!TargetGraph)
	{
		return FBridgeToolResult::Error(TEXT("No graph found on CustomizableObject asset"));
	}

	UEdGraph* ComponentGraph = nullptr;
	UEdGraphNode* ComponentMeshNode = FindNode(AssetObject, ComponentMeshNodeRef, &ComponentGraph);
	if (!ComponentMeshNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("ComponentMesh node not found: %s"), *ComponentMeshNodeRef));
	}
	if (ComponentGraph != TargetGraph)
	{
		return FBridgeToolResult::Error(TEXT("ComponentMesh node must be in the same CustomizableObject graph as the new slot chain"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "WireCustomizableObjectSlotFromTable", "Wire CustomizableObject slot {0}"),
			FText::FromString(ParameterName)));

	FBridgeAssetModifier::MarkModified(AssetObject);
	FBridgeAssetModifier::MarkModified(TargetGraph);
	FBridgeAssetModifier::MarkModified(ComponentMeshNode);

	TArray<UEdGraphNode*> CreatedNodes;
	auto FailAfterCreation = [&](const FString& Message) -> FBridgeToolResult
	{
		for (UEdGraphNode* CreatedNode : CreatedNodes)
		{
			if (CreatedNode && CreatedNode->GetGraph())
			{
				FBridgeAssetModifier::MarkModified(CreatedNode);
				CreatedNode->BreakAllNodeLinks();
				CreatedNode->GetGraph()->RemoveNode(CreatedNode);
			}
		}
		TargetGraph->NotifyGraphChanged();
		FBridgeAssetModifier::MarkPackageDirty(AssetObject);
		return FBridgeToolResult::Error(Message);
	};

	TArray<TSharedPtr<FJsonValue>> FilterJsonValues;
	for (const FString& FilterValue : FilterValues)
	{
		FilterJsonValues.Add(MakeShared<FJsonValueString>(FilterValue));
	}

	TSharedPtr<FJsonObject> TableProperties = MakeShared<FJsonObject>();
	TableProperties->SetStringField(TEXT("Table"), DataTablePath);
	TableProperties->SetStringField(TEXT("ParameterName"), ParameterName);
	TableProperties->SetBoolField(TEXT("bAddNoneOption"), bAddNoneOption);

	TSharedPtr<FJsonObject> CompilationFilterOption = MakeShared<FJsonObject>();
	CompilationFilterOption->SetStringField(TEXT("FilterColumn"), FilterColumn);
	CompilationFilterOption->SetArrayField(TEXT("Filters"), FilterJsonValues);
	CompilationFilterOption->SetStringField(
		TEXT("OperationType"),
		FilterOperation == TEXT("AND") ? TEXT("TCFOT_AND") : TEXT("TCFOT_OR"));
	TArray<TSharedPtr<FJsonValue>> CompilationFilterOptions;
	CompilationFilterOptions.Add(MakeShared<FJsonValueObject>(CompilationFilterOption));
	TableProperties->SetArrayField(TEXT("CompilationFilterOptions"), CompilationFilterOptions);

	TArray<FString> TablePropertyWarnings;
	FString CreateError;
	UEdGraphNode* TableNode = CreateCustomizableObjectGraphNode(
		AssetObject,
		TargetGraph,
		TEXT("CustomizableObjectNodeTable"),
		BasePosition,
		TableProperties,
		TablePropertyWarnings,
		CreateError);
	if (!TableNode)
	{
		return FBridgeToolResult::Error(CreateError);
	}
	CreatedNodes.Add(TableNode);
	if (TablePropertyWarnings.Num() > 0)
	{
		return FailAfterCreation(FString::Printf(
			TEXT("Failed to configure NodeTable: %s"),
			*FString::Join(TablePropertyWarnings, TEXT("; "))));
	}

	TArray<FString> MaterialPropertyWarnings;
	UEdGraphNode* MaterialNode = CreateCustomizableObjectGraphNode(
		AssetObject,
		TargetGraph,
		TEXT("CustomizableObjectNodeMaterial"),
		FVector2D(BasePosition.X + 360.0, BasePosition.Y),
		MakeShared<FJsonObject>(),
		MaterialPropertyWarnings,
		CreateError);
	if (!MaterialNode)
	{
		return FailAfterCreation(CreateError);
	}
	CreatedNodes.Add(MaterialNode);

	TSharedPtr<FJsonObject> MaterialConstantProperties = MakeShared<FJsonObject>();
	MaterialConstantProperties->SetStringField(TEXT("Material"), MaterialAsset);

	TArray<FString> MaterialConstantPropertyWarnings;
	UEdGraphNode* MaterialConstantNode = nullptr;
	for (const TCHAR* MaterialConstantClass : {TEXT("CONodeMaterialConstant"), TEXT("CustomizableObjectNodeMaterialConstant")})
	{
		CreateError.Empty();
		MaterialConstantNode = CreateCustomizableObjectGraphNode(
			AssetObject,
			TargetGraph,
			MaterialConstantClass,
			FVector2D(BasePosition.X + 360.0, BasePosition.Y + 220.0),
			MaterialConstantProperties,
			MaterialConstantPropertyWarnings,
			CreateError);
		if (MaterialConstantNode)
		{
			break;
		}
	}
	FString MaterialAssignmentMode;
	if (MaterialConstantNode)
	{
		CreatedNodes.Add(MaterialConstantNode);
		if (MaterialConstantPropertyWarnings.Num() > 0)
		{
			return FailAfterCreation(FString::Printf(
				TEXT("Failed to configure Material Constant node: %s"),
				*FString::Join(MaterialConstantPropertyWarnings, TEXT("; "))));
		}
		MaterialAssignmentMode = TEXT("material_constant_node");
	}
	else
	{
		MaterialAssignmentMode = TEXT("material_node_property");
		TSharedPtr<FJsonObject> MaterialNodeProperties = MakeShared<FJsonObject>();
		MaterialNodeProperties->SetStringField(TEXT("Material"), MaterialAsset);
		TArray<FString> MaterialAssignmentPropertyWarnings = ApplyReflectedProperties(MaterialNode, MaterialNodeProperties);
		if (MaterialAssignmentPropertyWarnings.Num() > 0)
		{
			return FailAfterCreation(FString::Printf(
				TEXT("Failed to assign material on Material node: %s"),
				*FString::Join(MaterialAssignmentPropertyWarnings, TEXT("; "))));
		}
		MaterialPropertyWarnings.Append(MaterialAssignmentPropertyWarnings);
		RefreshCustomizableObjectNodePins(MaterialNode);
		MaterialNode->ReconstructNode();
	}

	RefreshCustomizableObjectNodePins(TableNode);
	RefreshCustomizableObjectNodePins(MaterialNode);
	if (MaterialConstantNode)
	{
		RefreshCustomizableObjectNodePins(MaterialConstantNode);
	}
	RefreshCustomizableObjectNodePins(ComponentMeshNode);

	TArray<TSharedPtr<FJsonValue>> CreatedEdges;
	FString ConnectError;
	if (!ConnectCustomizableObjectPinsOrError(
		TargetGraph,
		TableNode,
		{FString::Printf(TEXT("Mesh LOD_%d Mat_%d"), LodIndex, MaterialIndex)},
		MaterialNode,
		{TEXT("Mesh_Input_Pin"), TEXT("Mesh")},
		TEXT("table_mesh_to_material"),
		CreatedEdges,
		ConnectError))
	{
		return FailAfterCreation(ConnectError);
	}

	if (MaterialConstantNode && !ConnectCustomizableObjectPinsOrError(
		TargetGraph,
		MaterialConstantNode,
		{TEXT("Material")},
		MaterialNode,
		{TEXT("Material_Input_Pin"), TEXT("Material")},
		TEXT("material_constant_to_material"),
		CreatedEdges,
		ConnectError))
	{
		return FailAfterCreation(ConnectError);
	}

	if (!ConnectCustomizableObjectPinsOrError(
		TargetGraph,
		MaterialNode,
		{TEXT("Mesh Section_Output_Pin"), TEXT("Mesh Section"), TEXT("MeshSection")},
		ComponentMeshNode,
		{FString::Printf(TEXT("LOD %d"), LodIndex), FString::Printf(TEXT("LOD_%d"), LodIndex)},
		TEXT("material_to_component_mesh"),
		CreatedEdges,
		ConnectError))
	{
		return FailAfterCreation(ConnectError);
	}

	TargetGraph->NotifyGraphChanged();
	FBridgeAssetModifier::MarkPackageDirty(AssetObject);

	TArray<TSharedPtr<FJsonValue>> CreatedNodeValues;
	CreatedNodeValues.Add(MakeShared<FJsonValueObject>(BuildCreatedNodeSummary(TEXT("node_table"), TableNode)));
	CreatedNodeValues.Add(MakeShared<FJsonValueObject>(BuildCreatedNodeSummary(TEXT("material"), MaterialNode)));
	if (MaterialConstantNode)
	{
		CreatedNodeValues.Add(MakeShared<FJsonValueObject>(BuildCreatedNodeSummary(TEXT("material_constant"), MaterialConstantNode)));
	}

	TArray<FString> PropertyWarnings;
	PropertyWarnings.Append(TablePropertyWarnings);
	PropertyWarnings.Append(MaterialPropertyWarnings);
	PropertyWarnings.Append(MaterialConstantPropertyWarnings);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("graph"), TargetGraph->GetName());
	Result->SetStringField(TEXT("graph_path"), TargetGraph->GetPathName());
	Result->SetStringField(TEXT("parameter_name"), ParameterName);
	Result->SetStringField(TEXT("data_table_path"), DataTablePath);
	Result->SetStringField(TEXT("filter_column"), FilterColumn);
	Result->SetArrayField(TEXT("filter_values"), FilterJsonValues);
	Result->SetStringField(TEXT("filter_operation"), FilterOperation);
	Result->SetStringField(TEXT("material_assignment_mode"), MaterialAssignmentMode);
	Result->SetStringField(TEXT("component_mesh_node"), ComponentMeshNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetNumberField(TEXT("lod_index"), LodIndex);
	Result->SetNumberField(TEXT("material_index"), MaterialIndex);
	Result->SetArrayField(TEXT("created_nodes"), CreatedNodeValues);
	Result->SetArrayField(TEXT("created_edges"), CreatedEdges);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	if (PropertyWarnings.Num() > 0)
	{
		Result->SetStringField(TEXT("property_warnings"), FString::Join(PropertyWarnings, TEXT("; ")));
	}
	return FBridgeToolResult::Json(Result);
}
