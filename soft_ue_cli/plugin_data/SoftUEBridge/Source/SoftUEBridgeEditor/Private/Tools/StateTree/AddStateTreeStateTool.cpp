// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/StateTree/AddStateTreeStateTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "Editor.h"

FString UAddStateTreeStateTool::GetToolDescription() const
{
	return TEXT("Add a new state to a StateTree asset. "
		"Specify the state name, type, and optionally a parent state to create hierarchical state machines.");
}

TMap<FString, FBridgeSchemaProperty> UAddStateTreeStateTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty StateName;
	StateName.Type = TEXT("string");
	StateName.Description = TEXT("Name of the new state");
	StateName.bRequired = true;
	Schema.Add(TEXT("state_name"), StateName);

	FBridgeSchemaProperty StateType;
	StateType.Type = TEXT("string");
	StateType.Description = TEXT("Type of state: 'State', 'Group', 'Linked', 'Subtree' (default: 'State')");
	StateType.bRequired = false;
	Schema.Add(TEXT("state_type"), StateType);

	FBridgeSchemaProperty ParentState;
	ParentState.Type = TEXT("string");
	ParentState.Description = TEXT("Name of the parent state (optional, creates as root state if not specified)");
	ParentState.bRequired = false;
	Schema.Add(TEXT("parent_state"), ParentState);

	FBridgeSchemaProperty SelectionBehavior;
	SelectionBehavior.Type = TEXT("string");
	SelectionBehavior.Description = TEXT("Selection behavior: 'None', 'TryEnterState', 'TrySelectChildrenInOrder', 'TryFollowTransitions' (default: 'TryEnterState')");
	SelectionBehavior.bRequired = false;
	Schema.Add(TEXT("selection_behavior"), SelectionBehavior);

	return Schema;
}

TArray<FString> UAddStateTreeStateTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_name") };
}

FBridgeToolResult UAddStateTreeStateTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString StateName;
	if (!GetStringArg(Arguments, TEXT("state_name"), StateName))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: state_name"));
	}

	FString StateTypeStr = GetStringArgOrDefault(Arguments, TEXT("state_type"), TEXT("State"));
	FString ParentStateName = GetStringArgOrDefault(Arguments, TEXT("parent_state"), TEXT(""));
	FString SelectionBehaviorStr = GetStringArgOrDefault(Arguments, TEXT("selection_behavior"), TEXT("TryEnterState"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-statetree-state: path='%s', name='%s', type='%s'"),
		*AssetPath, *StateName, *StateTypeStr);

	// Load the StateTree asset
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
	if (!StateTree)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load StateTree: %s"), *AssetPath));
	}

	// Get the editor data
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!EditorData)
	{
		return FBridgeToolResult::Error(TEXT("StateTree has no editor data. Cannot modify."));
	}

	// Parse state type
	EStateTreeStateType StateType = EStateTreeStateType::State;
	if (StateTypeStr.Equals(TEXT("Group"), ESearchCase::IgnoreCase))
	{
		StateType = EStateTreeStateType::Group;
	}
	else if (StateTypeStr.Equals(TEXT("Linked"), ESearchCase::IgnoreCase))
	{
		StateType = EStateTreeStateType::Linked;
	}
	else if (StateTypeStr.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase))
	{
		StateType = EStateTreeStateType::Subtree;
	}

	// Parse selection behavior
	EStateTreeStateSelectionBehavior SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
	if (SelectionBehaviorStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		SelectionBehavior = EStateTreeStateSelectionBehavior::None;
	}
	else if (SelectionBehaviorStr.Equals(TEXT("TrySelectChildrenInOrder"), ESearchCase::IgnoreCase))
	{
		SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	}
	else if (SelectionBehaviorStr.Equals(TEXT("TryFollowTransitions"), ESearchCase::IgnoreCase))
	{
		SelectionBehavior = EStateTreeStateSelectionBehavior::TryFollowTransitions;
	}

	// Begin transaction for undo support
	FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Add StateTree State: %s"), *StateName)));

	StateTree->Modify();
	EditorData->Modify();

	// Find parent state if specified
	UStateTreeState* ParentState = nullptr;
	if (!ParentStateName.IsEmpty())
	{
		// Search for parent in subtrees
		for (UStateTreeState* RootState : EditorData->SubTrees)
		{
			if (RootState && RootState->Name.ToString() == ParentStateName)
			{
				ParentState = RootState;
				break;
			}

			// Search children recursively
			TArray<UStateTreeState*> StatesToCheck;
			StatesToCheck.Add(RootState);

			while (StatesToCheck.Num() > 0)
			{
				UStateTreeState* CurrentState = StatesToCheck.Pop();
				if (!CurrentState) continue;

				for (UStateTreeState* Child : CurrentState->Children)
				{
					if (Child)
					{
						if (Child->Name.ToString() == ParentStateName)
						{
							ParentState = Child;
							break;
						}
						StatesToCheck.Add(Child);
					}
				}

				if (ParentState) break;
			}

			if (ParentState) break;
		}

		if (!ParentState)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Parent state '%s' not found"), *ParentStateName));
		}
	}

	// Create new state
	UStateTreeState* NewState = NewObject<UStateTreeState>(EditorData, NAME_None, RF_Transactional);
	NewState->Name = FName(*StateName);
	NewState->Type = StateType;
	NewState->SelectionBehavior = SelectionBehavior;

	// Add to parent or root
	if (ParentState)
	{
		ParentState->Children.Add(NewState);
		NewState->Parent = ParentState;
	}
	else
	{
		EditorData->SubTrees.Add(NewState);
	}

	// Mark package dirty
	StateTree->MarkPackageDirty();

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("state_type"), StateTypeStr);
	Result->SetStringField(TEXT("selection_behavior"), SelectionBehaviorStr);
	Result->SetBoolField(TEXT("success"), true);

	if (ParentState)
	{
		Result->SetStringField(TEXT("parent_state"), ParentStateName);
	}

	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("State '%s' added to StateTree"), *StateName));

	return FBridgeToolResult::Json(Result);
}
