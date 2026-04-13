// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindSaveTool.h"
#include "Tools/Rewind/RewindHelper.h"
#include "HAL/FileManager.h"

FString URewindSaveTool::GetToolDescription() const
{
	return TEXT("Save in-memory Rewind Debugger recording data to a .utrace file.");
}

TMap<FString, FBridgeSchemaProperty> URewindSaveTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty File;
	File.Type = TEXT("string");
	File.Description = TEXT("Output file path. Default: auto-generated timestamp in Saved/Traces/");
	File.bRequired = false;
	Schema.Add(TEXT("file"), File);

	return Schema;
}

FBridgeToolResult URewindSaveTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!FRewindHelper::HasData())
	{
		return FBridgeToolResult::Error(TEXT("No recording data in memory."));
	}

	FString FilePath = GetStringArgOrDefault(Arguments, TEXT("file"));
	FString SavedPath = FRewindHelper::SaveTraceFile(FilePath);

	if (SavedPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Failed to save trace file."));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("file"), SavedPath);

	// Duration from rewind debugger
	double Duration = FRewindHelper::GetRecordingEndTime() - FRewindHelper::GetRecordingStartTime();
	Result->SetNumberField(TEXT("duration"), Duration);

	// Get file size
	int64 FileSize = IFileManager::Get().FileSize(*SavedPath);
	Result->SetNumberField(TEXT("size_mb"),
		static_cast<double>(FileSize) / (1024.0 * 1024.0));

	return FBridgeToolResult::Json(Result);
}
