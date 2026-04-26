// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindStopTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindStopTool::GetToolDescription() const
{
	return TEXT("Stop the current Rewind Debugger recording session.");
}

TMap<FString, FBridgeSchemaProperty> URewindStopTool::GetInputSchema() const
{
	return {};
}

FBridgeToolResult URewindStopTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!FRewindHelper::IsRecording())
	{
		return FBridgeToolResult::Error(TEXT("No active recording."));
	}

	double Duration = FRewindHelper::GetRecordingDuration();
	FRewindHelper::StopRecording();

	TSharedPtr<FJsonObject> Status = FRewindHelper::GetStatus();
	Status->SetStringField(TEXT("status"), TEXT("stopped"));
	Status->SetNumberField(TEXT("duration"), Duration);

	return FBridgeToolResult::Json(Status);
}
