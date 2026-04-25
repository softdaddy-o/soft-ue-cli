// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Blueprint/QueryBlueprintGraphTool.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "EdGraphSchema_K2.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

// Animation Blueprint support
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Base.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "Animation/AnimNodeBase.h"
#include "Utils/BridgePropertySerializer.h"

FString UQueryBlueprintGraphTool::GetToolDescription() const
{
	return TEXT("Query Blueprint graphs: list all graphs, get specific node by GUID, get callable details, or list callables. "
		"For AnimBlueprints: also returns anim_graph, state_machine, state, transition, blend_stack graphs. "
		"Use node_guid for specific node, callable_name for callable graph, list_callables=true for callable summary. "
		"Use include_anim_node_properties=true to read embedded FAnimNode struct properties (play rates, sequences, blend settings).");
}

TMap<FString, FBridgeSchemaProperty> UQueryBlueprintGraphTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint (e.g., /Game/Blueprints/BP_Character)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	// Mode selectors (mutually exclusive)
	FBridgeSchemaProperty NodeGuid;
	NodeGuid.Type = TEXT("string");
	NodeGuid.Description = TEXT("Get specific node by GUID (e.g., 12345678-1234-1234-1234-123456789ABC)");
	NodeGuid.bRequired = false;
	Schema.Add(TEXT("node_guid"), NodeGuid);

	FBridgeSchemaProperty CallableName;
	CallableName.Type = TEXT("string");
	CallableName.Description = TEXT("Get specific callable's graph by name (event, function, or macro)");
	CallableName.bRequired = false;
	Schema.Add(TEXT("callable_name"), CallableName);

	FBridgeSchemaProperty ListCallables;
	ListCallables.Type = TEXT("boolean");
	ListCallables.Description = TEXT("List all callables (events, functions, macros) without full graph details (default: false)");
	ListCallables.bRequired = false;
	Schema.Add(TEXT("list_callables"), ListCallables);

	// Filtering options
	FBridgeSchemaProperty GraphName;
	GraphName.Type = TEXT("string");
	GraphName.Description = TEXT("Filter by specific graph name");
	GraphName.bRequired = false;
	Schema.Add(TEXT("graph_name"), GraphName);

	FBridgeSchemaProperty GraphType;
	GraphType.Type = TEXT("string");
	GraphType.Description = TEXT("Filter by graph type: 'event', 'function', 'macro', 'interface', or for AnimBlueprints: 'anim_graph', 'state_machine', 'state', 'transition', 'blend_stack'");
	GraphType.bRequired = false;
	Schema.Add(TEXT("graph_type"), GraphType);

	FBridgeSchemaProperty IncludePositions;
	IncludePositions.Type = TEXT("boolean");
	IncludePositions.Description = TEXT("Include node X/Y positions (default: false)");
	IncludePositions.bRequired = false;
	Schema.Add(TEXT("include_positions"), IncludePositions);

	FBridgeSchemaProperty Search;
	Search.Type = TEXT("string");
	Search.Description = TEXT("Filter nodes by title or class name (wildcards supported: *pattern*)");
	Search.bRequired = false;
	Schema.Add(TEXT("search"), Search);

	FBridgeSchemaProperty IncludeAnimNodeProps;
	IncludeAnimNodeProps.Type = TEXT("boolean");
	IncludeAnimNodeProps.Description = TEXT("For AnimBlueprints: include embedded FAnimNode struct properties on AnimGraph nodes (e.g., play rates, sequences, blend settings). Default: false");
	IncludeAnimNodeProps.bRequired = false;
	Schema.Add(TEXT("include_anim_node_properties"), IncludeAnimNodeProps);

	return Schema;
}

TArray<FString> UQueryBlueprintGraphTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryBlueprintGraphTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	// Normalize: collapse leading double-slashes to a single slash to avoid
	// UE's "package name containing double slashes" fatal assert.
	while (AssetPath.StartsWith(TEXT("//")))
	{
		AssetPath.RemoveAt(0);
	}

	// Mode selectors
	FString NodeGuidStr = GetStringArgOrDefault(Arguments, TEXT("node_guid"), TEXT(""));
	FString CallableName = GetStringArgOrDefault(Arguments, TEXT("callable_name"), TEXT(""));
	bool bListCallables = GetBoolArgOrDefault(Arguments, TEXT("list_callables"), false);

	// Filtering
	FString GraphNameFilter = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT(""));
	FString GraphTypeFilter = GetStringArgOrDefault(Arguments, TEXT("graph_type"), TEXT(""));
	FString SearchFilter = GetStringArgOrDefault(Arguments, TEXT("search"), TEXT(""));

	FGraphSerializeOptions SerializeOptions;
	SerializeOptions.bIncludePositions = GetBoolArgOrDefault(Arguments, TEXT("include_positions"), false);
	SerializeOptions.bIncludeAnimNodeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_anim_node_properties"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-blueprint-graph: path='%s', search='%s'"), *AssetPath, *SearchFilter);

	// Load Blueprint (could be AnimBlueprint)
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load Blueprint: %s"), *AssetPath));
	}

	// Check if this is an AnimBlueprint for specialized handling
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	bool bIsAnimBlueprint = (AnimBP != nullptr);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("blueprint"), AssetPath);
	Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
	Result->SetBoolField(TEXT("is_anim_blueprint"), bIsAnimBlueprint);

	if (bIsAnimBlueprint && AnimBP->TargetSkeleton)
	{
		Result->SetStringField(TEXT("target_skeleton"), AnimBP->TargetSkeleton->GetPathName());
	}

	// === Mode 1: Get specific node by GUID ===
	if (!NodeGuidStr.IsEmpty())
	{
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeGuidStr, NodeGuid))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid GUID format: %s"), *NodeGuidStr));
		}

		FString GraphName, GraphType;
		UEdGraphNode* Node = FindNodeByGuid(Blueprint, NodeGuid, GraphName, GraphType);

		// If not found and is AnimBlueprint, search in animation graphs
		if (!Node && bIsAnimBlueprint)
		{
			Node = FindNodeInAnimGraphs(AnimBP, NodeGuid, GraphName, GraphType);
		}

		if (!Node)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeGuidStr));
		}

		Result->SetStringField(TEXT("graph_name"), GraphName);
		Result->SetStringField(TEXT("graph_type"), GraphType);
		Result->SetObjectField(TEXT("node"), NodeToJson(Node, SerializeOptions));

		return FBridgeToolResult::Json(Result);
	}

	// === Mode 2: Get specific callable's graph ===
	if (!CallableName.IsEmpty())
	{
		FString GraphType;
		UEdGraph* Graph = FindGraphByName(Blueprint, CallableName, GraphType);

		// If not found and is AnimBlueprint, search animation graphs
		if (!Graph && bIsAnimBlueprint)
		{
			Graph = FindAnimGraphByName(AnimBP, CallableName, GraphType);
		}

		if (!Graph)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Callable not found: %s"), *CallableName));
		}

		Result->SetObjectField(TEXT("graph"), GraphToJson(Graph, GraphType, SerializeOptions, SearchFilter));

		return FBridgeToolResult::Json(Result);
	}

	// === Mode 3: List callables (lightweight) ===
	if (bListCallables)
	{
		TArray<TSharedPtr<FJsonValue>> EventsArray;
		TArray<TSharedPtr<FJsonValue>> FunctionsArray;
		TArray<TSharedPtr<FJsonValue>> MacrosArray;

		ExtractEvents(Blueprint, EventsArray);
		ExtractFunctions(Blueprint, FunctionsArray);
		ExtractMacros(Blueprint, MacrosArray);

		Result->SetArrayField(TEXT("events"), EventsArray);
		Result->SetArrayField(TEXT("functions"), FunctionsArray);
		Result->SetArrayField(TEXT("macros"), MacrosArray);
		Result->SetNumberField(TEXT("event_count"), EventsArray.Num());
		Result->SetNumberField(TEXT("function_count"), FunctionsArray.Num());
		Result->SetNumberField(TEXT("macro_count"), MacrosArray.Num());

		// Add AnimBlueprint-specific callables
		if (bIsAnimBlueprint)
		{
			TArray<TSharedPtr<FJsonValue>> AnimCallablesArray;
			ExtractAnimCallables(AnimBP, AnimCallablesArray);
			Result->SetArrayField(TEXT("anim_graphs"), AnimCallablesArray);
			Result->SetNumberField(TEXT("anim_graph_count"), AnimCallablesArray.Num());
		}

		return FBridgeToolResult::Json(Result);
	}

	// === Mode 4: List all graphs with full node details ===
	TArray<TSharedPtr<FJsonValue>> GraphsArray;

	// Event graphs
	if (GraphTypeFilter.IsEmpty() || GraphTypeFilter == TEXT("event"))
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (!Graph) continue;
			if (!GraphNameFilter.IsEmpty() && Graph->GetName() != GraphNameFilter) continue;

			TSharedPtr<FJsonObject> GraphJson = GraphToJson(Graph, TEXT("event"), SerializeOptions, SearchFilter);
			if (GraphJson.IsValid())
			{
				GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphJson)));
			}
		}
	}

	// Function graphs
	if (GraphTypeFilter.IsEmpty() || GraphTypeFilter == TEXT("function"))
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (!Graph) continue;
			if (!GraphNameFilter.IsEmpty() && Graph->GetName() != GraphNameFilter) continue;

			TSharedPtr<FJsonObject> GraphJson = GraphToJson(Graph, TEXT("function"), SerializeOptions, SearchFilter);
			if (GraphJson.IsValid())
			{
				GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphJson)));
			}
		}
	}

	// Macro graphs
	if (GraphTypeFilter.IsEmpty() || GraphTypeFilter == TEXT("macro"))
	{
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (!Graph) continue;
			if (!GraphNameFilter.IsEmpty() && Graph->GetName() != GraphNameFilter) continue;

			TSharedPtr<FJsonObject> GraphJson = GraphToJson(Graph, TEXT("macro"), SerializeOptions, SearchFilter);
			if (GraphJson.IsValid())
			{
				GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphJson)));
			}
		}
	}

	// Interface implementation graphs
	if (GraphTypeFilter.IsEmpty() || GraphTypeFilter == TEXT("interface"))
	{
		for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			if (!InterfaceDesc.Interface) continue;
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (!Graph) continue;
				if (!GraphNameFilter.IsEmpty() && Graph->GetName() != GraphNameFilter) continue;
				TSharedPtr<FJsonObject> GraphJson = GraphToJson(Graph, TEXT("interface"), SerializeOptions, SearchFilter);
				if (GraphJson.IsValid())
				{
					GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphJson)));
				}
			}
		}
	}

	// Animation Blueprint specific graphs
	if (bIsAnimBlueprint)
	{
		ProcessAnimBlueprintGraphs(AnimBP, GraphNameFilter, GraphTypeFilter, SerializeOptions, SearchFilter, GraphsArray);
	}

	Result->SetArrayField(TEXT("graphs"), GraphsArray);
	Result->SetNumberField(TEXT("graph_count"), GraphsArray.Num());

	return FBridgeToolResult::Json(Result);
}

