// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/PIE/PieInputTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"

FString UPieInputTool::GetToolDescription() const
{
	return TEXT("Simulate player input in PIE. Actions: 'key' (press key), 'action' (trigger input action), 'axis' (set axis value), 'move-to' (pathfind to location), 'look-at' (rotate to face target).");
}

TMap<FString, FBridgeSchemaProperty> UPieInputTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'key', 'action', 'axis', 'move-to', or 'look-at'");
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

	FBridgeSchemaProperty AxisName;
	AxisName.Type = TEXT("string");
	AxisName.Description = TEXT("[axis] Input axis name (e.g., 'MoveForward', 'Turn')");
	AxisName.bRequired = false;
	Schema.Add(TEXT("axis_name"), AxisName);

	FBridgeSchemaProperty Value;
	Value.Type = TEXT("number");
	Value.Description = TEXT("[axis] Axis value (-1.0 to 1.0)");
	Value.bRequired = false;
	Schema.Add(TEXT("value"), Value);

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

	FBridgeSchemaProperty AcceptanceRadius;
	AcceptanceRadius.Type = TEXT("number");
	AcceptanceRadius.Description = TEXT("[move-to] Acceptable distance from target (default: 50)");
	AcceptanceRadius.bRequired = false;
	Schema.Add(TEXT("acceptance_radius"), AcceptanceRadius);

	return Schema;
}

FBridgeToolResult UPieInputTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString Action;
	GetStringArg(Arguments, TEXT("action"), Action);
	Action = Action.ToLower();

	if (!GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running. Use pie-session action:start first."));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FBridgeToolResult::Error(TEXT("PIE world not found"));
	}

	if (Action == TEXT("key"))
	{
		return ExecuteKey(Arguments, PIEWorld);
	}
	else if (Action == TEXT("action"))
	{
		return ExecuteAction(Arguments, PIEWorld);
	}
	else if (Action == TEXT("axis"))
	{
		return ExecuteAxis(Arguments, PIEWorld);
	}
	else if (Action == TEXT("move-to") || Action == TEXT("move"))
	{
		return ExecuteMoveTo(Arguments, PIEWorld);
	}
	else if (Action == TEXT("look-at") || Action == TEXT("look"))
	{
		return ExecuteLookAt(Arguments, PIEWorld);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Unknown action: '%s'. Valid: key, action, axis, move-to, look-at"), *Action));
	}
}

FBridgeToolResult UPieInputTool::ExecuteKey(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	FString KeyName = GetStringArgOrDefault(Arguments, TEXT("key"));

	if (KeyName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("key is required for key action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	// Find the key
	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid key: %s"), *KeyName));
	}

	// Determine press mode
	bool bPressOnly = false;
	bool bReleaseOnly = false;
	if (Arguments->HasField(TEXT("pressed")))
	{
		bool bPressed = GetBoolArgOrDefault(Arguments, TEXT("pressed"), true);
		if (bPressed)
		{
			bPressOnly = true;
		}
		else
		{
			bReleaseOnly = true;
		}
	}

	// Simulate key input via PlayerInput
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
		// Small delay then release
		PlayerInput->InputKey(FInputKeyParams(Key, IE_Released, 0.0, false));
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("key"), KeyName);
	Result->SetStringField(TEXT("event"), bPressOnly ? TEXT("pressed") : (bReleaseOnly ? TEXT("released") : TEXT("pressed_and_released")));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-input: Key %s %s"), *KeyName,
		bPressOnly ? TEXT("pressed") : (bReleaseOnly ? TEXT("released") : TEXT("pressed+released")));

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieInputTool::ExecuteAction(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	FString ActionName = GetStringArgOrDefault(Arguments, TEXT("action_name"));

	if (ActionName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("action_name is required for action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	// Try to find and trigger the action via input component
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FBridgeToolResult::Error(TEXT("Player has no pawn"));
	}

	// For now, we use a simple approach - call common action functions directly
	// In a full implementation, we'd look up the action binding

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
		Result->SetStringField(TEXT("message"), TEXT("Action not directly mapped. Consider using pie-actor call-function instead."));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-input: Action %s triggered=%d"), *ActionName, bTriggered);

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieInputTool::ExecuteAxis(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	// Axis input simulation is not directly supported in UE 5.6+
	// The InputAxis method was removed from APlayerController
	return FBridgeToolResult::Error(TEXT("Axis input simulation is not supported in UE 5.6+. Use 'key' action with axis keys (e.g., Gamepad_LeftX) or 'move-to' for character movement."));
}

FBridgeToolResult UPieInputTool::ExecuteMoveTo(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	float AcceptanceRadius = GetFloatArgOrDefault(Arguments, TEXT("acceptance_radius"), 50.0f);

	// Parse target location
	FVector Target = FVector::ZeroVector;
	if (Arguments->HasField(TEXT("target")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (Arguments->TryGetArrayField(TEXT("target"), TargetArray) && TargetArray->Num() >= 3)
		{
			Target.X = (*TargetArray)[0]->AsNumber();
			Target.Y = (*TargetArray)[1]->AsNumber();
			Target.Z = (*TargetArray)[2]->AsNumber();
		}
	}
	else
	{
		return FBridgeToolResult::Error(TEXT("target location is required for move-to action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FBridgeToolResult::Error(TEXT("Player has no pawn"));
	}

	// Use simple move to location (AI navigation)
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

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-input: Moving to [%.0f, %.0f, %.0f]"), Target.X, Target.Y, Target.Z);

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UPieInputTool::ExecuteLookAt(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
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
	FString TargetActorName = GetStringArgOrDefault(Arguments, TEXT("target_actor"));

	if (!TargetActorName.IsEmpty())
	{
		// Find target actor
		for (TActorIterator<AActor> It(PIEWorld); It; ++It)
		{
			if ((*It)->GetName().Equals(TargetActorName, ESearchCase::IgnoreCase))
			{
				Target = (*It)->GetActorLocation();
				break;
			}
		}
		if (Target.IsZero())
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target actor not found: %s"), *TargetActorName));
		}
	}
	else if (Arguments->HasField(TEXT("target")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (Arguments->TryGetArrayField(TEXT("target"), TargetArray) && TargetArray->Num() >= 3)
		{
			Target.X = (*TargetArray)[0]->AsNumber();
			Target.Y = (*TargetArray)[1]->AsNumber();
			Target.Z = (*TargetArray)[2]->AsNumber();
		}
	}
	else
	{
		return FBridgeToolResult::Error(TEXT("Either target or target_actor is required for look-at action"));
	}

	// Calculate rotation to face target
	FVector Direction = Target - Pawn->GetActorLocation();
	Direction.Z = 0; // Keep level
	FRotator NewRotation = Direction.Rotation();

	// Set the rotation
	PC->SetControlRotation(NewRotation);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> RotArr;
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Pitch)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Yaw)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Roll)));
	Result->SetArrayField(TEXT("rotation"), RotArr);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("pie-input: Looking at yaw=%.1f"), NewRotation.Yaw);

	return FBridgeToolResult::Json(Result);
}

UWorld* UPieInputTool::GetPIEWorld() const
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

APlayerController* UPieInputTool::GetPlayerController(UWorld* PIEWorld, int32 PlayerIndex) const
{
	int32 CurrentIndex = 0;
	for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
	{
		if (CurrentIndex == PlayerIndex)
		{
			return It->Get();
		}
		CurrentIndex++;
	}
	return nullptr;
}
