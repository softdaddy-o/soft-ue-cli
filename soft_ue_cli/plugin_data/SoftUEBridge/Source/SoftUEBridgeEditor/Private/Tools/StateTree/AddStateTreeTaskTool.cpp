// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/StateTree/AddStateTreeTaskTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "StateTreeEditorData.h"
#include "StateTreeTaskBase.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"

FString UAddStateTreeTaskTool::GetToolDescription() const
{
	return TEXT("Add a task to a state in a StateTree asset. "
		"Tasks define the behavior executed when a state is active.");
}

TMap<FString, FBridgeSchemaProperty> UAddStateTreeTaskTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the StateTree (e.g., /Game/AI/ST_EnemyBehavior)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty StateName;
	StateName.Type = TEXT("string");
	StateName.Description = TEXT("Name of the state to add the task to");
	StateName.bRequired = true;
	Schema.Add(TEXT("state_name"), StateName);

	FBridgeSchemaProperty TaskClass;
	TaskClass.Type = TEXT("string");
	TaskClass.Description = TEXT("Class name of the task to add (e.g., 'StateTreeTask_PlayAnimation', 'StateTreeTask_Wait')");
	TaskClass.bRequired = true;
	Schema.Add(TEXT("task_class"), TaskClass);

	FBridgeSchemaProperty TaskName;
	TaskName.Type = TEXT("string");
	TaskName.Description = TEXT("Optional display name for the task instance");
	TaskName.bRequired = false;
	Schema.Add(TEXT("task_name"), TaskName);

	return Schema;
}

TArray<FString> UAddStateTreeTaskTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("state_name"), TEXT("task_class") };
}

FBridgeToolResult UAddStateTreeTaskTool::Execute(
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

	FString TaskClassName;
	if (!GetStringArg(Arguments, TEXT("task_class"), TaskClassName))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: task_class"));
	}

	FString TaskName = GetStringArgOrDefault(Arguments, TEXT("task_name"), TaskClassName);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-statetree-task: path='%s', state='%s', task='%s'"),
		*AssetPath, *StateName, *TaskClassName);

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

	// Find the task struct type
	UScriptStruct* TaskStruct = nullptr;

	// Search for the struct by name
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (Struct->GetName() == TaskClassName || Struct->GetName().Contains(TaskClassName))
		{
			// Check if it's a valid task struct (inherits from FStateTreeTaskBase)
			if (Struct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
			{
				TaskStruct = Struct;
				break;
			}
		}
	}

	if (!TaskStruct)
	{
		// List available task types for helpful error message
		TArray<FString> AvailableTypes;
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			// Check if it's a concrete task struct (not the base class itself)
			if (Struct->IsChildOf(FStateTreeTaskBase::StaticStruct()) &&
				Struct != FStateTreeTaskBase::StaticStruct())
			{
				AvailableTypes.Add(Struct->GetName());
			}
		}

		FString TypeList = FString::Join(AvailableTypes, TEXT(", "));
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Task class '%s' not found. Available types: %s"),
			*TaskClassName, *TypeList));
	}

	// Find the target state
	UStateTreeState* TargetState = nullptr;

	for (UStateTreeState* RootState : EditorData->SubTrees)
	{
		if (RootState && RootState->Name.ToString() == StateName)
		{
			TargetState = RootState;
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
					if (Child->Name.ToString() == StateName)
					{
						TargetState = Child;
						break;
					}
					StatesToCheck.Add(Child);
				}
			}

			if (TargetState) break;
		}

		if (TargetState) break;
	}

	if (!TargetState)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("State '%s' not found in StateTree"), *StateName));
	}

	// Begin transaction for undo support
	FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Add StateTree Task: %s"), *TaskName)));

	StateTree->Modify();
	EditorData->Modify();
	TargetState->Modify();

	// Create and add the task
	FStateTreeEditorNode NewTaskNode;
	NewTaskNode.Node.InitializeAs(TaskStruct);

	if (FStateTreeTaskBase* Task = NewTaskNode.Node.GetMutablePtr<FStateTreeTaskBase>())
	{
		Task->Name = FName(*TaskName);
		Task->bTaskEnabled = true;
	}

	TargetState->Tasks.Add(NewTaskNode);

	// Mark package dirty
	StateTree->MarkPackageDirty();

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("task_class"), TaskStruct->GetName());
	Result->SetStringField(TEXT("task_name"), TaskName);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Task '%s' added to state '%s'"), *TaskName, *StateName));

	return FBridgeToolResult::Json(Result);
}