// === Graph conversion ===

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::GraphToJson(UEdGraph* Graph, const FString& GraphType, const FGraphSerializeOptions& Options, const FString& SearchFilter) const
{
	if (!Graph)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);
	GraphJson->SetStringField(TEXT("name"), Graph->GetName());
	GraphJson->SetStringField(TEXT("type"), GraphType);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		// Apply search filter on node title and class
		if (!SearchFilter.IsEmpty())
		{
			FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			FString NodeClass = Node->GetClass()->GetName();
			if (!MatchesWildcard(NodeTitle, SearchFilter) && !MatchesWildcard(NodeClass, SearchFilter))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> NodeJson = NodeToJson(Node, Options);
		if (NodeJson.IsValid())
		{
			NodesArray.Add(MakeShareable(new FJsonValueObject(NodeJson)));
		}
	}

	GraphJson->SetArrayField(TEXT("nodes"), NodesArray);
	GraphJson->SetNumberField(TEXT("node_count"), NodesArray.Num());

	return GraphJson;
}

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::NodeToJson(UEdGraphNode* Node, const FGraphSerializeOptions& Options) const
{
	if (!Node)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

	NodeJson->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	if (!Node->NodeComment.IsEmpty())
	{
		NodeJson->SetStringField(TEXT("comment"), Node->NodeComment);
	}

	if (Options.bIncludePositions)
	{
		TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
		PositionJson->SetNumberField(TEXT("x"), Node->NodePosX);
		PositionJson->SetNumberField(TEXT("y"), Node->NodePosY);
		NodeJson->SetObjectField(TEXT("position"), PositionJson);
	}

	if (Options.bIncludeAnimNodeProperties)
	{
		if (UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Node))
		{
			TSharedPtr<FJsonObject> AnimPropsJson = ExtractAnimNodeProperties(AnimGraphNode);
			if (AnimPropsJson.IsValid())
			{
				NodeJson->SetObjectField(TEXT("anim_node_properties"), AnimPropsJson);
			}
		}
	}

	// Pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;

		TSharedPtr<FJsonObject> PinJson = PinToJson(Pin);
		if (PinJson.IsValid())
		{
			PinsArray.Add(MakeShareable(new FJsonValueObject(PinJson)));
		}
	}
	NodeJson->SetArrayField(TEXT("pins"), PinsArray);

	return NodeJson;
}

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::PinToJson(UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinJson = MakeShareable(new FJsonObject);

	PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinJson->SetStringField(TEXT("direction"), GetPinDirectionString(Pin->Direction));
	PinJson->SetStringField(TEXT("category"), GetPinCategoryString(Pin->PinType.PinCategory));

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		PinJson->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetName());
	}

	PinJson->SetBoolField(TEXT("is_array"), Pin->PinType.IsArray());

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}

	// Connections
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin) continue;
		UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode) continue;

		TSharedPtr<FJsonObject> ConnectionJson = MakeShareable(new FJsonObject);
		ConnectionJson->SetStringField(TEXT("node_guid"), LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		ConnectionJson->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnectionJson)));
	}

	if (ConnectionsArray.Num() > 0)
	{
		PinJson->SetArrayField(TEXT("connections"), ConnectionsArray);
	}

	return PinJson;
}

