// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/PIE/PieSessionTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "EngineUtils.h"

namespace
{
	enum class EBridgePIETransition : uint8
	{
		None,
		Starting,
		Stopping
	};

	EBridgePIETransition GBridgePIETransition = EBridgePIETransition::None;
	double GBridgePIETransitionStartedAt = 0.0;
	FString GBridgePIESessionId;

	constexpr double GBridgePIETransitionGraceSeconds = 10.0;

	bool IsPIEReadyState(UWorld* PIEWorld)
	{
		return PIEWorld && (PIEWorld->GetFirstPlayerController() || PIEWorld->HasBegunPlay() || GEditor->PlayWorld == PIEWorld);
	}

	void NotePIETransition(EBridgePIETransition Transition)
	{
		GBridgePIETransition = Transition;
		GBridgePIETransitionStartedAt = FPlatformTime::Seconds();
	}

	void ClearPIETransition()
	{
		GBridgePIETransition = EBridgePIETransition::None;
		GBridgePIETransitionStartedAt = 0.0;
	}

	bool IsTransitionFresh()
	{
		return GBridgePIETransition != EBridgePIETransition::None
			&& (FPlatformTime::Seconds() - GBridgePIETransitionStartedAt) < GBridgePIETransitionGraceSeconds;
	}
}

FString UPieSessionTool::GetToolDescription() const
{
	return TEXT("Control PIE (Play-In-Editor) sessions. Actions: 'start', 'stop', 'pause', 'resume', 'get-state', 'wait-for' (wait until condition met).");
}

TMap<FString, FBridgeSchemaProperty> UPieSessionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'start', 'stop', 'pause', 'resume', 'get-state', or 'wait-for'");
	Action.bRequired = true;
	Schema.Add(TEXT("action"), Action);

	FBridgeSchemaProperty Mode;
	Mode.Type = TEXT("string");
	Mode.Description = TEXT("[start] PIE launch mode: 'viewport' (default), 'new_window', or 'standalone'");
	Mode.bRequired = false;
	Schema.Add(TEXT("mode"), Mode);

	FBridgeSchemaProperty MapPath;
	MapPath.Type = TEXT("string");
	MapPath.Description = TEXT("[start] Optional map to load (e.g., '/Game/Maps/TestLevel')");
	MapPath.bRequired = false;
	Schema.Add(TEXT("map"), MapPath);

	FBridgeSchemaProperty TimeoutSeconds;
	TimeoutSeconds.Type = TEXT("number");
	TimeoutSeconds.Description = TEXT("[start] Timeout in seconds to wait for PIE ready (default: 30)");
	TimeoutSeconds.bRequired = false;
	Schema.Add(TEXT("timeout"), TimeoutSeconds);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("array");
	Include.Description = TEXT("[get-state] What to include: 'world', 'players'. Default: all.");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	// wait-for parameters
	FBridgeSchemaProperty ActorName;
	ActorName.Type = TEXT("string");
	ActorName.Description = TEXT("[wait-for] Actor name to monitor");
	ActorName.bRequired = false;
	Schema.Add(TEXT("actor_name"), ActorName);

	FBridgeSchemaProperty Property;
	Property.Type = TEXT("string");
	Property.Description = TEXT("[wait-for] Property name to check (e.g., 'Health', 'bIsDead')");
	Property.bRequired = false;
	Schema.Add(TEXT("property"), Property);

	FBridgeSchemaProperty Operator;
	Operator.Type = TEXT("string");
	Operator.Description = TEXT("[wait-for] Comparison: 'equals', 'not_equals', 'less_than', 'greater_than', 'contains'");
	Operator.bRequired = false;
	Schema.Add(TEXT("operator"), Operator);

	FBridgeSchemaProperty ExpectedValue;
	ExpectedValue.Type = TEXT("any");
	ExpectedValue.Description = TEXT("[wait-for] Expected value for comparison");
	ExpectedValue.bRequired = false;
	Schema.Add(TEXT("expected"), ExpectedValue);

	FBridgeSchemaProperty WaitTimeout;
	WaitTimeout.Type = TEXT("number");
	WaitTimeout.Description = TEXT("[wait-for] Timeout in seconds (default: 10)");
	WaitTimeout.bRequired = false;
	Schema.Add(TEXT("wait_timeout"), WaitTimeout);

	FBridgeSchemaProperty PollInterval;
	PollInterval.Type = TEXT("number");
	PollInterval.Description = TEXT("[wait-for] Poll interval in seconds (default: 0.1)");
	PollInterval.bRequired = false;
	Schema.Add(TEXT("poll_interval"), PollInterval);

	return Schema;
}

