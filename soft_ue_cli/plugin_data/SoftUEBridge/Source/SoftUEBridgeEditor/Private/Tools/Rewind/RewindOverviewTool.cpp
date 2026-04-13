// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindOverviewTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindOverviewTool::GetToolDescription() const
{
	return TEXT("Track-level summary for an actor from the Rewind Debugger. "
		"Shows state machine transitions, montage play ranges, and notify fire times. "
		"Use this to decide where to drill down with rewind-snapshot.");
}

TMap<FString, FBridgeSchemaProperty> URewindOverviewTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty ActorTag;
	ActorTag.Type = TEXT("string");
	ActorTag.Description = TEXT("Target actor tag");
	ActorTag.bRequired = true;
	Schema.Add(TEXT("actor_tag"), ActorTag);

	FBridgeSchemaProperty Tracks;
	Tracks.Type = TEXT("array");
	Tracks.ItemsType = TEXT("string");
	Tracks.Description = TEXT("Track types to include: 'state_machines', 'montages', 'notifies'. Default: all");
	Tracks.bRequired = false;
	Schema.Add(TEXT("tracks"), Tracks);

	return Schema;
}

FBridgeToolResult URewindOverviewTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!FRewindHelper::HasData())
	{
		return FBridgeToolResult::Error(
			TEXT("No recording data. Start a recording or load a trace file first. The Animation Insights plugin (GameplayInsights) must be enabled in Edit > Plugins."));
	}

	FString ActorTag = GetStringArgOrDefault(Arguments, TEXT("actor_tag"));
	if (ActorTag.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_tag is required."));
	}

	TArray<FString> TrackTypes;
	const TArray<TSharedPtr<FJsonValue>>* TracksArr;
	if (Arguments->TryGetArrayField(TEXT("tracks"), TracksArr))
	{
		for (const auto& Val : *TracksArr)
		{
			TrackTypes.Add(Val->AsString());
		}
	}

	TSharedPtr<FJsonObject> Result = FRewindHelper::CollectOverview(ActorTag, TrackTypes);
	if (!Result.IsValid())
	{
		return FBridgeToolResult::Error(
			FString::Printf(TEXT("No actor found with tag: %s"), *ActorTag));
	}

	return FBridgeToolResult::Json(Result);
}
