// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindHelper.h"
#include "SoftUEBridgeEditorModule.h"
#include "IRewindDebugger.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Dom/JsonValue.h"

// ── Static state ───────────────────────────────────────────────────────────
bool FRewindHelper::bRecordingActive = false;
bool FRewindHelper::bLoadedFromFile = false;
FString FRewindHelper::ActiveTraceFile;
TArray<FString> FRewindHelper::ActiveChannels;
TArray<FString> FRewindHelper::ActiveActorFilters;
double FRewindHelper::RecordingStartTime = 0.0;

// ── Channel mapping ─────────────────────────────────────────────────────────
namespace
{
	struct FChannelMapEntry
	{
		const TCHAR* CliName;
		const TCHAR* TraceName;
	};

	static const FChannelMapEntry GChannelMap[] =
	{
		{ TEXT("skeletal-mesh"), TEXT("SkeletalMeshTrace") },
		{ TEXT("montage"),      TEXT("MontageTrace")      },
		{ TEXT("anim-state"),   TEXT("AnimationTrace")     },
		{ TEXT("notify"),       TEXT("AnimNotifyTrace")    },
		{ TEXT("object"),       TEXT("ObjectTrace")        },
	};
}

FString FRewindHelper::MapChannelName(const FString& CliName)
{
	for (const FChannelMapEntry& Entry : GChannelMap)
	{
		if (CliName.Equals(Entry.CliName, ESearchCase::IgnoreCase))
		{
			return FString(Entry.TraceName);
		}
	}
	return CliName;
}

TArray<FString> FRewindHelper::GetAllChannelNames()
{
	TArray<FString> Names;
	for (const FChannelMapEntry& Entry : GChannelMap)
	{
		Names.Add(FString(Entry.CliName));
	}
	return Names;
}

// ── Rewind Debugger accessor ────────────────────────────────────────────────
IRewindDebugger* FRewindHelper::GetRewindDebugger()
{
	return IRewindDebugger::Instance();
}

// ── Convenience accessors using actual IRewindDebugger API ──────────────────
double FRewindHelper::GetRecordingStartTime()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return 0.0;
	}
	const TRange<double>& Range = Debugger->GetCurrentTraceRange();
	return Range.HasLowerBound() ? Range.GetLowerBoundValue() : 0.0;
}

double FRewindHelper::GetRecordingEndTime()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return 0.0;
	}
	const TRange<double>& Range = Debugger->GetCurrentTraceRange();
	if (Range.HasUpperBound())
	{
		return Range.GetUpperBoundValue();
	}
	// Fallback: use recording duration
	return Debugger->GetRecordingDuration();
}

double FRewindHelper::FrameToTime(int32 Frame, double FrameRate)
{
	double Start = GetRecordingStartTime();
	return Start + (Frame / FrameRate);
}

// ── Recording state queries ─────────────────────────────────────────────────
bool FRewindHelper::IsRecording()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger && Debugger->IsRecording())
	{
		return true;
	}
	return bRecordingActive;
}

double FRewindHelper::GetRecordingDuration()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger)
	{
		return Debugger->GetRecordingDuration();
	}
	if (bRecordingActive)
	{
		return FPlatformTime::Seconds() - RecordingStartTime;
	}
	return 0.0;
}

bool FRewindHelper::HasData()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger)
	{
		// Check if debugger has objects or is recording/loaded
		TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();
		if (Objects.Num() > 0)
		{
			return true;
		}
		if (Debugger->IsRecording() || Debugger->IsTraceFileLoaded())
		{
			return true;
		}
	}
	return bRecordingActive || bLoadedFromFile;
}

// ── Recording control ───────────────────────────────────────────────────────
FString FRewindHelper::StartRecording(
	const TArray<FString>& Channels,
	const TArray<FString>& ActorTags,
	const FString& FilePath)
{
	// Check both our flag and the debugger's state
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (bRecordingActive || (Debugger && Debugger->IsRecording()))
	{
		return TEXT("Already recording. Stop current recording first.");
	}

	// Try using the IRewindDebugger's own recording API first
	if (Debugger && Debugger->CanStartRecording())
	{
		Debugger->StartRecording();
		bRecordingActive = true;
		bLoadedFromFile = false;
		ActiveChannels = Channels;
		ActiveActorFilters = ActorTags;
		ActiveTraceFile = FilePath;
		RecordingStartTime = FPlatformTime::Seconds();
		return FString();
	}

	// Fallback: start via console command
	TArray<FString> ResolvedChannels;
	if (Channels.Num() == 0)
	{
		ResolvedChannels = { TEXT("SkeletalMeshTrace"), TEXT("AnimationTrace"), TEXT("ObjectTrace") };
	}
	else
	{
		for (const FString& Ch : Channels)
		{
			ResolvedChannels.Add(MapChannelName(Ch));
		}
	}

	FString TraceFilePath = FilePath;
	if (TraceFilePath.IsEmpty())
	{
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		FString TraceDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Rewind"));
		IFileManager::Get().MakeDirectory(*TraceDir, true);
		TraceFilePath = FPaths::Combine(TraceDir, FString::Printf(TEXT("Rewind_%s.utrace"), *Timestamp));
	}

	FString ChannelString = FString::Join(ResolvedChannels, TEXT(","));
	FString Command = FString::Printf(TEXT("Trace.Start \"%s\" %s"), *TraceFilePath, *ChannelString);

	UE_LOG(LogSoftUEBridgeEditor, Log,
		TEXT("RewindHelper: Starting recording – channels: %s, file: %s"),
		*ChannelString, *TraceFilePath);

	if (GEngine && GEngine->Exec(nullptr, *Command))
	{
		bRecordingActive = true;
		bLoadedFromFile = false;
		ActiveTraceFile = TraceFilePath;
		ActiveChannels = ResolvedChannels;
		ActiveActorFilters = ActorTags;
		RecordingStartTime = FPlatformTime::Seconds();
		return FString();
	}

	return TEXT("Failed to start trace.");
}

