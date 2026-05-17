// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AddAnimStateMachineTool.h"
#include "Tools/Animation/AddAnimStateTool.h"
#include "Tools/Animation/AddAnimTransitionTool.h"

#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"

#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty MakeSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

FBridgeSchemaProperty MakePositionProperty(const FString& Description)
{
	FBridgeSchemaProperty Property = MakeSchemaProperty(TEXT("array"), Description);
	Property.ItemsType = TEXT("number");
	return Property;
}

FVector2f ReadPosition(const TSharedPtr<FJsonObject>& Arguments, const FVector2f& DefaultPosition)
{
	const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray && PositionArray->Num() >= 2)
	{
		return FVector2f(
			static_cast<float>((*PositionArray)[0]->AsNumber()),
			static_cast<float>((*PositionArray)[1]->AsNumber()));
	}
	return DefaultPosition;
}

UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath, FString& OutError)
{
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, OutError);
	if (!Object)
	{
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Object);
	if (!AnimBlueprint)
	{
		OutError = FString::Printf(TEXT("Asset is not an AnimBlueprint: %s"), *AssetPath);
		return nullptr;
	}

	return AnimBlueprint;
}

UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName, FString& OutError)
{
	UEdGraph* Graph = FBridgeAssetModifier::FindGraphByName(AnimBlueprint, StateMachineName);
	UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Graph);
	if (!StateMachineGraph)
	{
		OutError = FString::Printf(TEXT("AnimBlueprint state machine graph not found: %s"), *StateMachineName);
		return nullptr;
	}
	return StateMachineGraph;
}

UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* StateMachineGraph, const FString& StateName)
{
	if (!StateMachineGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (StateNode && StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
		{
			return StateNode;
		}
	}
	return nullptr;
}

bool ConnectEntryToState(UAnimationStateMachineGraph* StateMachineGraph, UAnimStateNode* StateNode)
{
	if (!StateMachineGraph || !StateMachineGraph->EntryNode || !StateNode)
	{
		return false;
	}

	UEdGraphPin* EntryPin = StateMachineGraph->EntryNode->Pins.Num() > 0 ? StateMachineGraph->EntryNode->Pins[0] : nullptr;
	UEdGraphPin* StateInputPin = StateNode->GetInputPin();
	if (!EntryPin || !StateInputPin)
	{
		return false;
	}

	EntryPin->Modify();
	StateInputPin->Modify();
	EntryPin->BreakAllPinLinks();
	return StateMachineGraph->GetSchema()->TryCreateConnection(EntryPin, StateInputPin);
}

UAnimStateNode* CreateAnimState(
	UAnimBlueprint* AnimBlueprint,
	UAnimationStateMachineGraph* StateMachineGraph,
	const FString& StateName,
	const FVector2f& Position,
	bool bMakeEntry,
	FString& OutError)
{
	if (FindStateNode(StateMachineGraph, StateName))
	{
		OutError = FString::Printf(TEXT("State already exists in '%s': %s"), *StateMachineGraph->GetName(), *StateName);
		return nullptr;
	}
	if (FBridgeAssetModifier::FindGraphByName(AnimBlueprint, StateName))
	{
		OutError = FString::Printf(TEXT("A graph named '%s' already exists"), *StateName);
		return nullptr;
	}

	UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
		StateMachineGraph,
		NewObject<UAnimStateNode>(),
		Position,
		false);

	if (!StateNode || !StateNode->BoundGraph)
	{
		OutError = FString::Printf(TEXT("Failed to create AnimBlueprint state: %s"), *StateName);
		return nullptr;
	}

	FBlueprintEditorUtils::RenameGraph(StateNode->BoundGraph, StateName);

	if (bMakeEntry && !ConnectEntryToState(StateMachineGraph, StateNode))
	{
		OutError = FString::Printf(TEXT("Failed to connect entry node to state: %s"), *StateName);
		return nullptr;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FBridgeAssetModifier::MarkPackageDirty(AnimBlueprint);
	return StateNode;
}

TSharedPtr<FJsonObject> StateNodeToJson(UAnimStateNode* StateNode)
{
	TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);
	StateJson->SetStringField(TEXT("state_name"), StateNode ? StateNode->GetStateName() : TEXT(""));
	if (StateNode)
	{
		StateJson->SetStringField(TEXT("node_guid"), StateNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		if (StateNode->BoundGraph)
		{
			StateJson->SetStringField(TEXT("state_graph"), StateNode->BoundGraph->GetName());
		}
	}
	return StateJson;
}

void SetLiteralTransitionRule(UAnimStateTransitionNode* TransitionNode, bool bRule)
{
	UAnimationTransitionGraph* TransitionGraph = TransitionNode ? Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph) : nullptr;
	UAnimGraphNode_TransitionResult* ResultNode = TransitionGraph ? TransitionGraph->GetResultNode() : nullptr;
	if (!ResultNode)
	{
		return;
	}

	ResultNode->Modify();
	ResultNode->Node.bCanEnterTransition = bRule;
	if (UEdGraphPin* RulePin = ResultNode->FindPin(TEXT("bCanEnterTransition")))
	{
		RulePin->DefaultValue = bRule ? TEXT("true") : TEXT("false");
	}
}
}

