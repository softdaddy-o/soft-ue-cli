// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class IRewindDebugger;
struct FDebugObjectInfo;

class FRewindHelper
{
public:
	// Recording State
	static bool IsRecording();
	static double GetRecordingDuration();
	static bool HasData();

	// Recording Control
	static FString StartRecording(
		const TArray<FString>& Channels,
		const TArray<FString>& ActorTags,
		const FString& FilePath);
	static void StopRecording();

	// File Management
	static FString LoadTraceFile(const FString& FilePath);
	static FString SaveTraceFile(const FString& FilePath);

	// Analysis
	static TSharedPtr<FJsonObject> CollectSnapshot(
		double Time,
		const FString& ActorTag,
		const TArray<FString>& IncludeSections);

	static TSharedPtr<FJsonObject> CollectOverview(
		const FString& ActorTag,
		const TArray<FString>& TrackTypes);

	static TSharedPtr<FJsonObject> CollectTrackList(
		const FString& ActorTag);

	// Status
	static TSharedPtr<FJsonObject> GetStatus();

	// Channel Mapping
	static FString MapChannelName(const FString& CliName);
	static TArray<FString> GetAllChannelNames();

	static IRewindDebugger* GetRewindDebugger();

	// Convenience accessors using actual IRewindDebugger API
	static double GetRecordingStartTime();
	static double GetRecordingEndTime();
	static double FrameToTime(int32 Frame, double FrameRate = 60.0);

private:
	static bool bRecordingActive;
	static bool bLoadedFromFile;
	static FString ActiveTraceFile;
	static TArray<FString> ActiveChannels;
	static TArray<FString> ActiveActorFilters;
	static double RecordingStartTime;

	// Object helpers
	static TSharedPtr<FDebugObjectInfo> FindObjectByTag(
		IRewindDebugger* Debugger, const FString& ActorTag);
	static FString GetActorTagFromDebugObject(const FDebugObjectInfo& Info);
};