// === Node search ===

UEdGraphNode* UQueryBlueprintGraphTool::FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid, FString& OutGraphName, FString& OutGraphType) const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Search event graphs
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				OutGraphName = Graph->GetName();
				OutGraphType = TEXT("event");
				return Node;
			}
		}
	}

	// Search function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				OutGraphName = Graph->GetName();
				OutGraphType = TEXT("function");
				return Node;
			}
		}
	}

	// Search macro graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				OutGraphName = Graph->GetName();
				OutGraphType = TEXT("macro");
				return Node;
			}
		}
	}

	// Search interface implementation graphs
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (!InterfaceDesc.Interface) continue;
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					OutGraphName = Graph->GetName();
					OutGraphType = TEXT("interface");
					return Node;
				}
			}
		}
	}

	return nullptr;
}

UEdGraph* UQueryBlueprintGraphTool::FindGraphByName(UBlueprint* Blueprint, const FString& CallableName, FString& OutGraphType) const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Search event graphs for matching event
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;

		// Check if graph name matches
		if (Graph->GetName().Equals(CallableName, ESearchCase::IgnoreCase))
		{
			OutGraphType = TEXT("event");
			return Graph;
		}

		// Also check for events within the graph
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->GetFunctionName().ToString().Equals(CallableName, ESearchCase::IgnoreCase))
				{
					OutGraphType = TEXT("event");
					return Graph;
				}
			}
			if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
			{
				if (CustomEvent->CustomFunctionName.ToString().Equals(CallableName, ESearchCase::IgnoreCase))
				{
					OutGraphType = TEXT("event");
					return Graph;
				}
			}
		}
	}

	// Search function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		if (Graph->GetName().Equals(CallableName, ESearchCase::IgnoreCase))
		{
			OutGraphType = TEXT("function");
			return Graph;
		}
	}

	// Search macro graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;
		if (Graph->GetName().Equals(CallableName, ESearchCase::IgnoreCase))
		{
			OutGraphType = TEXT("macro");
			return Graph;
		}
	}

	// Search interface implementation graphs
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (!InterfaceDesc.Interface) continue;
		for (UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (!Graph) continue;
			if (Graph->GetName().Equals(CallableName, ESearchCase::IgnoreCase))
			{
				OutGraphType = TEXT("interface");
				return Graph;
			}
		}
	}

	return nullptr;
}

// === Callable extraction ===

void UQueryBlueprintGraphTool::ExtractEvents(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Blueprint) return;

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
				EventObj->SetStringField(TEXT("name"), EventNode->GetFunctionName().ToString());
				EventObj->SetStringField(TEXT("type"), Cast<UK2Node_CustomEvent>(EventNode) ? TEXT("custom") : TEXT("native"));
				EventObj->SetStringField(TEXT("graph"), Graph->GetName());

				OutArray.Add(MakeShareable(new FJsonValueObject(EventObj)));
			}
		}
	}
}

