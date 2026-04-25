// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/TriggerInputTool.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"

FString UTriggerInputTool::GetToolDescription() const
{
	return TEXT("Simulate player input in a running game (PIE or packaged). Actions: 'key' (press/release a key), 'action' (trigger a named input action), 'move-to' (pathfind to location), 'look-at' (rotate to face target).");
}

TMap<FString, FBridgeSchemaProperty> UTriggerInputTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'key', 'action', 'move-to', or 'look-at'");
	Action.bRequired = true;
	Schema.Add(TEXT("action"), Action);

	FBridgeSchemaProperty PlayerIndex;
	PlayerIndex.Type = TEXT("integer");
	PlayerIndex.Description = TEXT("Player index (default: 0)");
	PlayerIndex.bRequired = false;
	Schema.Add(TEXT("player_index"), PlayerIndex);

	FBridgeSchemaProperty Key;
	Key.Type = TEXT("string");
	Key.Description = TEXT("[key] Key name (e.g., 'W', 'Space', 'LeftMouseButton')");
	Key.bRequired = false;
	Schema.Add(TEXT("key"), Key);

	FBridgeSchemaProperty ActionName;
	ActionName.Type = TEXT("string");
	ActionName.Description = TEXT("[action] Input action name (e.g., 'Jump', 'Attack', 'Interact')");
	ActionName.bRequired = false;
	Schema.Add(TEXT("action_name"), ActionName);

	FBridgeSchemaProperty Pressed;
	Pressed.Type = TEXT("boolean");
	Pressed.Description = TEXT("[key/action] True for press, false for release (default: press then release)");
	Pressed.bRequired = false;
	Schema.Add(TEXT("pressed"), Pressed);

	FBridgeSchemaProperty Target;
	Target.Type = TEXT("array");
	Target.Description = TEXT("[move-to/look-at] Target location as [X, Y, Z]");
	Target.bRequired = false;
	Schema.Add(TEXT("target"), Target);

	FBridgeSchemaProperty TargetActor;
	TargetActor.Type = TEXT("string");
	TargetActor.Description = TEXT("[look-at] Target actor name (alternative to target location)");
	TargetActor.bRequired = false;
	Schema.Add(TEXT("target_actor"), TargetActor);

	return Schema;
}

FBridgeToolResult UTriggerInputTool::Execute(
	const TSharedPtr<FJsonObject>& Args,
	const FBridgeToolContext& Ctx)
{
	FString Action;
	GetStringArg(Args, TEXT("action"), Action);
	Action = Action.ToLower();

	UWorld* World = FindGameWorld();
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No game world found. Start a PIE session or run a packaged build with the bridge plugin."));
	}

	if (Action == TEXT("key"))
	{
		return ExecuteKey(Args, World);
	}
	else if (Action == TEXT("action"))
	{
		return ExecuteAction(Args, World);
	}
	else if (Action == TEXT("move-to") || Action == TEXT("move"))
	{
		return ExecuteMoveTo(Args, World);
	}
	else if (Action == TEXT("look-at") || Action == TEXT("look"))
	{
		return ExecuteLookAt(Args, World);
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Unknown action: '%s'. Valid: key, action, move-to, look-at"), *Action));
}

// ---------------------------------------------------------------------------
// key
// ---------------------------------------------------------------------------

FBridgeToolResult UTriggerInputTool::ExecuteKey(const TSharedPtr<FJsonObject>& Args, UWorld* World)
{
	int32 PlayerIndex = GetIntArgOrDefault(Args, TEXT("player_index"), 0);
	FString KeyName = GetStringArgOrDefault(Args, TEXT("key"));

	if (KeyName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("'key' is required for key action"));
	}

	APlayerController* PC = GetPlayerController(World, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid key: %s"), *KeyName));
	}

	// Determine press mode
	bool bPressOnly = false;
	bool bReleaseOnly = false;
	if (Args->HasField(TEXT("pressed")))
	{
		if (GetBoolArgOrDefault(Args, TEXT("pressed"), true))
		{
			bPressOnly = true;
		}
		else
		{
			bReleaseOnly = true;
		}
	}

	UPlayerInput* PlayerInput = PC->PlayerInput;
	if (!PlayerInput)
	{
		return FBridgeToolResult::Error(TEXT("Player input not available"));
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!bReleaseOnly)
	{
		PlayerInput->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false));
	}
	if (!bPressOnly)
	{
		PlayerInput->InputKey(FInputKeyParams(Key, IE_Released, 0.0, false));
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("key"), KeyName);
	Result->SetStringField(TEXT("event"),
		bPressOnly ? TEXT("pressed") : (bReleaseOnly ? TEXT("released") : TEXT("pressed_and_released")));

	UE_LOG(LogSoftUEBridge, Log, TEXT("trigger-input: Key %s %s"), *KeyName,
		bPressOnly ? TEXT("pressed") : (bReleaseOnly ? TEXT("released") : TEXT("pressed+released")));

	return FBridgeToolResult::Json(Result);
}

// ---------------------------------------------------------------------------
// action
// ---------------------------------------------------------------------------

