// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/StateTree/QueryStateTreeTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeTypes.h"
#include "StructUtils/InstancedStructContainer.h"

FString UQueryStateTreeTool::GetToolDescription() const
{
	return TEXT("Query StateTree structure: states, transitions, tasks, evaluators, and parameters. "
		"Use 'include' parameter to select sections: 'states', 'transitions', 'tasks', 'evaluators', 'parameters', 'all'.");
}

TMap<FString, FBridgeSchemaProperty> UQueryStateTreeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("string");
	Include.Description = TEXT("Sections to include: 'states', 'transitions', 'tasks', 'evaluators', 'parameters', or 'all' (default: 'all')");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	FBridgeSchemaProperty Detailed;
	Detailed.Type = TEXT("boolean");
	Detailed.Description = TEXT("Include detailed info (default: true)");
	Detailed.bRequired = false;
	Schema.Add(TEXT("detailed"), Detailed);

	return Schema;
}

TArray<FString> UQueryStateTreeTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryStateTreeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString Include = GetStringArgOrDefault(Arguments, TEXT("include"), TEXT("all")).ToLower();
	bool bDetailed = GetBoolArgOrDefault(Arguments, TEXT("detailed"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-statetree: path='%s', include='%s'"), *AssetPath, *Include);

	// Load the StateTree asset
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
	if (!StateTree)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("query-statetree: Failed to load StateTree at '%s'"), *AssetPath);
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load StateTree: %s"), *AssetPath));
	}

	// Build result JSON
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("name"), StateTree->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	// Get StateTree schema info
	if (StateTree->GetSchema())
	{
		Result->SetStringField(TEXT("schema"), StateTree->GetSchema()->GetName());
	}

	// Add requested sections
	bool bAll = Include == TEXT("all");

	if (bAll || Include == TEXT("states"))
	{
		Result->SetObjectField(TEXT("states"), ExtractStates(StateTree, bDetailed));
	}

	if (bAll || Include == TEXT("transitions"))
	{
		Result->SetObjectField(TEXT("transitions"), ExtractTransitions(StateTree));
	}

	if (bAll || Include == TEXT("tasks"))
	{
		Result->SetObjectField(TEXT("tasks"), ExtractTasks(StateTree));
	}

	if (bAll || Include == TEXT("evaluators"))
	{
		Result->SetObjectField(TEXT("evaluators"), ExtractEvaluators(StateTree));
	}

	if (bAll || Include == TEXT("parameters"))
	{
		Result->SetObjectField(TEXT("parameters"), ExtractParameters(StateTree));
	}

	return FBridgeToolResult::Json(Result);
}

TSharedPtr<FJsonObject> UQueryStateTreeTool::ExtractStates(UStateTree* StateTree, bool bDetailed) const
{
	TSharedPtr<FJsonObject> StatesObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> StateArray;

	TConstArrayView<FCompactStateTreeState> States = StateTree->GetStates();

	for (int32 i = 0; i < States.Num(); ++i)
	{
		const FCompactStateTreeState& State = States[i];

		TSharedPtr<FJsonObject> StateObj = MakeShareable(new FJsonObject);
		StateObj->SetStringField(TEXT("name"), State.Name.ToString());
		StateObj->SetNumberField(TEXT("index"), i);
		StateObj->SetStringField(TEXT("type"), GetStateTypeString(static_cast<uint8>(State.Type)));

		if (bDetailed)
		{
			StateObj->SetStringField(TEXT("selection_behavior"), GetSelectionBehaviorString(static_cast<uint8>(State.SelectionBehavior)));
			StateObj->SetNumberField(TEXT("depth"), State.Depth);

			// Parent info
			if (State.Parent.IsValid())
			{
				StateObj->SetNumberField(TEXT("parent_index"), State.Parent.Index);
			}

			// Children info - ChildrenEnd exists in UE 5.6
			if (State.HasChildren())
			{
				StateObj->SetNumberField(TEXT("children_begin"), State.ChildrenBegin);
				StateObj->SetNumberField(TEXT("children_end"), State.ChildrenEnd);
				StateObj->SetNumberField(TEXT("children_count"), State.ChildrenEnd - State.ChildrenBegin);
			}

			// Task info - TasksBegin only (no TasksEnd in UE 5.6)
			StateObj->SetNumberField(TEXT("tasks_begin"), State.TasksBegin);

			// Transition info - TransitionsBegin only (no TransitionsEnd in UE 5.6)
			StateObj->SetNumberField(TEXT("transitions_begin"), State.TransitionsBegin);

			// Enabled states
			StateObj->SetBoolField(TEXT("enabled"), State.bEnabled);
		}

		StateArray.Add(MakeShareable(new FJsonValueObject(StateObj)));
	}

	StatesObj->SetArrayField(TEXT("items"), StateArray);
	StatesObj->SetNumberField(TEXT("count"), StateArray.Num());

	return StatesObj;
}

