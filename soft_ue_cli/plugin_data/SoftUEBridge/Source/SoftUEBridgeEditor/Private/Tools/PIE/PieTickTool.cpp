// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/PieTickTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "Modules/ModuleManager.h"

namespace
{
	TSharedPtr<FJsonObject> BuildPieTickFailureResult(
		const FString& ActivePhase,
		const FString& Message,
		double OperationStartSeconds,
		double OperationDeadlineSeconds,
		int32 RequestedFrames,
		int32 FramesCompleted)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		const double TimeoutSeconds = FMath::Max(0.0, OperationDeadlineSeconds - OperationStartSeconds);
		const double ElapsedSeconds = FMath::Max(0.0, NowSeconds - OperationStartSeconds);
		const double RemainingBudgetSeconds = FMath::Max(0.0, OperationDeadlineSeconds - NowSeconds);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("status"), TEXT("failed"));
		Result->SetStringField(TEXT("tool"), TEXT("pie-tick"));
		Result->SetStringField(TEXT("active_phase"), ActivePhase);
		Result->SetStringField(TEXT("message"), Message);
		Result->SetNumberField(TEXT("requested_frames"), RequestedFrames);
		Result->SetNumberField(TEXT("frames_completed"), FramesCompleted);
		Result->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
		Result->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
		Result->SetNumberField(TEXT("remaining_budget_seconds"), RemainingBudgetSeconds);
		return Result;
	}

	TSharedPtr<FJsonObject> BuildPieTickTimeoutResult(
		const FString& ActivePhase,
		const FString& Message,
		double OperationStartSeconds,
		double OperationDeadlineSeconds,
		int32 RequestedFrames,
		int32 FramesCompleted)
	{
		TSharedPtr<FJsonObject> Result = BuildPieTickFailureResult(
			ActivePhase,
			Message,
			OperationStartSeconds,
			OperationDeadlineSeconds,
			RequestedFrames,
			FramesCompleted);
		Result->SetStringField(TEXT("status"), TEXT("timeout"));
		Result->SetStringField(TEXT("error_code"), TEXT("pie_tick_timeout"));
		return Result;
	}
}

FString UPieTickTool::GetToolDescription() const
{
	return TEXT("Advance the PIE world by N frames at a pinned delta time. Starts PIE if not running by default.");
}

TMap<FString, FBridgeSchemaProperty> UPieTickTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Frames;
	Frames.Type = TEXT("integer");
	Frames.Description = TEXT("Number of frames to advance (must be > 0)");
	Frames.bRequired = true;
	Schema.Add(TEXT("frames"), Frames);

	FBridgeSchemaProperty Delta;
	Delta.Type = TEXT("number");
	Delta.Description = TEXT("Pinned delta seconds per frame (default: 1/60)");
	Schema.Add(TEXT("delta"), Delta);

	FBridgeSchemaProperty AutoStart;
	AutoStart.Type = TEXT("boolean");
	AutoStart.Description = TEXT("Start PIE if it is not already running (default: true)");
	Schema.Add(TEXT("auto_start"), AutoStart);

	FBridgeSchemaProperty Map;
	Map.Type = TEXT("string");
	Map.Description = TEXT("Optional map path to load when auto-starting PIE");
	Schema.Add(TEXT("map"), Map);

	FBridgeSchemaProperty Timeout;
	Timeout.Type = TEXT("number");
	Timeout.Description = TEXT("Maximum seconds to wait for PIE start and tick completion (default: 30)");
	Schema.Add(TEXT("timeout"), Timeout);

	return Schema;
}