FBridgeToolResult UPieSessionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString Action;
	GetStringArg(Arguments, TEXT("action"), Action);
	Action = Action.ToLower();

	if (Action == TEXT("start"))
	{
		return ExecuteStart(Arguments);
	}
	else if (Action == TEXT("stop"))
	{
		return ExecuteStop(Arguments);
	}
	else if (Action == TEXT("pause"))
	{
		return ExecutePause(Arguments);
	}
	else if (Action == TEXT("resume"))
	{
		return ExecuteResume(Arguments);
	}
	else if (Action == TEXT("get-state") || Action == TEXT("state") || Action == TEXT("status"))
	{
		return ExecuteGetState(Arguments);
	}
	else if (Action == TEXT("wait-for") || Action == TEXT("wait"))
	{
		return ExecuteWaitFor(Arguments);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Unknown action: '%s'. Valid: start, stop, pause, resume, get-state, wait-for"), *Action));
	}
}

FBridgeToolResult UPieSessionTool::ExecuteStart(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("viewport"));
	FString MapPath = GetStringArgOrDefault(Arguments, TEXT("map"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Starting PIE (mode=%s, map=%s)"),
		*Mode, MapPath.IsEmpty() ? TEXT("current") : *MapPath);

	// Check if PIE is already running
	if (GEditor->IsPlaySessionInProgress())
	{
		UWorld* PIEWorld = GetPIEWorld();
		if (PIEWorld)
		{
			ClearPIETransition();
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetBoolField(TEXT("success"), true);
			if (GBridgePIESessionId.IsEmpty())
			{
				GBridgePIESessionId = GenerateSessionId();
			}
			Result->SetStringField(TEXT("session_id"), GBridgePIESessionId);
			Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
			Result->SetStringField(TEXT("state"), TEXT("already_running"));
			return FBridgeToolResult::Json(Result);
		}
	}

	// Load specific map if requested
	if (!MapPath.IsEmpty())
	{
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (LevelEditorSubsystem && !LevelEditorSubsystem->LoadLevel(MapPath))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load map: %s"), *MapPath));
		}
	}

	// Configure PIE settings
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;

	if (Mode.Equals(TEXT("new_window"), ESearchCase::IgnoreCase) ||
		Mode.Equals(TEXT("standalone"), ESearchCase::IgnoreCase))
	{
		Params.DestinationSlateViewport = nullptr;
	}
	else // viewport (default)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();
		if (ActiveViewport.IsValid())
		{
			Params.DestinationSlateViewport = ActiveViewport;
		}
	}

	GEditor->RequestPlaySession(Params);

	if (GBridgePIESessionId.IsEmpty())
	{
		GBridgePIESessionId = GenerateSessionId();
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (IsPIEReadyState(PIEWorld))
	{
		ClearPIETransition();
	}
	else
	{
		NotePIETransition(EBridgePIETransition::Starting);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("session_id"), GBridgePIESessionId);
	Result->SetStringField(TEXT("state"), IsPIEReadyState(PIEWorld) ? TEXT("running") : TEXT("starting"));
	if (PIEWorld)
	{
		Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Start requested"));
	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieSessionTool::ExecuteStop(const TSharedPtr<FJsonObject>& Arguments)
{
	const bool bRunning = GEditor->IsPlaySessionInProgress();
	if (!bRunning && GBridgePIETransition != EBridgePIETransition::Starting)
	{
		ClearPIETransition();
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("state"), TEXT("not_running"));
		return FBridgeToolResult::Json(Result);
	}

	if (bRunning)
	{
		GEditor->RequestEndPlayMap();
	}

	if (GEditor->IsPlaySessionInProgress())
	{
		NotePIETransition(EBridgePIETransition::Stopping);
	}
	else
	{
		ClearPIETransition();
		GBridgePIESessionId.Reset();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state"), GEditor->IsPlaySessionInProgress() ? TEXT("stopping") : TEXT("stopped"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Stop requested"));
	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieSessionTool::ExecutePause(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FBridgeToolResult::Error(TEXT("PIE world not found"));
	}

	if (PIEWorld->IsPaused())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("paused"), true);
		Result->SetStringField(TEXT("message"), TEXT("Already paused"));
		return FBridgeToolResult::Json(Result);
	}

	if (GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = true;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("paused"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Paused"));
	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieSessionTool::ExecuteResume(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FBridgeToolResult::Error(TEXT("PIE world not found"));
	}

	if (!PIEWorld->IsPaused())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("paused"), false);
		Result->SetStringField(TEXT("message"), TEXT("Already running"));
		return FBridgeToolResult::Json(Result);
	}

	if (GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = false;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("paused"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Resumed"));
	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieSessionTool::ExecuteGetState(const TSharedPtr<FJsonObject>& Arguments)
{
	TSet<FString> IncludeSet;
	if (Arguments->HasField(TEXT("include")))
	{
		const TArray<TSharedPtr<FJsonValue>>* IncludeArray;
		if (Arguments->TryGetArrayField(TEXT("include"), IncludeArray))
		{
			for (const TSharedPtr<FJsonValue>& Val : *IncludeArray)
			{
				IncludeSet.Add(Val->AsString().ToLower());
			}
		}
	}

	bool bIncludeWorld = IncludeSet.Num() == 0 || IncludeSet.Contains(TEXT("world"));
	bool bIncludePlayers = IncludeSet.Num() == 0 || IncludeSet.Contains(TEXT("players"));

	bool bRunning = GEditor->IsPlaySessionInProgress();
	UWorld* PIEWorld = GetPIEWorld();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("running"), bRunning);
	if (!GBridgePIESessionId.IsEmpty())
	{
		Result->SetStringField(TEXT("session_id"), GBridgePIESessionId);
	}

	if (bRunning && IsPIEReadyState(PIEWorld))
	{
		ClearPIETransition();
	}

	if (GBridgePIETransition == EBridgePIETransition::Starting && !IsTransitionFresh())
	{
		ClearPIETransition();
	}
	else if (GBridgePIETransition == EBridgePIETransition::Stopping && !bRunning)
	{
		ClearPIETransition();
		GBridgePIESessionId.Reset();
	}

	if (GBridgePIETransition == EBridgePIETransition::Starting)
	{
		Result->SetStringField(TEXT("state"), TEXT("starting"));
		return FBridgeToolResult::Json(Result);
	}

	if (GBridgePIETransition == EBridgePIETransition::Stopping)
	{
		Result->SetStringField(TEXT("state"), bRunning ? TEXT("stopping") : TEXT("stopped"));
		return FBridgeToolResult::Json(Result);
	}

	if (!bRunning)
	{
		Result->SetStringField(TEXT("state"), TEXT("not_running"));
		return FBridgeToolResult::Json(Result);
	}

	if (!PIEWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("initializing"));
		return FBridgeToolResult::Json(Result);
	}

	Result->SetStringField(TEXT("state"), TEXT("running"));
	Result->SetBoolField(TEXT("paused"), PIEWorld->IsPaused());

	if (bIncludeWorld)
	{
		Result->SetObjectField(TEXT("world"), GetWorldInfo(PIEWorld));
	}

	if (bIncludePlayers)
	{
		Result->SetArrayField(TEXT("players"), GetPlayersInfo(PIEWorld));
	}

	return FBridgeToolResult::Json(Result);
}