void UQueryBlueprintGraphTool::ExtractFunctions(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Blueprint) return;

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;

		TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject);
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());

		// Find entry node for signature
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				TArray<TSharedPtr<FJsonValue>> ParamsArray;
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && !Pin->PinName.IsEqual(UEdGraphSchema_K2::PN_Then))
					{
						TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
						ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						ParamObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
						ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
					}
				}
				FuncObj->SetArrayField(TEXT("parameters"), ParamsArray);
				break;
			}
		}

		OutArray.Add(MakeShareable(new FJsonValueObject(FuncObj)));
	}
}

void UQueryBlueprintGraphTool::ExtractMacros(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Blueprint) return;

	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;

		TSharedPtr<FJsonObject> MacroObj = MakeShareable(new FJsonObject);
		MacroObj->SetStringField(TEXT("name"), Graph->GetName());

		OutArray.Add(MakeShareable(new FJsonValueObject(MacroObj)));
	}
}

// === Helpers ===

FString UQueryBlueprintGraphTool::GetPinCategoryString(FName Category) const
{
	return Category.ToString();
}

FString UQueryBlueprintGraphTool::GetPinDirectionString(EEdGraphPinDirection Direction) const
{
	return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
}

// === Animation Blueprint support ===

FString UQueryBlueprintGraphTool::GetAnimGraphTypeString(UEdGraph* Graph) const
{
	if (!Graph)
	{
		return TEXT("unknown");
	}

	if (Graph->IsA<UAnimationStateMachineGraph>())
	{
		return TEXT("state_machine");
	}
	if (Graph->IsA<UAnimationStateGraph>())
	{
		return TEXT("state");
	}
	if (Graph->IsA<UAnimationTransitionGraph>())
	{
		return TEXT("transition");
	}

	// Check class name for AnimationGraph and variants
	FString ClassName = Graph->GetClass()->GetName();
	if (ClassName.Contains(TEXT("AnimationBlendStackGraph")))
	{
		return TEXT("blend_stack");
	}
	if (ClassName.Contains(TEXT("AnimationGraph")))
	{
		return TEXT("anim_graph");
	}

	return TEXT("anim_unknown");
}

void UQueryBlueprintGraphTool::ProcessAnimBlueprintGraphs(
	UAnimBlueprint* AnimBP,
	const FString& GraphNameFilter,
	const FString& GraphTypeFilter,
	const FGraphSerializeOptions& Options,
	const FString& SearchFilter,
	TArray<TSharedPtr<FJsonValue>>& OutGraphsArray) const
{
	if (!AnimBP)
	{
		return;
	}

	// Use GetAllGraphs to iterate through all animation graphs
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		// Skip standard Blueprint graphs (already processed)
		if (AnimBP->UbergraphPages.Contains(Graph) ||
			AnimBP->FunctionGraphs.Contains(Graph) ||
			AnimBP->MacroGraphs.Contains(Graph))
		{
			continue;
		}

		// Apply name filter
		if (!GraphNameFilter.IsEmpty() && Graph->GetName() != GraphNameFilter)
		{
			continue;
		}

		FString AnimGraphType = GetAnimGraphTypeString(Graph);

		// Apply type filter - check both animation-specific types and allow empty filter
		if (!GraphTypeFilter.IsEmpty() &&
			GraphTypeFilter != AnimGraphType &&
			GraphTypeFilter != TEXT("anim_graph") &&
			GraphTypeFilter != TEXT("state_machine") &&
			GraphTypeFilter != TEXT("state") &&
			GraphTypeFilter != TEXT("transition") &&
			GraphTypeFilter != TEXT("blend_stack"))
		{
			// If filter is a standard type (event/function/macro), skip anim graphs
			continue;
		}

		// If filter is set to a specific anim type, only include matching
		if (!GraphTypeFilter.IsEmpty() && GraphTypeFilter != AnimGraphType)
		{
			continue;
		}

		// Process the graph
		TSharedPtr<FJsonObject> GraphJson = GraphToJson(Graph, AnimGraphType, Options, SearchFilter);
		if (GraphJson.IsValid())
		{
			// Add animation-specific metadata for state machines
			if (UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Graph))
			{
				ExtractStateMachineHierarchy(StateMachineGraph, GraphJson, Options.bIncludePositions);
			}

			OutGraphsArray.Add(MakeShareable(new FJsonValueObject(GraphJson)));
		}
	}
}

