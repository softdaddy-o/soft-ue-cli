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

			FString SetError;
			if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Pair.Value, SetError))
			{
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

			if ((*ValuePtr)->Type == EJson::Number)
			{
				Pin->DefaultValue = FString::Printf(TEXT("%g"), (*ValuePtr)->AsNumber());
			}
			else if ((*ValuePtr)->Type == EJson::Boolean)
			{
				Pin->DefaultValue = (*ValuePtr)->AsBool() ? TEXT("true") : TEXT("false");
			}
			else
			{
				Pin->DefaultValue = (*ValuePtr)->AsString();
			}
			ResolvedByPin.Add(PropertyName);
		}

		for (const FString& PropertyName : ResolvedByPin)
		{
			Errors.Remove(FString::Printf(TEXT("Property not found: %s"), *PropertyName));
		}

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
	if (!SourcePin)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source pin not found: %s"), *SourcePinName));
	}
	if (!TargetPin)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target pin not found: %s"), *TargetPinName));
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

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		NSLOCTEXT("SoftUEBridge", "ConnectCustomizableObjectPins", "Connect CustomizableObject pins"));

	FBridgeAssetModifier::MarkModified(AssetObject);
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
	const bool bCompileCalled = TryCompileWithFunctionLibrary(AssetObject, CompileState, bCompileSucceeded, CompileError);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bCompileCalled && bCompileSucceeded);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("loaded_class"), AssetObject->GetClass()->GetName());
	Result->SetBoolField(TEXT("compile_requested"), bCompileCalled);
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