FBridgeToolResult UPieTickTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	int32 Frames = 0;
	if (!GetIntArg(Arguments, TEXT("frames"), Frames) || Frames <= 0)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'frames' must be a positive integer"));
	}

	const float Delta = GetFloatArgOrDefault(Arguments, TEXT("delta"), 1.0f / 60.0f);
	if (Delta <= 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'delta' must be > 0"));
	}

	const float TimeoutSeconds = GetFloatArgOrDefault(Arguments, TEXT("timeout"), 30.0f);
	if (TimeoutSeconds <= 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'timeout' must be > 0"));
	}

	const bool bAutoStart = GetBoolArgOrDefault(Arguments, TEXT("auto_start"), true);
	const FString MapPath = GetStringArgOrDefault(Arguments, TEXT("map"));
	const double OperationStartSeconds = FPlatformTime::Seconds();
	const double OperationDeadlineSeconds = OperationStartSeconds + static_cast<double>(TimeoutSeconds);

	bool bPieStartedByCall = false;
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		if (!bAutoStart)
		{
			return FBridgeToolResult::Error(TEXT("pie-tick: PIE is not running and auto_start=false"));
		}

		TSharedPtr<FJsonObject> StartFailure;
		if (!StartPIEForTick(MapPath, OperationStartSeconds, OperationDeadlineSeconds, Frames, StartFailure))
		{
			return FBridgeToolResult::Json(StartFailure);
		}

		bPieStartedByCall = true;
		PIEWorld = GetPIEWorld();
		if (!PIEWorld)
		{
			return FBridgeToolResult::Error(TEXT("pie-tick: PIE reported started but world is null"));
		}
	}

	float WorldTimeBefore = 0.0f;
	float WorldTimeAfter = 0.0f;
	int32 FramesTicked = 0;
	TSharedPtr<FJsonObject> TickFailure;
	const float TickTimeoutSeconds = static_cast<float>(OperationDeadlineSeconds - FPlatformTime::Seconds());
	if (TickTimeoutSeconds <= 0.0f)
	{
		return FBridgeToolResult::Json(BuildPieTickTimeoutResult(
			TEXT("tick_frames"),
			FString::Printf(TEXT("Timeout expired before ticking (%0.2fs)."), TimeoutSeconds),
			OperationStartSeconds,
			OperationDeadlineSeconds,
			Frames,
			0));
	}
	if (!TickWorldFrames(
		PIEWorld,
		Frames,
		Delta,
		TickTimeoutSeconds,
		OperationStartSeconds,
		OperationDeadlineSeconds,
		FramesTicked,
		WorldTimeBefore,
		WorldTimeAfter,
		TickFailure))
	{
		return FBridgeToolResult::Json(TickFailure);
	}

	const float WorldTimeDelta = WorldTimeAfter - WorldTimeBefore;
	if (WorldTimeDelta <= 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: world time did not advance"));
	}
	const float TotalSimTime = WorldTimeDelta;

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("ok"));
	Response->SetNumberField(TEXT("ticks"), FramesTicked);
	Response->SetNumberField(TEXT("delta"), Delta);
	Response->SetNumberField(TEXT("total_sim_time"), TotalSimTime);
	Response->SetNumberField(TEXT("world_time_before"), WorldTimeBefore);
	Response->SetNumberField(TEXT("world_time_after"), WorldTimeAfter);
	Response->SetNumberField(TEXT("world_time_delta"), WorldTimeDelta);
	Response->SetStringField(TEXT("tick_mode"), TEXT("direct_world_tick"));
	Response->SetStringField(TEXT("execution_context"), BridgeToolExecutionContextToString(GetExecutionContextRequirement()));
	Response->SetStringField(TEXT("active_phase"), TEXT("complete"));
	Response->SetBoolField(TEXT("pie_started_by_call"), bPieStartedByCall);
	Response->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	return FBridgeToolResult::Json(Response);
}

UWorld* UPieTickTool::GetPIEWorld() const
{
	return GEditor ? GEditor->PlayWorld : nullptr;
}

