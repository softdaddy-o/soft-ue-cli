// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindSnapshotTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindSnapshotTool::GetToolDescription() const
{
	return TEXT("Detailed animation state at a specific point in recorded time. "
		"Mirrors inspect-anim-instance but reads from Rewind Debugger history. "
		"Specify --time (seconds) or --frame (frame number).");
}

TMap<FString, FBridgeSchemaProperty> URewindSnapshotTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Time;
	Time.Type = TEXT("number");
	Time.Description = TEXT("Time in seconds to snapshot (mutually exclusive with frame)");
	Time.bRequired = false;
	Schema.Add(TEXT("time"), Time);

	FBridgeSchemaProperty Frame;
	Frame.Type = TEXT("integer");
	Frame.Description = TEXT("Frame number to snapshot (mutually exclusive with time)");
	Frame.bRequired = false;
	Schema.Add(TEXT("frame"), Frame);

	FBridgeSchemaProperty ActorTag;
	ActorTag.Type = TEXT("string");
	ActorTag.Description = TEXT("Target actor tag");
	ActorTag.bRequired = true;
	Schema.Add(TEXT("actor_tag"), ActorTag);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("array");
	Include.ItemsType = TEXT("string");
	Include.Description = TEXT("Sections to include: 'state-machines', 'montages', 'notifies', 'blend-weights', 'curves'. Default: all");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	return Schema;
}

FBridgeToolResult URewindSnapshotTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!FRewindHelper::HasData())
	{
		return FBridgeToolResult::Error(
			TEXT("No recording data. Start a recording or load a trace file first. The Animation Insights plugin (GameplayInsights) must be enabled in Edit > Plugins."));
	}

	// Parse time/frame (mutually exclusive)
	bool bHasTime = Arguments->HasField(TEXT("time"));
	bool bHasFrame = Arguments->HasField(TEXT("frame"));

	if (bHasTime && bHasFrame)
	{
		return FBridgeToolResult::Error(
			TEXT("Specify either 'time' or 'frame', not both."));
	}
	if (!bHasTime && !bHasFrame)
	{
		return FBridgeToolResult::Error(
			TEXT("Specify 'time' or 'frame'."));
	}

	double Time;
	if (bHasTime)
	{
		Time = Arguments->GetNumberField(TEXT("time"));
	}
	else
	{
		int32 Frame = static_cast<int32>(Arguments->GetNumberField(TEXT("frame")));
		Time = FRewindHelper::FrameToTime(Frame);
	}

	FString ActorTag = GetStringArgOrDefault(Arguments, TEXT("actor_tag"));
	if (ActorTag.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_tag is required."));
	}

	TArray<FString> IncludeSections;
	const TArray<TSharedPtr<FJsonValue>>* IncludeArr;
	if (Arguments->TryGetArrayField(TEXT("include"), IncludeArr))
	{
		for (const auto& Val : *IncludeArr)
		{
			IncludeSections.Add(Val->AsString());
		}
	}

	TSharedPtr<FJsonObject> Result = FRewindHelper::CollectSnapshot(
		Time, ActorTag, IncludeSections);

	if (!Result.IsValid())
	{
		// Determine specific error
		double Start = FRewindHelper::GetRecordingStartTime();
		double End = FRewindHelper::GetRecordingEndTime();
		if (Time < Start || Time > End)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Time %.3f out of range [%.3f, %.3f]."),
				Time, Start, End));
		}
		return FBridgeToolResult::Error(
			FString::Printf(TEXT("No actor found with tag: %s"), *ActorTag));
	}

	return FBridgeToolResult::Json(Result);
}
