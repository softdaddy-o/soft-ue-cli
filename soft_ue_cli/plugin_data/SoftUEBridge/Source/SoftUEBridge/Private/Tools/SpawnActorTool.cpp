// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/SpawnActorTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraActor.h"

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(USpawnActorTool)
#endif

FString USpawnActorTool::GetToolDescription() const
{
	return TEXT("Spawn an actor in the game world. Supports native class names (e.g. 'StaticMeshActor') or full Blueprint asset paths (e.g. '/Game/BP_Enemy').");
}

TMap<FString, FBridgeSchemaProperty> USpawnActorTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	FBridgeSchemaProperty ActorClass;
	ActorClass.Type = TEXT("string");
	ActorClass.Description = TEXT("Class name or Blueprint path");
	ActorClass.bRequired = true;
	S.Add(TEXT("actor_class"), ActorClass);

	FBridgeSchemaProperty Loc;
	Loc.Type = TEXT("array");
	Loc.ItemsType = TEXT("number");
	Loc.Description = TEXT("Location [x, y, z] in cm");
	S.Add(TEXT("location"), Loc);

	FBridgeSchemaProperty Rot;
	Rot.Type = TEXT("array");
	Rot.ItemsType = TEXT("number");
	Rot.Description = TEXT("Rotation [pitch, yaw, roll] in degrees");
	S.Add(TEXT("rotation"), Rot);

	FBridgeSchemaProperty World;
	World.Type = TEXT("string");
	World.Description = TEXT("World context: 'editor' (editor scene), 'pie' (Play-In-Editor), 'game' (packaged build only). Omit to use the first available world.");
	S.Add(TEXT("world"), World);

	return S;
}

FBridgeToolResult USpawnActorTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	FString ActorClass = GetStringArgOrDefault(Args, TEXT("actor_class"));
	if (ActorClass.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_class is required"));
	}

	UWorld* World = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No world available. Specify 'world': 'editor', 'pie', or 'game'."));
	}

	// Parse location
	FVector Location(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (Args->TryGetArrayField(TEXT("location"), LocArr) && LocArr->Num() >= 3)
	{
		Location.X = (*LocArr)[0]->AsNumber();
		Location.Y = (*LocArr)[1]->AsNumber();
		Location.Z = (*LocArr)[2]->AsNumber();
	}

	// Parse rotation
	FRotator Rotation(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	if (Args->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() >= 3)
	{
		Rotation.Pitch = (*RotArr)[0]->AsNumber();
		Rotation.Yaw   = (*RotArr)[1]->AsNumber();
		Rotation.Roll  = (*RotArr)[2]->AsNumber();
	}

	// Resolve class
	UClass* SpawnClass = nullptr;

	if (ActorClass.StartsWith(TEXT("/")))
	{
		// Blueprint asset path — load the generated class
		UClass* BPClass = LoadClass<AActor>(nullptr, *ActorClass);
		if (!BPClass)
		{
			// Try appending _C suffix (compiled BP class)
			BPClass = LoadClass<AActor>(nullptr, *(ActorClass + TEXT("_C")));
		}
		SpawnClass = BPClass;
	}
	else
	{
		// Native class by name (try with 'A' prefix)
		SpawnClass = FindFirstObject<UClass>(*ActorClass, EFindFirstObjectOptions::ExactClass);
		if (!SpawnClass)
		{
			SpawnClass = FindFirstObject<UClass>(*(TEXT("A") + ActorClass), EFindFirstObjectOptions::ExactClass);
		}
	}

	if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Spawned = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, Params);
	if (!Spawned)
	{
		return FBridgeToolResult::Error(TEXT("SpawnActor returned null"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("actor_name"), Spawned->GetName());
	Result->SetStringField(TEXT("actor_class"), SpawnClass->GetName());

	TArray<TSharedPtr<FJsonValue>> LocJson;
	LocJson.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocJson.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocJson.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocJson);

	UE_LOG(LogSoftUEBridge, Log, TEXT("spawn-actor: spawned %s"), *Spawned->GetName());
	return FBridgeToolResult::Json(Result);
}