void UQueryBlueprintGraphTool::ExtractStateMachineHierarchy(
	UAnimationStateMachineGraph* StateMachineGraph,
	TSharedPtr<FJsonObject>& OutGraphJson,
	bool bIncludePositions) const
{
	if (!StateMachineGraph)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> StatesArray;
	TArray<TSharedPtr<FJsonValue>> TransitionsArray;
	TArray<TSharedPtr<FJsonValue>> ConduitsArray;

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Extract state nodes
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			TSharedPtr<FJsonObject> StateJson = AnimStateNodeToJson(StateNode, bIncludePositions);
			if (StateJson.IsValid())
			{
				StatesArray.Add(MakeShareable(new FJsonValueObject(StateJson)));
			}
		}
		// Extract transition nodes
		else if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
		{
			TSharedPtr<FJsonObject> TransitionJson = TransitionNodeToJson(TransitionNode, bIncludePositions);
			if (TransitionJson.IsValid())
			{
				TransitionsArray.Add(MakeShareable(new FJsonValueObject(TransitionJson)));
			}
		}
		// Extract conduit nodes
		else if (UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
		{
			TSharedPtr<FJsonObject> ConduitJson = MakeShareable(new FJsonObject);
			ConduitJson->SetStringField(TEXT("guid"), ConduitNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
			ConduitJson->SetStringField(TEXT("name"), ConduitNode->GetStateName());
			ConduitJson->SetStringField(TEXT("type"), TEXT("conduit"));

			if (bIncludePositions)
			{
				TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
				PositionJson->SetNumberField(TEXT("x"), ConduitNode->NodePosX);
				PositionJson->SetNumberField(TEXT("y"), ConduitNode->NodePosY);
				ConduitJson->SetObjectField(TEXT("position"), PositionJson);
			}

			ConduitsArray.Add(MakeShareable(new FJsonValueObject(ConduitJson)));
		}
	}

	OutGraphJson->SetArrayField(TEXT("states"), StatesArray);
	OutGraphJson->SetArrayField(TEXT("transitions"), TransitionsArray);
	OutGraphJson->SetArrayField(TEXT("conduits"), ConduitsArray);
	OutGraphJson->SetNumberField(TEXT("state_count"), StatesArray.Num());
	OutGraphJson->SetNumberField(TEXT("transition_count"), TransitionsArray.Num());
	OutGraphJson->SetNumberField(TEXT("conduit_count"), ConduitsArray.Num());
}

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::AnimStateNodeToJson(UAnimStateNode* StateNode, bool bIncludePositions) const
{
	if (!StateNode)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);
	StateJson->SetStringField(TEXT("guid"), StateNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	StateJson->SetStringField(TEXT("name"), StateNode->GetStateName());
	StateJson->SetStringField(TEXT("type"), TEXT("state"));

	if (bIncludePositions)
	{
		TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
		PositionJson->SetNumberField(TEXT("x"), StateNode->NodePosX);
		PositionJson->SetNumberField(TEXT("y"), StateNode->NodePosY);
		StateJson->SetObjectField(TEXT("position"), PositionJson);
	}

	// Check if this state has a nested graph (UAnimationStateGraph)
	if (UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->BoundGraph))
	{
		StateJson->SetBoolField(TEXT("has_graph"), true);
		StateJson->SetStringField(TEXT("graph_name"), StateGraph->GetName());
	}

	return StateJson;
}

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::TransitionNodeToJson(UAnimStateTransitionNode* TransitionNode, bool bIncludePositions) const
{
	if (!TransitionNode)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> TransitionJson = MakeShareable(new FJsonObject);
	TransitionJson->SetStringField(TEXT("guid"), TransitionNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	TransitionJson->SetStringField(TEXT("type"), TEXT("transition"));

	// Get connected states
	if (UAnimStateNodeBase* PrevState = TransitionNode->GetPreviousState())
	{
		TransitionJson->SetStringField(TEXT("from_state"), PrevState->GetStateName());
	}
	if (UAnimStateNodeBase* NextState = TransitionNode->GetNextState())
	{
		TransitionJson->SetStringField(TEXT("to_state"), NextState->GetStateName());
	}

	// Check if has transition graph
	if (UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph))
	{
		TransitionJson->SetBoolField(TEXT("has_graph"), true);
		TransitionJson->SetStringField(TEXT("graph_name"), TransitionGraph->GetName());
	}

	if (bIncludePositions)
	{
		TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
		PositionJson->SetNumberField(TEXT("x"), TransitionNode->NodePosX);
		PositionJson->SetNumberField(TEXT("y"), TransitionNode->NodePosY);
		TransitionJson->SetObjectField(TEXT("position"), PositionJson);
	}

	return TransitionJson;
}