FString UAddAnimStateMachineTool::GetToolDescription() const
{
	return TEXT("Create an AnimBlueprint state machine node, initialize its inner graph, and optionally create a connected default state.");
}

TMap<FString, FBridgeSchemaProperty> UAddAnimStateMachineTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), MakeSchemaProperty(TEXT("string"), TEXT("AnimBlueprint asset path"), true));
	Schema.Add(TEXT("state_machine_name"), MakeSchemaProperty(TEXT("string"), TEXT("Name for the new state machine graph"), true));
	Schema.Add(TEXT("graph_name"), MakeSchemaProperty(TEXT("string"), TEXT("Parent animation graph name (default: AnimGraph)")));
	Schema.Add(TEXT("default_state"), MakeSchemaProperty(TEXT("string"), TEXT("Optional initial state to create and connect from Entry")));
	Schema.Add(TEXT("position"), MakePositionProperty(TEXT("State machine node position as [x, y]")));
	return Schema;
}

TArray<FString> UAddAnimStateMachineTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_machine_name") };
}

FBridgeToolResult UAddAnimStateMachineTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	FString StateMachineName;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath) ||
		!GetStringArg(Arguments, TEXT("state_machine_name"), StateMachineName))
	{
		return FBridgeToolResult::Error(TEXT("asset_path and state_machine_name are required"));
	}

	FString Error;
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AssetPath, Error);
	if (!AnimBlueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	if (FBridgeAssetModifier::FindGraphByName(AnimBlueprint, StateMachineName))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("A graph named '%s' already exists"), *StateMachineName));
	}

	const FString ParentGraphName = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT("AnimGraph"));
	const FString DefaultState = GetStringArgOrDefault(Arguments, TEXT("default_state"));
	if (!DefaultState.IsEmpty() && FBridgeAssetModifier::FindGraphByName(AnimBlueprint, DefaultState))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("A graph named '%s' already exists"), *DefaultState));
	}

	UEdGraph* ParentGraph = FBridgeAssetModifier::FindGraphByName(AnimBlueprint, ParentGraphName);
	if (!ParentGraph)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Parent animation graph not found: %s"), *ParentGraphName));
	}

	const FVector2f Position = ReadPosition(Arguments, FVector2f(0.0f, 0.0f));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-anim-state-machine: asset='%s', graph='%s', name='%s'"),
		*AssetPath, *ParentGraphName, *StateMachineName);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(FString::Printf(TEXT("Add Anim State Machine: %s"), *StateMachineName)));

	AnimBlueprint->Modify();
	ParentGraph->Modify();

	UAnimGraphNode_StateMachine* StateMachineNode = NewObject<UAnimGraphNode_StateMachine>(
		ParentGraph,
		UAnimGraphNode_StateMachine::StaticClass(),
		NAME_None,
		RF_Transactional);
	if (!StateMachineNode)
	{
		return FBridgeToolResult::Error(TEXT("Failed to create AnimGraphNode_StateMachine"));
	}

	ParentGraph->AddNode(StateMachineNode, true, false);
	StateMachineNode->CreateNewGuid();
	StateMachineNode->NodePosX = static_cast<int32>(Position.X);
	StateMachineNode->NodePosY = static_cast<int32>(Position.Y);
	StateMachineNode->PostPlacedNewNode();
	StateMachineNode->AllocateDefaultPins();

	UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		return FBridgeToolResult::Error(TEXT("State machine node was created without an inner graph"));
	}

	FBlueprintEditorUtils::RenameGraph(StateMachineGraph, StateMachineName);

	UAnimStateNode* DefaultStateNode = nullptr;
	if (!DefaultState.IsEmpty())
	{
		DefaultStateNode = CreateAnimState(
			AnimBlueprint,
			StateMachineGraph,
			DefaultState,
			FVector2f(Position.X + 300.0f, Position.Y),
			true,
			Error);
		if (!DefaultStateNode)
		{
			return FBridgeToolResult::Error(Error);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FBridgeAssetModifier::MarkPackageDirty(AnimBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("graph"), ParentGraphName);
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("state_machine_graph"), StateMachineGraph->GetName());
	Result->SetStringField(TEXT("node_guid"), StateMachineNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	if (DefaultStateNode)
	{
		Result->SetObjectField(TEXT("default_state"), StateNodeToJson(DefaultStateNode));
	}
	return FBridgeToolResult::Json(Result);
}

FString UAddAnimStateTool::GetToolDescription() const
{
	return TEXT("Create an AnimStateNode inside an existing AnimBlueprint state-machine graph.");
}

TMap<FString, FBridgeSchemaProperty> UAddAnimStateTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), MakeSchemaProperty(TEXT("string"), TEXT("AnimBlueprint asset path"), true));
	Schema.Add(TEXT("state_machine_name"), MakeSchemaProperty(TEXT("string"), TEXT("State machine graph name"), true));
	Schema.Add(TEXT("state_name"), MakeSchemaProperty(TEXT("string"), TEXT("State name"), true));
	Schema.Add(TEXT("entry"), MakeSchemaProperty(TEXT("boolean"), TEXT("Connect the state-machine entry node to this state")));
	Schema.Add(TEXT("position"), MakePositionProperty(TEXT("State node position as [x, y]")));
	return Schema;
}