FString UPieSessionTool::GenerateSessionId() const
{
	return FString::Printf(TEXT("pie_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
}

UWorld* UPieSessionTool::GetPIEWorld() const
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.World())
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}

bool UPieSessionTool::WaitForPIEReady(float TimeoutSeconds) const
{
	const double EndTime = FPlatformTime::Seconds() + TimeoutSeconds;

	while (FPlatformTime::Seconds() < EndTime)
	{
		FPlatformProcess::Sleep(0.1f);

		if (GEditor->IsPlaySessionInProgress())
		{
			UWorld* PIEWorld = GetPIEWorld();
			if (IsPIEReadyState(PIEWorld))
			{
				return true;
			}
		}
	}
	return false;
}

TSharedPtr<FJsonObject> UPieSessionTool::GetWorldInfo(UWorld* PIEWorld) const
{
	TSharedPtr<FJsonObject> WorldInfo = MakeShareable(new FJsonObject);
	WorldInfo->SetStringField(TEXT("name"), PIEWorld->GetName());
	WorldInfo->SetStringField(TEXT("map_name"), PIEWorld->GetMapName());
	WorldInfo->SetNumberField(TEXT("time_seconds"), PIEWorld->GetTimeSeconds());

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It) { ActorCount++; }
	WorldInfo->SetNumberField(TEXT("actor_count"), ActorCount);

	return WorldInfo;
}