TSharedPtr<FJsonObject> UQueryStateTreeTool::ExtractTransitions(UStateTree* StateTree) const
{
	TSharedPtr<FJsonObject> TransitionsObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> TransitionArray;

	// UE 5.6: Iterate through states and extract transitions using GetTransitionFromIndex
	TConstArrayView<FCompactStateTreeState> States = StateTree->GetStates();
	int32 TransitionIndex = 0;

	for (const FCompactStateTreeState& State : States)
	{
		// Get transitions for this state starting from TransitionsBegin
		FStateTreeIndex16 TransIdx = FStateTreeIndex16(State.TransitionsBegin);
		while (const FCompactStateTransition* Transition = StateTree->GetTransitionFromIndex(TransIdx))
		{
			TSharedPtr<FJsonObject> TransObj = MakeShareable(new FJsonObject);
			TransObj->SetNumberField(TEXT("index"), TransitionIndex++);
			TransObj->SetStringField(TEXT("state_name"), State.Name.ToString());

			// Trigger type
			FString TriggerStr;
			switch (Transition->Trigger)
			{
			case EStateTreeTransitionTrigger::OnStateCompleted: TriggerStr = TEXT("OnStateCompleted"); break;
			case EStateTreeTransitionTrigger::OnStateFailed: TriggerStr = TEXT("OnStateFailed"); break;
			case EStateTreeTransitionTrigger::OnTick: TriggerStr = TEXT("OnTick"); break;
			case EStateTreeTransitionTrigger::OnEvent: TriggerStr = TEXT("OnEvent"); break;
			default: TriggerStr = TEXT("Unknown");
			}
			TransObj->SetStringField(TEXT("trigger"), TriggerStr);

			// Target state (UE 5.6: FCompactStateTransition::State is FStateTreeStateHandle directly)
			if (Transition->State.IsValid())
			{
				TransObj->SetNumberField(TEXT("target_state_index"), Transition->State.Index);
			}

			// Priority
			TransObj->SetNumberField(TEXT("priority"), static_cast<int32>(Transition->Priority));

			// Conditions count (UE 5.6 uses ConditionsNum)
			if (Transition->ConditionsNum > 0)
			{
				TransObj->SetNumberField(TEXT("conditions_count"), Transition->ConditionsNum);
			}

			TransitionArray.Add(MakeShareable(new FJsonValueObject(TransObj)));

			// Move to next transition - break if we've gone past this state's transitions
			TransIdx = FStateTreeIndex16(TransIdx.Get() + 1);
			// Check if still valid
			if (!StateTree->GetTransitionFromIndex(TransIdx))
			{
				break;
			}
		}
	}

	TransitionsObj->SetArrayField(TEXT("items"), TransitionArray);
	TransitionsObj->SetNumberField(TEXT("count"), TransitionArray.Num());

	return TransitionsObj;
}

TSharedPtr<FJsonObject> UQueryStateTreeTool::ExtractTasks(UStateTree* StateTree) const
{
	TSharedPtr<FJsonObject> TasksObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> TaskArray;

	// Get task instances from the StateTree (UE 5.6: FInstancedStructContainer)
	const FInstancedStructContainer& Nodes = StateTree->GetNodes();

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		FConstStructView Node = Nodes[i];

		// UE 5.6: FConstStructView::GetPtr requires const template argument
		if (const FStateTreeTaskBase* Task = Node.GetPtr<const FStateTreeTaskBase>())
		{
			TSharedPtr<FJsonObject> TaskObj = MakeShareable(new FJsonObject);
			TaskObj->SetNumberField(TEXT("index"), i);
			TaskObj->SetStringField(TEXT("name"), Task->Name.ToString());

			if (const UScriptStruct* Struct = Node.GetScriptStruct())
			{
				TaskObj->SetStringField(TEXT("type"), Struct->GetName());
			}

			TaskObj->SetBoolField(TEXT("enabled"), Task->bTaskEnabled);

			TaskArray.Add(MakeShareable(new FJsonValueObject(TaskObj)));
		}
	}

	TasksObj->SetArrayField(TEXT("items"), TaskArray);
	TasksObj->SetNumberField(TEXT("count"), TaskArray.Num());

	return TasksObj;
}

