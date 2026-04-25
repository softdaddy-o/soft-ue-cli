// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/StateTree/AddStateTreeTransitionTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "StateTreeTypes.h"
#include "Editor.h"

FString UAddStateTreeTransitionTool::GetToolDescription() const
{
	return TEXT("Add a transition between states in a StateTree asset. "
		"Transitions define how the state machine moves from one state to another.");
}

TMap<FString, FBridgeSchemaProperty> UAddStateTreeTransitionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty SourceState;
	SourceState.Type = TEXT("string");
	SourceState.Description = TEXT("Name of the source state (where the transition starts)");
	SourceState.bRequired = true;
	Schema.Add(TEXT("source_state"), SourceState);

	FBridgeSchemaProperty TargetState;
	TargetState.Type = TEXT("string");
	TargetState.Description = TEXT("Name of the target state (where the transition goes). Use 'Succeeded', 'Failed', 'Next', or a state name.");
	TargetState.bRequired = true;
	Schema.Add(TEXT("target_state"), TargetState);

	FBridgeSchemaProperty Trigger;
	Trigger.Type = TEXT("string");
	Trigger.Description = TEXT("Transition trigger: 'OnStateCompleted', 'OnStateFailed', 'OnTick', 'OnEvent' (default: 'OnStateCompleted')");
	Trigger.bRequired = false;
	Schema.Add(TEXT("trigger"), Trigger);

	FBridgeSchemaProperty Priority;
	Priority.Type = TEXT("string");
	Priority.Description = TEXT("Transition priority: 'Normal', 'Low', 'High', 'Critical' (default: 'Normal')");
	Priority.bRequired = false;
	Schema.Add(TEXT("priority"), Priority);

	return Schema;
}

TArray<FString> UAddStateTreeTransitionTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("source_state"), TEXT("target_state") };
}

FBridgeToolResult UAddStateTreeTransitionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString SourceStateName;
	if (!GetStringArg(Arguments, TEXT("source_state"), SourceStateName))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: source_state"));
	}

	FString TargetStateName;
	if (!GetStringArg(Arguments, TEXT("target_state"), TargetStateName))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: target_state"));
	}

	FString TriggerStr = GetStringArgOrDefault(Arguments, TEXT("trigger"), TEXT("OnStateCompleted"));
	FString PriorityStr = GetStringArgOrDefault(Arguments, TEXT("priority"), TEXT("Normal"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-statetree-transition: path='%s', source='%s', target='%s'"),
		*AssetPath, *SourceStateName, *TargetStateName);

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

	// Helper lambda to find a state by name
	auto FindState = [&EditorData](const FString& Name) -> UStateTreeState*
	{
		for (UStateTreeState* RootState : EditorData->SubTrees)
		{
			if (RootState && RootState->Name.ToString() == Name)
			{
				return RootState;
			}

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
						if (Child->Name.ToString() == Name)
						{
							return Child;
						}
						StatesToCheck.Add(Child);
					}
				}
			}
		}
		return nullptr;
	};

	// Find source state
	UStateTreeState* SourceState = FindState(SourceStateName);
	if (!SourceState)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source state '%s' not found"), *SourceStateName));
	}

	// Parse trigger type
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	if (TriggerStr.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase))
	{
		Trigger = EStateTreeTransitionTrigger::OnStateFailed;
	}
	else if (TriggerStr.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase))
	{
		Trigger = EStateTreeTransitionTrigger::OnTick;
	}
	else if (TriggerStr.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase))
	{
		Trigger = EStateTreeTransitionTrigger::OnEvent;
	}

	// Parse priority
	EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;
	if (PriorityStr.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
	{
		Priority = EStateTreeTransitionPriority::Low;
	}
	else if (PriorityStr.Equals(TEXT("High"), ESearchCase::IgnoreCase))
	{
		Priority = EStateTreeTransitionPriority::High;
	}
	else if (PriorityStr.Equals(TEXT("Critical"), ESearchCase::IgnoreCase))
	{
		Priority = EStateTreeTransitionPriority::Critical;
	}

	// Begin transaction for undo support
	FScopedTransaction Transaction(FText::FromString(FString::Printf(
		TEXT("Add StateTree Transition: %s -> %s"), *SourceStateName, *TargetStateName)));

	StateTree->Modify();
	EditorData->Modify();
	SourceState->Modify();

	// Create the transition
	FStateTreeTransition NewTransition;
	NewTransition.Trigger = Trigger;
	NewTransition.Priority = Priority;

	// Set target state - handle special cases
	// UE 5.6: Use constructor with EStateTreeTransitionType (editor only)
	if (TargetStateName.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
	{
		NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::Succeeded);
	}
	else if (TargetStateName.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
	{
		NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::Failed);
	}
	else if (TargetStateName.Equals(TEXT("Next"), ESearchCase::IgnoreCase))
	{
		NewTransition.State = FStateTreeStateLink(EStateTreeTransitionType::NextSelectableState);
	}
	else
	{
		// Find target state
		UStateTreeState* TargetState = FindState(TargetStateName);
		if (!TargetState)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Target state '%s' not found. Use 'Succeeded', 'Failed', 'Next', or a valid state name."),
				*TargetStateName));
		}
		// UE 5.6: Set ID and LinkType directly instead of using constructor
		NewTransition.State.ID = TargetState->ID;
		NewTransition.State.LinkType = EStateTreeTransitionType::GotoState;
		NewTransition.State.Name = TargetState->Name;
	}

	// Add transition to source state
	SourceState->Transitions.Add(NewTransition);

	// Mark package dirty
	StateTree->MarkPackageDirty();

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("source_state"), SourceStateName);
	Result->SetStringField(TEXT("target_state"), TargetStateName);
	Result->SetStringField(TEXT("trigger"), TriggerStr);
	Result->SetStringField(TEXT("priority"), PriorityStr);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Transition added: %s -> %s (trigger: %s)"), *SourceStateName, *TargetStateName, *TriggerStr));

	return FBridgeToolResult::Json(Result);
}
