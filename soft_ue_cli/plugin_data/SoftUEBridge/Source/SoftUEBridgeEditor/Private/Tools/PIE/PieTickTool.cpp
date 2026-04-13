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
#include "Containers/Ticker.h"

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

	const bool bAutoStart = GetBoolArgOrDefault(Arguments, TEXT("auto_start"), true);
	const FString MapPath = GetStringArgOrDefault(Arguments, TEXT("map"));

	bool bPieStartedByCall = false;
	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		if (!bAutoStart)
		{
			return FBridgeToolResult::Error(TEXT("pie-tick: PIE is not running and auto_start=false"));
		}

		FString StartError;
		if (!StartPIEForTick(MapPath, StartError))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("pie-tick: %s"), *StartError));
		}

		bPieStartedByCall = true;
		PIEWorld = GetPIEWorld();
		if (!PIEWorld)
		{
			return FBridgeToolResult::Error(TEXT("pie-tick: PIE reported started but world is null"));
		}
	}

	const float TotalSimTime = TickWorldFrames(PIEWorld, Frames, Delta);

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("ok"));
	Response->SetNumberField(TEXT("ticks"), Frames);
	Response->SetNumberField(TEXT("delta"), Delta);
	Response->SetNumberField(TEXT("total_sim_time"), TotalSimTime);
	Response->SetBoolField(TEXT("pie_started_by_call"), bPieStartedByCall);
	Response->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	return FBridgeToolResult::Json(Response);
}

UWorld* UPieTickTool::GetPIEWorld() const
{
	return GEditor ? GEditor->PlayWorld : nullptr;
}

bool UPieTickTool::StartPIEForTick(const FString& MapPath, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
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
			OutError = FString::Printf(TEXT("Failed to load map: %s"), *MapPath);
			return false;
		}
	}

	FRequestPlaySessionParams Params;
	GEditor->RequestPlaySession(Params);

	constexpr double StartTimeoutSeconds = 30.0;
	const double Deadline = FPlatformTime::Seconds() + StartTimeoutSeconds;
	while (FPlatformTime::Seconds() < Deadline)
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

	OutError = TEXT("PIE did not start within 30 seconds");
	return false;
}

float UPieTickTool::TickWorldFrames(UWorld* World, int32 Frames, float DeltaSeconds)
{
	if (!World || Frames <= 0 || DeltaSeconds <= 0.0f)
	{
		return 0.0f;
	}

	// We cannot call World->Tick() directly from the bridge tool handler because
	// it runs inside AsyncTask(GameThread), which is itself a task graph task.
	// When any FTickableEditorObject (e.g., UMassEntityEditorSubsystem) posts a
	// game-thread task and waits synchronously during its Tick(), it causes
	// re-entrant TaskGraph execution → assertion failure + crash.
	//
	// Fix: Defer each frame tick to a FTSTicker delegate that fires during the
	// normal engine tick loop (via FSlateApplication::Tick). This ensures
	// World->Tick() runs in the Slate/engine context, not the task graph context.

	struct FTickState
	{
		TWeakObjectPtr<UWorld> WorldPtr;
		int32 FramesRemaining;
		float DeltaSeconds;
		float TotalTime;
		bool bComplete;
	};

	auto State = MakeShared<FTickState>();
	State->WorldPtr = World;
	State->FramesRemaining = Frames;
	State->DeltaSeconds = DeltaSeconds;
	State->TotalTime = 0.0f;
	State->bComplete = false;

	FTSTicker::FDelegateHandle TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([State](float) -> bool
		{
			UWorld* W = State->WorldPtr.Get();
			if (!W || State->FramesRemaining <= 0)
			{
				State->bComplete = true;
				return false;
			}

			W->Tick(ELevelTick::LEVELTICK_All, State->DeltaSeconds);
			State->TotalTime += State->DeltaSeconds;
			State->FramesRemaining--;

			if (State->FramesRemaining <= 0)
			{
				State->bComplete = true;
				return false;
			}
			return true;
		}),
		0.0f
	);

	// Pump Slate to drive the ticker (same pattern as StartPIEForTick)
	constexpr double TimeoutSeconds = 300.0;
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (!State->bComplete && FPlatformTime::Seconds() < Deadline)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
		FPlatformProcess::Sleep(0.001f);
	}

	if (!State->bComplete)
	{
		// Timeout — remove ticker if still active
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("PieTickTool: timed out after %.0fs"), TimeoutSeconds);
	}

	return State->TotalTime;
}