FBridgeToolResult UTriggerInputTool::ExecuteAction(const TSharedPtr<FJsonObject>& Args, UWorld* World)
{
	int32 PlayerIndex = GetIntArgOrDefault(Args, TEXT("player_index"), 0);
	FString ActionName = GetStringArgOrDefault(Args, TEXT("action_name"));

	if (ActionName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("'action_name' is required for action"));
	}

	APlayerController* PC = GetPlayerController(World, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FBridgeToolResult::Error(TEXT("Player has no pawn"));
	}

	bool bTriggered = false;
	if (ActionName.Equals(TEXT("Jump"), ESearchCase::IgnoreCase))
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			Character->Jump();
			bTriggered = true;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action_name"), ActionName);
	Result->SetBoolField(TEXT("triggered"), bTriggered);

	if (!bTriggered)
	{
		Result->SetStringField(TEXT("message"),
			TEXT("Action not directly mapped. Consider using call-function instead."));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("trigger-input: Action %s triggered=%d"), *ActionName, bTriggered);
	return FBridgeToolResult::Json(Result);
}

// ---------------------------------------------------------------------------
// move-to
// ---------------------------------------------------------------------------

FBridgeToolResult UTriggerInputTool::ExecuteMoveTo(const TSharedPtr<FJsonObject>& Args, UWorld* World)
{
	int32 PlayerIndex = GetIntArgOrDefault(Args, TEXT("player_index"), 0);

	if (!Args->HasField(TEXT("target")))
	{
		return FBridgeToolResult::Error(TEXT("'target' location is required for move-to action"));
	}

	const TArray<TSharedPtr<FJsonValue>>* TargetArray;
	if (!Args->TryGetArrayField(TEXT("target"), TargetArray) || TargetArray->Num() < 3)
	{
		return FBridgeToolResult::Error(TEXT("'target' must be an array of 3 numbers [X, Y, Z]"));
	}

	FVector Target(
		(*TargetArray)[0]->AsNumber(),
		(*TargetArray)[1]->AsNumber(),
		(*TargetArray)[2]->AsNumber());

	APlayerController* PC = GetPlayerController(World, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FBridgeToolResult::Error(TEXT("Player has no pawn"));
	}

	UAIBlueprintHelperLibrary::SimpleMoveToLocation(PC, Target);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("moving"));

	TArray<TSharedPtr<FJsonValue>> TargetArr;
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.X)));
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.Y)));
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.Z)));
	Result->SetArrayField(TEXT("target"), TargetArr);

	FVector CurrentLoc = Pawn->GetActorLocation();
	Result->SetNumberField(TEXT("distance"), FVector::Dist(CurrentLoc, Target));

	UE_LOG(LogSoftUEBridge, Log, TEXT("trigger-input: Moving to [%.0f, %.0f, %.0f]"), Target.X, Target.Y, Target.Z);
	return FBridgeToolResult::Json(Result);
}

// ---------------------------------------------------------------------------
// look-at
// ---------------------------------------------------------------------------

FBridgeToolResult UTriggerInputTool::ExecuteLookAt(const TSharedPtr<FJsonObject>& Args, UWorld* World)
{
	int32 PlayerIndex = GetIntArgOrDefault(Args, TEXT("player_index"), 0);

	APlayerController* PC = GetPlayerController(World, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FBridgeToolResult::Error(TEXT("Player has no pawn"));
	}

	FVector Target = FVector::ZeroVector;
	FString TargetActorName = GetStringArgOrDefault(Args, TEXT("target_actor"));

	if (!TargetActorName.IsEmpty())
	{
		bool bFound = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (MatchesWildcard((*It)->GetName(), TargetActorName))
			{
				Target = (*It)->GetActorLocation();
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target actor not found: %s"), *TargetActorName));
		}
	}
	else if (Args->HasField(TEXT("target")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (!Args->TryGetArrayField(TEXT("target"), TargetArray) || TargetArray->Num() < 3)
		{
			return FBridgeToolResult::Error(TEXT("'target' must be an array of 3 numbers [X, Y, Z]"));
		}
		Target.X = (*TargetArray)[0]->AsNumber();
		Target.Y = (*TargetArray)[1]->AsNumber();
		Target.Z = (*TargetArray)[2]->AsNumber();
	}
	else
	{
		return FBridgeToolResult::Error(TEXT("Either 'target' or 'target_actor' is required for look-at action"));
	}

	FVector Direction = Target - Pawn->GetActorLocation();
	Direction.Z = 0;
	FRotator NewRotation = Direction.Rotation();
	PC->SetControlRotation(NewRotation);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> RotArr;
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Pitch)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Yaw)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Roll)));
	Result->SetArrayField(TEXT("rotation"), RotArr);

	UE_LOG(LogSoftUEBridge, Log, TEXT("trigger-input: Looking at yaw=%.1f"), NewRotation.Yaw);
	return FBridgeToolResult::Json(Result);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UWorld* UTriggerInputTool::FindGameWorld()
{
	// Prefer a standalone Game world, then fall back to PIE.
	UWorld* Found = FindWorldByType(TEXT("game"));
	if (!Found)
	{
		Found = FindWorldByType(TEXT("pie"));
	}
	return Found;
}

APlayerController* UTriggerInputTool::GetPlayerController(UWorld* World, int32 PlayerIndex)
{
	int32 Idx = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (Idx == PlayerIndex)
		{
			return It->Get();
		}
		Idx++;
	}
	return nullptr;
}