UEdGraphNode* UQueryBlueprintGraphTool::FindNodeInAnimGraphs(
	UAnimBlueprint* AnimBP,
	const FGuid& NodeGuid,
	FString& OutGraphName,
	FString& OutGraphType) const
{
	if (!AnimBP)
	{
		return nullptr;
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		// Skip already searched standard graphs
		if (AnimBP->UbergraphPages.Contains(Graph) ||
			AnimBP->FunctionGraphs.Contains(Graph) ||
			AnimBP->MacroGraphs.Contains(Graph))
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				OutGraphName = Graph->GetName();
				OutGraphType = GetAnimGraphTypeString(Graph);
				return Node;
			}
		}
	}

	return nullptr;
}

UEdGraph* UQueryBlueprintGraphTool::FindAnimGraphByName(
	UAnimBlueprint* AnimBP,
	const FString& GraphName,
	FString& OutGraphType) const
{
	if (!AnimBP)
	{
		return nullptr;
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			OutGraphType = GetAnimGraphTypeString(Graph);
			return Graph;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> UQueryBlueprintGraphTool::ExtractAnimNodeProperties(UAnimGraphNode_Base* AnimGraphNode) const
{
	if (!AnimGraphNode)
	{
		return nullptr;
	}

	UScriptStruct* AnimNodeBaseStruct = FAnimNode_Base::StaticStruct();

	// Find the embedded FAnimNode_* struct property via reflection
	for (TFieldIterator<FStructProperty> It(AnimGraphNode->GetClass()); It; ++It)
	{
		FStructProperty* StructProp = *It;
		UScriptStruct* Struct = StructProp->Struct;

		if (!Struct || !Struct->IsChildOf(AnimNodeBaseStruct))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("struct_type"), Struct->GetName());

		const void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(AnimGraphNode);

		// Serialize all editable properties of the embedded struct
		TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);
		for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;

			// Skip transient/internal runtime state
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
			{
				continue;
			}

			TSharedPtr<FJsonValue> Value = FBridgePropertySerializer::SerializePropertyValue(
				Property, StructPtr, AnimGraphNode, 0, 2);

			if (Value.IsValid())
			{
				PropsObj->SetField(Property->GetName(), Value);
			}
		}

		Result->SetObjectField(TEXT("properties"), PropsObj);
		return Result;
	}

	return nullptr;
}

void UQueryBlueprintGraphTool::ExtractAnimCallables(UAnimBlueprint* AnimBP, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!AnimBP)
	{
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		// Skip standard Blueprint graphs
		if (AnimBP->UbergraphPages.Contains(Graph) ||
			AnimBP->FunctionGraphs.Contains(Graph) ||
			AnimBP->MacroGraphs.Contains(Graph))
		{
			continue;
		}

		FString GraphType = GetAnimGraphTypeString(Graph);

		TSharedPtr<FJsonObject> CallableObj = MakeShareable(new FJsonObject);
		CallableObj->SetStringField(TEXT("name"), Graph->GetName());
		CallableObj->SetStringField(TEXT("type"), GraphType);

		// Add state machine specific info
		if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(Graph))
		{
			// Count states and transitions
			int32 StateCount = 0;
			int32 TransitionCount = 0;
			for (UEdGraphNode* Node : SMGraph->Nodes)
			{
				if (Cast<UAnimStateNode>(Node))
				{
					StateCount++;
				}
				else if (Cast<UAnimStateTransitionNode>(Node))
				{
					TransitionCount++;
				}
			}
			CallableObj->SetNumberField(TEXT("state_count"), StateCount);
			CallableObj->SetNumberField(TEXT("transition_count"), TransitionCount);
		}

		OutArray.Add(MakeShareable(new FJsonValueObject(CallableObj)));
	}
}