void FRewindHelper::StopRecording()
{
	if (GEngine)
	{
		GEngine->Exec(nullptr, TEXT("Trace.Stop"));
	}
	bRecordingActive = false;
}

// ── File management ─────────────────────────────────────────────────────────
FString FRewindHelper::LoadTraceFile(const FString& FilePath)
{
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return FString::Printf(TEXT("File not found: %s"), *FilePath);
	}

	ActiveTraceFile = FilePath;
	bLoadedFromFile = true;
	bRecordingActive = false;
	return FString();
}

FString FRewindHelper::SaveTraceFile(const FString& FilePath)
{
	if (!HasData())
	{
		return TEXT("No recording data in memory.");
	}

	FString DestDir = FPaths::GetPath(FilePath);
	if (!DestDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DestDir, true);
	}

	if (!ActiveTraceFile.IsEmpty() && IFileManager::Get().FileExists(*ActiveTraceFile))
	{
		uint32 CopyResult = IFileManager::Get().Copy(*FilePath, *ActiveTraceFile);
		if (CopyResult != COPY_OK)
		{
			return FString::Printf(TEXT("Failed to copy trace file to: %s"), *FilePath);
		}
		return FString();
	}

	return TEXT("No source trace file found to save.");
}

// ── Status ──────────────────────────────────────────────────────────────────
TSharedPtr<FJsonObject> FRewindHelper::GetStatus()
{
	TSharedPtr<FJsonObject> Status = MakeShareable(new FJsonObject);

	Status->SetBoolField(TEXT("recording"), IsRecording());
	Status->SetNumberField(TEXT("duration"), GetRecordingDuration());

	TArray<TSharedPtr<FJsonValue>> ChannelsJson;
	for (const FString& Ch : ActiveChannels)
	{
		ChannelsJson.Add(MakeShareable(new FJsonValueString(Ch)));
	}
	Status->SetArrayField(TEXT("channels"), ChannelsJson);

	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	for (const FString& Tag : ActiveActorFilters)
	{
		ActorsJson.Add(MakeShareable(new FJsonValueString(Tag)));
	}
	Status->SetArrayField(TEXT("actors"), ActorsJson);

	if (!ActiveTraceFile.IsEmpty())
	{
		Status->SetStringField(TEXT("file"), ActiveTraceFile);
	}
	else
	{
		Status->SetField(TEXT("file"), MakeShareable(new FJsonValueNull()));
	}

	return Status;
}

// ── Object helpers ──────────────────────────────────────────────────────────
TSharedPtr<FDebugObjectInfo> FRewindHelper::FindObjectByTag(
	IRewindDebugger* Debugger, const FString& ActorTag)
{
	if (!Debugger)
	{
		return nullptr;
	}

	TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();
	for (const TSharedPtr<FDebugObjectInfo>& Info : Objects)
	{
		if (Info.IsValid() && GetActorTagFromDebugObject(*Info) == ActorTag)
		{
			return Info;
		}
		// Also check children
		for (const TSharedPtr<FDebugObjectInfo>& Child : Info->Children)
		{
			if (Child.IsValid() && GetActorTagFromDebugObject(*Child) == ActorTag)
			{
				return Child;
			}
		}
	}
	return nullptr;
}

FString FRewindHelper::GetActorTagFromDebugObject(const FDebugObjectInfo& Info)
{
	// FDebugObjectInfo has ObjectName — use it as the tag identifier
	return Info.ObjectName;
}