TArray<FString> UAddAnimStateTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_machine_name"), TEXT("state_name") };
}

FBridgeToolResult UAddAnimStateTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	FString StateMachineName;
	FString StateName;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath) ||
		!GetStringArg(Arguments, TEXT("state_machine_name"), StateMachineName) ||
		!GetStringArg(Arguments, TEXT("state_name"), StateName))
	{
		return FBridgeToolResult::Error(TEXT("asset_path, state_machine_name, and state_name are required"));
	}

	FString Error;
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AssetPath, Error);
	if (!AnimBlueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimationStateMachineGraph* StateMachineGraph = FindStateMachineGraph(AnimBlueprint, StateMachineName, Error);
	if (!StateMachineGraph)
	{
		return FBridgeToolResult::Error(Error);
	}

	const FVector2f Position = ReadPosition(Arguments, FVector2f(300.0f, 0.0f));
	const bool bEntry = GetBoolArgOrDefault(Arguments, TEXT("entry"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-anim-state: asset='%s', machine='%s', state='%s'"),
		*AssetPath, *StateMachineName, *StateName);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(FString::Printf(TEXT("Add Anim State: %s"), *StateName)));

	AnimBlueprint->Modify();
	StateMachineGraph->Modify();

	UAnimStateNode* StateNode = CreateAnimState(AnimBlueprint, StateMachineGraph, StateName, Position, bEntry, Error);
	if (!StateNode)
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = StateNodeToJson(StateNode);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetBoolField(TEXT("entry"), bEntry);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	return FBridgeToolResult::Json(Result);
}

FString UAddAnimTransitionTool::GetToolDescription() const
{
	return TEXT("Create an AnimStateTransitionNode between two states in an AnimBlueprint state machine.");
}