TSharedPtr<FJsonObject> UQueryStateTreeTool::ExtractEvaluators(UStateTree* StateTree) const
{
	TSharedPtr<FJsonObject> EvaluatorsObj = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> EvaluatorArray;

	// UE 5.6: FInstancedStructContainer
	const FInstancedStructContainer& Nodes = StateTree->GetNodes();

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		FConstStructView Node = Nodes[i];

		// UE 5.6: FConstStructView::GetPtr requires const template argument
		if (const FStateTreeEvaluatorBase* Evaluator = Node.GetPtr<const FStateTreeEvaluatorBase>())
		{
			TSharedPtr<FJsonObject> EvalObj = MakeShareable(new FJsonObject);
			EvalObj->SetNumberField(TEXT("index"), i);
			EvalObj->SetStringField(TEXT("name"), Evaluator->Name.ToString());

			if (const UScriptStruct* Struct = Node.GetScriptStruct())
			{
				EvalObj->SetStringField(TEXT("type"), Struct->GetName());
			}

			EvaluatorArray.Add(MakeShareable(new FJsonValueObject(EvalObj)));
		}
	}

	EvaluatorsObj->SetArrayField(TEXT("items"), EvaluatorArray);
	EvaluatorsObj->SetNumberField(TEXT("count"), EvaluatorArray.Num());

	return EvaluatorsObj;
}

TSharedPtr<FJsonObject> UQueryStateTreeTool::ExtractParameters(UStateTree* StateTree) const
{
	TSharedPtr<FJsonObject> ParamsObj = MakeShareable(new FJsonObject);

	// Extract default parameters from the StateTree
	const FInstancedPropertyBag& DefaultParameters = StateTree->GetDefaultParameters();

	TArray<TSharedPtr<FJsonValue>> ParamArray;

	if (const UPropertyBag* PropertyBag = DefaultParameters.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Desc : PropertyBag->GetPropertyDescs())
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
			ParamObj->SetStringField(TEXT("name"), Desc.Name.ToString());

			// Property type
			FString TypeStr;
			switch (Desc.ValueType)
			{
			case EPropertyBagPropertyType::Bool: TypeStr = TEXT("bool"); break;
			case EPropertyBagPropertyType::Byte: TypeStr = TEXT("byte"); break;
			case EPropertyBagPropertyType::Int32: TypeStr = TEXT("int32"); break;
			case EPropertyBagPropertyType::Int64: TypeStr = TEXT("int64"); break;
			case EPropertyBagPropertyType::Float: TypeStr = TEXT("float"); break;
			case EPropertyBagPropertyType::Double: TypeStr = TEXT("double"); break;
			case EPropertyBagPropertyType::Name: TypeStr = TEXT("FName"); break;
			case EPropertyBagPropertyType::String: TypeStr = TEXT("FString"); break;
			case EPropertyBagPropertyType::Text: TypeStr = TEXT("FText"); break;
			case EPropertyBagPropertyType::Struct: TypeStr = TEXT("struct"); break;
			case EPropertyBagPropertyType::Object: TypeStr = TEXT("object"); break;
			case EPropertyBagPropertyType::Class: TypeStr = TEXT("class"); break;
			case EPropertyBagPropertyType::Enum: TypeStr = TEXT("enum"); break;
			default: TypeStr = TEXT("unknown");
			}
			ParamObj->SetStringField(TEXT("type"), TypeStr);

			ParamArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
		}
	}

	ParamsObj->SetArrayField(TEXT("items"), ParamArray);
	ParamsObj->SetNumberField(TEXT("count"), ParamArray.Num());

	return ParamsObj;
}

FString UQueryStateTreeTool::GetStateTypeString(uint8 StateType) const
{
	switch (static_cast<EStateTreeStateType>(StateType))
	{
	case EStateTreeStateType::State: return TEXT("State");
	case EStateTreeStateType::Group: return TEXT("Group");
	case EStateTreeStateType::Linked: return TEXT("Linked");
	case EStateTreeStateType::LinkedAsset: return TEXT("LinkedAsset");
	case EStateTreeStateType::Subtree: return TEXT("Subtree");
	default: return TEXT("Unknown");
	}
}

FString UQueryStateTreeTool::GetSelectionBehaviorString(uint8 SelectionBehavior) const
{
	switch (static_cast<EStateTreeStateSelectionBehavior>(SelectionBehavior))
	{
	case EStateTreeStateSelectionBehavior::None: return TEXT("None");
	case EStateTreeStateSelectionBehavior::TryEnterState: return TEXT("TryEnterState");
	case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder: return TEXT("TrySelectChildrenInOrder");
	case EStateTreeStateSelectionBehavior::TryFollowTransitions: return TEXT("TryFollowTransitions");
	default: return TEXT("Unknown");
	}
}