TArray<TSharedPtr<FJsonValue>> UPieSessionTool::GetPlayersInfo(UWorld* PIEWorld) const
{
	TArray<TSharedPtr<FJsonValue>> PlayersArray;

	int32 PlayerIndex = 0;
	for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;

		TSharedPtr<FJsonObject> PlayerInfo = MakeShareable(new FJsonObject);
		PlayerInfo->SetNumberField(TEXT("player_index"), PlayerIndex);
		PlayerInfo->SetStringField(TEXT("controller_name"), PC->GetName());

		if (APawn* Pawn = PC->GetPawn())
		{
			PlayerInfo->SetStringField(TEXT("pawn_name"), Pawn->GetName());
			PlayerInfo->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());

			FVector Loc = Pawn->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> LocArray;
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.X)));
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Y)));
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Z)));
			PlayerInfo->SetArrayField(TEXT("location"), LocArray);

			PlayerInfo->SetNumberField(TEXT("speed"), Pawn->GetVelocity().Size());
		}

		PlayersArray.Add(MakeShareable(new FJsonValueObject(PlayerInfo)));
		PlayerIndex++;
	}

	return PlayersArray;
}

FBridgeToolResult UPieSessionTool::ExecuteWaitFor(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FBridgeToolResult::Error(TEXT("PIE world not found"));
	}

	FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	FString PropertyName = GetStringArgOrDefault(Arguments, TEXT("property"));
	FString Operator = GetStringArgOrDefault(Arguments, TEXT("operator"), TEXT("equals"));
	float WaitTimeout = GetFloatArgOrDefault(Arguments, TEXT("wait_timeout"), 10.0f);
	float PollInterval = GetFloatArgOrDefault(Arguments, TEXT("poll_interval"), 0.1f);

	if (ActorName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_name is required for wait-for action"));
	}
	if (PropertyName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("property is required for wait-for action"));
	}

	// Get expected value
	TSharedPtr<FJsonValue> ExpectedJson = Arguments->TryGetField(TEXT("expected"));
	if (!ExpectedJson.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("expected value is required for wait-for action"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: Waiting for %s.%s %s ..."),
		*ActorName, *PropertyName, *Operator);

	const double StartTime = FPlatformTime::Seconds();
	const double EndTime = StartTime + WaitTimeout;
	bool bConditionMet = false;
	TSharedPtr<FJsonValue> ActualValue;

	while (FPlatformTime::Seconds() < EndTime)
	{
		// Check if PIE is still running
		if (!GEditor->IsPlaySessionInProgress())
		{
			return FBridgeToolResult::Error(TEXT("PIE session ended while waiting"));
		}

		// Find actor
		AActor* Actor = FindActorByName(PIEWorld, ActorName);
		if (!Actor)
		{
			// Actor might not exist yet, keep waiting
			FPlatformProcess::Sleep(PollInterval);
			continue;
		}

		// Get property value
		ActualValue = GetActorProperty(Actor, PropertyName);
		if (!ActualValue.IsValid())
		{
			FPlatformProcess::Sleep(PollInterval);
			continue;
		}

		// Compare values
		bool bMatch = false;
		if (Operator == TEXT("equals") || Operator == TEXT("eq") || Operator == TEXT("=="))
		{
			// Compare based on type
			if (ActualValue->Type == EJson::Boolean && ExpectedJson->Type == EJson::Boolean)
			{
				bMatch = ActualValue->AsBool() == ExpectedJson->AsBool();
			}
			else if (ActualValue->Type == EJson::Number && ExpectedJson->Type == EJson::Number)
			{
				bMatch = FMath::IsNearlyEqual(ActualValue->AsNumber(), ExpectedJson->AsNumber(), 0.001);
			}
			else if (ActualValue->Type == EJson::String && ExpectedJson->Type == EJson::String)
			{
				bMatch = ActualValue->AsString().Equals(ExpectedJson->AsString(), ESearchCase::IgnoreCase);
			}
			else
			{
				// Fallback: compare as strings
				bMatch = ActualValue->AsString().Equals(ExpectedJson->AsString());
			}
		}
		else if (Operator == TEXT("not_equals") || Operator == TEXT("ne") || Operator == TEXT("!="))
		{
			if (ActualValue->Type == EJson::Boolean && ExpectedJson->Type == EJson::Boolean)
			{
				bMatch = ActualValue->AsBool() != ExpectedJson->AsBool();
			}
			else if (ActualValue->Type == EJson::Number && ExpectedJson->Type == EJson::Number)
			{
				bMatch = !FMath::IsNearlyEqual(ActualValue->AsNumber(), ExpectedJson->AsNumber(), 0.001);
			}
			else
			{
				bMatch = !ActualValue->AsString().Equals(ExpectedJson->AsString());
			}
		}
		else if (Operator == TEXT("less_than") || Operator == TEXT("lt") || Operator == TEXT("<"))
		{
			bMatch = ActualValue->AsNumber() < ExpectedJson->AsNumber();
		}
		else if (Operator == TEXT("greater_than") || Operator == TEXT("gt") || Operator == TEXT(">"))
		{
			bMatch = ActualValue->AsNumber() > ExpectedJson->AsNumber();
		}
		else if (Operator == TEXT("less_equal") || Operator == TEXT("le") || Operator == TEXT("<="))
		{
			bMatch = ActualValue->AsNumber() <= ExpectedJson->AsNumber();
		}
		else if (Operator == TEXT("greater_equal") || Operator == TEXT("ge") || Operator == TEXT(">="))
		{
			bMatch = ActualValue->AsNumber() >= ExpectedJson->AsNumber();
		}
		else if (Operator == TEXT("contains"))
		{
			bMatch = ActualValue->AsString().Contains(ExpectedJson->AsString());
		}

		if (bMatch)
		{
			bConditionMet = true;
			break;
		}

		FPlatformProcess::Sleep(PollInterval);
	}

	double WaitTime = FPlatformTime::Seconds() - StartTime;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bConditionMet);
	Result->SetBoolField(TEXT("condition_met"), bConditionMet);
	Result->SetNumberField(TEXT("wait_time_seconds"), WaitTime);

	if (ActualValue.IsValid())
	{
		Result->SetField(TEXT("actual_value"), ActualValue);
	}

	if (!bConditionMet)
	{
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Timeout after %.1f seconds"), WaitTime));
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("pie-session: wait-for timed out after %.1fs"), WaitTime);
	}
	else
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-session: wait-for condition met in %.1fs"), WaitTime);
	}

	return FBridgeToolResult::Json(Result);
}

AActor* UPieSessionTool::FindActorByName(UWorld* World, const FString& ActorName) const
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonValue> UPieSessionTool::GetActorProperty(AActor* Actor, const FString& PropertyName) const
{
	if (!Actor) return nullptr;

	FProperty* Property = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property) return nullptr;

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Actor);

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			double Value = 0;
			NumProp->GetValue_InContainer(Actor, &Value);
			return MakeShareable(new FJsonValueNumber(Value));
		}
		else
		{
			int64 Value = 0;
			NumProp->GetValue_InContainer(Actor, &Value);
			return MakeShareable(new FJsonValueNumber(static_cast<double>(Value)));
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShareable(new FJsonValueBoolean(BoolProp->GetPropertyValue(ValuePtr)));
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(StrProp->GetPropertyValue(ValuePtr)));
	}

	// Fallback
	FString ExportedText;
	Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShareable(new FJsonValueString(ExportedText));
}