// ── Analysis: CollectTrackList ──────────────────────────────────────────────
TSharedPtr<FJsonObject> FRewindHelper::CollectTrackList(const FString& ActorTag)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Recording range
	TSharedPtr<FJsonObject> Range = MakeShareable(new FJsonObject);
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	Range->SetNumberField(TEXT("start"), TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0);
	Range->SetNumberField(TEXT("end"), TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration());
	Result->SetObjectField(TEXT("recording_range"), Range);

	// Enumerate debugged objects
	TArray<TSharedPtr<FJsonValue>> ActorsArr;
	TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();

	for (const TSharedPtr<FDebugObjectInfo>& Info : Objects)
	{
		if (!Info.IsValid())
		{
			continue;
		}

		FString ObjTag = GetActorTagFromDebugObject(*Info);
		if (!ActorTag.IsEmpty() && ObjTag != ActorTag)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActorJson = MakeShareable(new FJsonObject);
		ActorJson->SetStringField(TEXT("actor_tag"), ObjTag);
		ActorJson->SetStringField(TEXT("object_name"), Info->ObjectName);

		// List child object types as available tracks
		TArray<TSharedPtr<FJsonValue>> TracksArr;
		for (const TSharedPtr<FDebugObjectInfo>& Child : Info->Children)
		{
			if (Child.IsValid())
			{
				TracksArr.Add(MakeShareable(new FJsonValueString(Child->ObjectName)));
			}
		}
		ActorJson->SetArrayField(TEXT("tracks"), TracksArr);

		ActorsArr.Add(MakeShareable(new FJsonValueObject(ActorJson)));
	}

	Result->SetArrayField(TEXT("actors"), ActorsArr);
	return Result;
}

// ── Analysis: CollectOverview (stub — data reads depend on track providers) ─
TSharedPtr<FJsonObject> FRewindHelper::CollectOverview(
	const FString& ActorTag,
	const TArray<FString>& TrackTypes)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	// Verify actor exists
	TSharedPtr<FDebugObjectInfo> TargetObj = FindObjectByTag(Debugger, ActorTag);
	if (!TargetObj.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("actor_tag"), ActorTag);

	// Recording range
	TSharedPtr<FJsonObject> Range = MakeShareable(new FJsonObject);
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	Range->SetNumberField(TEXT("start"), TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0);
	Range->SetNumberField(TEXT("end"), TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration());
	Result->SetObjectField(TEXT("recording_range"), Range);

	// TODO: Track-specific overview data requires accessing IAnalysisSession
	// and querying providers (AnimationProvider, etc.) for the target object's
	// trace data. This needs the GameplayInsights provider API, which is
	// separate from the IRewindDebugger interface.
	//
	// For now, return the structure with empty track arrays.
	TSharedPtr<FJsonObject> TracksJson = MakeShareable(new FJsonObject);
	TracksJson->SetArrayField(TEXT("state_machines"), TArray<TSharedPtr<FJsonValue>>());
	TracksJson->SetArrayField(TEXT("montages"), TArray<TSharedPtr<FJsonValue>>());
	TracksJson->SetArrayField(TEXT("notifies"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetObjectField(TEXT("tracks"), TracksJson);

	return Result;
}

// ── Analysis: CollectSnapshot (stub — data reads depend on track providers) ─
TSharedPtr<FJsonObject> FRewindHelper::CollectSnapshot(
	double Time,
	const FString& ActorTag,
	const TArray<FString>& IncludeSections)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	// Validate time range
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	double Start = TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0;
	double End = TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration();
	if (Time < Start || Time > End)
	{
		return nullptr;
	}

	// Verify actor exists
	TSharedPtr<FDebugObjectInfo> TargetObj = FindObjectByTag(Debugger, ActorTag);
	if (!TargetObj.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetStringField(TEXT("actor_tag"), ActorTag);

	bool bIncludeAll = IncludeSections.IsEmpty();

	// TODO: Reading animation state at a specific time requires accessing
	// the IAnalysisSession from the TraceServices and querying the
	// IAnimationProvider / IGameplayProvider for the target object.
	// The IRewindDebugger interface provides GetAnalysisSession() for this.
	//
	// For now, return the structure with empty sections.
	if (bIncludeAll || IncludeSections.Contains(TEXT("state-machines")))
	{
		Result->SetArrayField(TEXT("state_machines"), TArray<TSharedPtr<FJsonValue>>());
	}
	if (bIncludeAll || IncludeSections.Contains(TEXT("montages")))
	{
		Result->SetArrayField(TEXT("active_montages"), TArray<TSharedPtr<FJsonValue>>());
	}
	if (bIncludeAll || IncludeSections.Contains(TEXT("blend-weights")))
	{
		Result->SetObjectField(TEXT("blend_weights"), MakeShareable(new FJsonObject));
	}
	if (bIncludeAll || IncludeSections.Contains(TEXT("curves")))
	{
		Result->SetObjectField(TEXT("curves"), MakeShareable(new FJsonObject));
	}
	if (bIncludeAll || IncludeSections.Contains(TEXT("notifies")))
	{
		Result->SetArrayField(TEXT("notifies"), TArray<TSharedPtr<FJsonValue>>());
	}

	return Result;
}