bool UPieTickTool::StartPIEForTick(
	const FString& MapPath,
	double OperationStartSeconds,
	double OperationDeadlineSeconds,
	int32 RequestedFrames,
	TSharedPtr<FJsonObject>& OutFailure)
{
	if (!GEditor)
	{
		OutFailure = BuildPieTickFailureResult(
			TEXT("request_play_session"),
			TEXT("GEditor is not available."),
			OperationStartSeconds,
			OperationDeadlineSeconds,
			RequestedFrames,
			0);
		return false;
	}

	if (GEditor->PlayWorld)
	{
		return true;
	}

	if (!MapPath.IsEmpty())
	{
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (!LevelEditorSubsystem || !LevelEditorSubsystem->LoadLevel(MapPath))
		{
			OutFailure = BuildPieTickFailureResult(
				TEXT("map_load"),
				FString::Printf(TEXT("Failed to load map: %s"), *MapPath),
				OperationStartSeconds,
				OperationDeadlineSeconds,
				RequestedFrames,
				0);
			return false;
		}
	}

	if (FPlatformTime::Seconds() >= OperationDeadlineSeconds)
	{
		OutFailure = BuildPieTickTimeoutResult(
			TEXT("map_load"),
			TEXT("Timeout expired while loading the map before RequestPlaySession."),
			OperationStartSeconds,
			OperationDeadlineSeconds,
			RequestedFrames,
			0);
		return false;
	}

	FRequestPlaySessionParams Params;
	GEditor->RequestPlaySession(Params);

	while (FPlatformTime::Seconds() < OperationDeadlineSeconds)
	{
		if (GEditor->PlayWorld)
		{
			return true;
		}

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
		FPlatformProcess::Sleep(0.01f);
	}

	OutFailure = BuildPieTickTimeoutResult(
		TEXT("wait_for_play_world"),
		TEXT("PIE did not start before the timeout budget expired."),
		OperationStartSeconds,
		OperationDeadlineSeconds,
		RequestedFrames,
		0);
	return false;
}

bool UPieTickTool::TickWorldFrames(
	UWorld* World,
	int32 Frames,
	float DeltaSeconds,
	float TimeoutSeconds,
	double OperationStartSeconds,
	double OperationDeadlineSeconds,
	int32& OutFramesTicked,
	float& OutWorldTimeBefore,
	float& OutWorldTimeAfter,
	TSharedPtr<FJsonObject>& OutFailure)
{
	OutFramesTicked = 0;
	if (!World || Frames <= 0 || DeltaSeconds <= 0.0f)
	{
		OutWorldTimeBefore = 0.0f;
		OutWorldTimeAfter = 0.0f;
		OutFailure = BuildPieTickFailureResult(
			TEXT("tick_frames"),
			TEXT("Invalid tick arguments."),
			OperationStartSeconds,
			OperationDeadlineSeconds,
			Frames,
			0);
		return false;
	}

	OutWorldTimeBefore = World->GetTimeSeconds();
	if (World->bInTick)
	{
		OutWorldTimeAfter = OutWorldTimeBefore;
		OutFailure = BuildPieTickFailureResult(
			TEXT("tick_frames"),
			TEXT("World is already ticking."),
			OperationStartSeconds,
			OperationDeadlineSeconds,
			Frames,
			0);
		return false;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const double DeadlineSeconds = StartSeconds + static_cast<double>(TimeoutSeconds);
	for (int32 FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
	{
		if (FPlatformTime::Seconds() >= DeadlineSeconds)
		{
			OutWorldTimeAfter = World->GetTimeSeconds();
			OutFailure = BuildPieTickTimeoutResult(
				TEXT("tick_frames"),
				FString::Printf(TEXT("Tick timed out after %.2fs (%d/%d frames)."),
				TimeoutSeconds,
				OutFramesTicked,
				Frames),
				OperationStartSeconds,
				OperationDeadlineSeconds,
				Frames,
				OutFramesTicked);
			return false;
		}

		World->Tick(ELevelTick::LEVELTICK_All, DeltaSeconds);
		++OutFramesTicked;

		if (FPlatformTime::Seconds() > DeadlineSeconds)
		{
			OutWorldTimeAfter = World->GetTimeSeconds();
			OutFailure = BuildPieTickTimeoutResult(
				TEXT("tick_frames"),
				FString::Printf(TEXT("Tick timed out after %.2fs (%d/%d frames)."),
				TimeoutSeconds,
				OutFramesTicked,
				Frames),
				OperationStartSeconds,
				OperationDeadlineSeconds,
				Frames,
				OutFramesTicked);
			return false;
		}
	}

	OutWorldTimeAfter = World->GetTimeSeconds();
	return true;
}