TMap<FString, FBridgeSchemaProperty> UAddAnimTransitionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), MakeSchemaProperty(TEXT("string"), TEXT("AnimBlueprint asset path"), true));
	Schema.Add(TEXT("state_machine_name"), MakeSchemaProperty(TEXT("string"), TEXT("State machine graph name"), true));
	Schema.Add(TEXT("source_state"), MakeSchemaProperty(TEXT("string"), TEXT("Source state name"), true));
	Schema.Add(TEXT("target_state"), MakeSchemaProperty(TEXT("string"), TEXT("Target state name"), true));
	Schema.Add(TEXT("crossfade_duration"), MakeSchemaProperty(TEXT("number"), TEXT("Crossfade duration in seconds")));
	Schema.Add(TEXT("priority"), MakeSchemaProperty(TEXT("integer"), TEXT("Transition priority order")));
	Schema.Add(TEXT("bidirectional"), MakeSchemaProperty(TEXT("boolean"), TEXT("Mark transition as bidirectional")));
	Schema.Add(TEXT("rule"), MakeSchemaProperty(TEXT("boolean"), TEXT("Optional literal transition rule default")));
	return Schema;
}

TArray<FString> UAddAnimTransitionTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_machine_name"), TEXT("source_state"), TEXT("target_state") };
}

FBridgeToolResult UAddAnimTransitionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	FString StateMachineName;
	FString SourceStateName;
	FString TargetStateName;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath) ||
		!GetStringArg(Arguments, TEXT("state_machine_name"), StateMachineName) ||
		!GetStringArg(Arguments, TEXT("source_state"), SourceStateName) ||
		!GetStringArg(Arguments, TEXT("target_state"), TargetStateName))
	{
		return FBridgeToolResult::Error(TEXT("asset_path, state_machine_name, source_state, and target_state are required"));
	}

	FString Error;
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AssetPath, Error);
	if (!AnimBlueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimationStateMachineGraph* StateMachineGraph = FindStateMachineGraph(AnimBlueprint, StateMachineName, Error);
	if (!StateMachineGraph)
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimStateNode* SourceState = FindStateNode(StateMachineGraph, SourceStateName);
	UAnimStateNode* TargetState = FindStateNode(StateMachineGraph, TargetStateName);
	if (!SourceState)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source state not found: %s"), *SourceStateName));
	}
	if (!TargetState)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target state not found: %s"), *TargetStateName));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-anim-transition: asset='%s', machine='%s', %s -> %s"),
		*AssetPath, *StateMachineName, *SourceStateName, *TargetStateName);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(FString::Printf(TEXT("Add Anim Transition: %s -> %s"), *SourceStateName, *TargetStateName)));

	AnimBlueprint->Modify();
	StateMachineGraph->Modify();

	const FVector2f Position(
		(static_cast<float>(SourceState->NodePosX) + static_cast<float>(TargetState->NodePosX)) * 0.5f,
		(static_cast<float>(SourceState->NodePosY) + static_cast<float>(TargetState->NodePosY)) * 0.5f);

	UAnimStateTransitionNode* TransitionNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(
		StateMachineGraph,
		NewObject<UAnimStateTransitionNode>(),
		Position,
		false);
	if (!TransitionNode || !TransitionNode->BoundGraph)
	{
		return FBridgeToolResult::Error(TEXT("Failed to create AnimBlueprint transition"));
	}

	TransitionNode->CreateConnections(SourceState, TargetState);
	FEdGraphUtilities::RenameGraphToNameOrCloseToName(
		TransitionNode->BoundGraph,
		FString::Printf(TEXT("%s_to_%s"), *SourceStateName, *TargetStateName));

	float CrossfadeDuration = 0.0f;
	if (GetFloatArg(Arguments, TEXT("crossfade_duration"), CrossfadeDuration))
	{
		TransitionNode->CrossfadeDuration = CrossfadeDuration;
	}
	int32 Priority = 0;
	if (GetIntArg(Arguments, TEXT("priority"), Priority))
	{
		TransitionNode->PriorityOrder = Priority;
	}
	bool bBidirectional = false;
	if (GetBoolArg(Arguments, TEXT("bidirectional"), bBidirectional))
	{
		TransitionNode->Bidirectional = bBidirectional;
	}
	bool bRule = false;
	if (GetBoolArg(Arguments, TEXT("rule"), bRule))
	{
		SetLiteralTransitionRule(TransitionNode, bRule);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FBridgeAssetModifier::MarkPackageDirty(AnimBlueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("source_state"), SourceStateName);
	Result->SetStringField(TEXT("target_state"), TargetStateName);
	Result->SetStringField(TEXT("node_guid"), TransitionNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("transition_graph"), TransitionNode->BoundGraph->GetName());
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	return FBridgeToolResult::Json(Result);
}
