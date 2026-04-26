// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindStatusTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindStatusTool::GetToolDescription() const
{
	return TEXT("Query current Rewind Debugger recording state. Detects recordings from CLI or editor UI.");
}

TMap<FString, FBridgeSchemaProperty> URewindStatusTool::GetInputSchema() const
{
	return {};
}

FBridgeToolResult URewindStatusTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	return FBridgeToolResult::Json(FRewindHelper::GetStatus());
}
