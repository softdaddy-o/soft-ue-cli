// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/StateTree/RemoveStateTreeStateTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "Editor.h"

FString URemoveStateTreeStateTool::GetToolDescription() const
{
	return TEXT("Remove a state from a StateTree asset. "
		"Warning: This will also remove all child states and transitions associated with the state.");
}

TMap<FString, FBridgeSchemaProperty> URemoveStateTreeStateTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty StateName;
	StateName.Type = TEXT("string");
	StateName.Description = TEXT("Name of the state to remove");
	StateName.bRequired = true;
	Schema.Add(TEXT("state_name"), StateName);

	return Schema;
}

TArray<FString> URemoveStateTreeStateTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_name") };
}

FBridgeToolResult URemoveStateTreeStateTool::Execute(
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

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("remove-statetree-state: path='%s', state='%s'"), *AssetPath, *StateName);

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

	// Begin transaction for undo support
	FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Remove StateTree State: %s"), *StateName)));

	StateTree->Modify();
	EditorData->Modify();

	// Find and remove the state
	UStateTreeState* StateToRemove = nullptr;
	UStateTreeState* ParentState = nullptr;
	int32 RootIndex = INDEX_NONE;

	// Search in root subtrees
	for (int32 i = 0; i < EditorData->SubTrees.Num(); ++i)
	{
		UStateTreeState* RootState = EditorData->SubTrees[i];
		if (RootState && RootState->Name.ToString() == StateName)
		{
			StateToRemove = RootState;
			RootIndex = i;
			break;
		}

		// Search children recursively
		TArray<TPair<UStateTreeState*, UStateTreeState*>> StatesToCheck; // Pair<State, Parent>
		for (UStateTreeState* Child : RootState->Children)
		{
			if (Child)
			{
				StatesToCheck.Add(TPair<UStateTreeState*, UStateTreeState*>(Child, RootState));
			}
		}

		while (StatesToCheck.Num() > 0)
		{
			auto Current = StatesToCheck.Pop();
			UStateTreeState* CurrentState = Current.Key;
			UStateTreeState* CurrentParent = Current.Value;

			if (CurrentState->Name.ToString() == StateName)
			{
				StateToRemove = CurrentState;
				ParentState = CurrentParent;
				break;
			}

			for (UStateTreeState* Child : CurrentState->Children)
			{
				if (Child)
				{
					StatesToCheck.Add(TPair<UStateTreeState*, UStateTreeState*>(Child, CurrentState));
				}
			}
		}

		if (StateToRemove) break;
	}

	if (!StateToRemove)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("State '%s' not found in StateTree"), *StateName));
	}

	// Count children for reporting
	int32 ChildCount = StateToRemove->Children.Num();

	// Remove from parent or root
	if (ParentState)
	{
		ParentState->Children.Remove(StateToRemove);
	}
	else if (RootIndex != INDEX_NONE)
	{
		EditorData->SubTrees.RemoveAt(RootIndex);
	}

	// Mark package dirty
	StateTree->MarkPackageDirty();

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("children_removed"), ChildCount);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("State '%s' removed from StateTree (including %d children)"), *StateName, ChildCount));

	return FBridgeToolResult::Json(Result);
}
