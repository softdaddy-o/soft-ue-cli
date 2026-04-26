// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindListTracksTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindListTracksTool::GetToolDescription() const
{
	return TEXT("List all recorded actors and their available track types from the Rewind Debugger.");
}

TMap<FString, FBridgeSchemaProperty> URewindListTracksTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty ActorTag;
	ActorTag.Type = TEXT("string");
	ActorTag.Description = TEXT("Filter to a specific actor tag. Default: list all actors");
	ActorTag.bRequired = false;
	Schema.Add(TEXT("actor_tag"), ActorTag);

	return Schema;
}

FBridgeToolResult URewindListTracksTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!FRewindHelper::HasData())
	{
		return FBridgeToolResult::Error(
			TEXT("No recording data. Start a recording or load a trace file first. The Animation Insights plugin (GameplayInsights) must be enabled in Edit > Plugins."));
	}

	FString ActorTag = GetStringArgOrDefault(Arguments, TEXT("actor_tag"));
	TSharedPtr<FJsonObject> Result = FRewindHelper::CollectTrackList(ActorTag);

	if (!Result.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Failed to access Rewind Debugger. Ensure the Animation Insights (GameplayInsights) plugin is enabled in Edit > Plugins."));
	}

	return FBridgeToolResult::Json(Result);
}
