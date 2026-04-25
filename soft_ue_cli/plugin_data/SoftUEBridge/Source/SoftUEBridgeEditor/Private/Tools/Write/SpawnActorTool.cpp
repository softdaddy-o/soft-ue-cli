// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/EditorSpawnActorTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraActor.h"
#include "Engine/TriggerBox.h"
#include "Engine/TriggerSphere.h"
#include "ScopedTransaction.h"

FString UEditorSpawnActorTool::GetToolDescription() const
{
	return TEXT("Spawn an actor in the editor level or PIE world. Supports native classes and Blueprint actors. "
		"Use 'world': 'editor' (default, with undo support) or 'pie' (runtime path, no transaction).");
}

TMap<FString, FBridgeSchemaProperty> UEditorSpawnActorTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema = Super::GetInputSchema();

	// Add label param (editor-world only)
	FBridgeSchemaProperty Label;
	Label.Type = TEXT("string");
	Label.Description = TEXT("Actor label in the World Outliner (editor world only)");
	Label.bRequired = false;
	Schema.Add(TEXT("label"), Label);

	// Override world description to clarify editor default
	FBridgeSchemaProperty WorldParam;
	WorldParam.Type = TEXT("string");
	WorldParam.Description = TEXT("Target world: 'editor' (default, with undo) or 'pie' (runtime path)");
	WorldParam.bRequired = false;
	Schema.Add(TEXT("world"), WorldParam);

	return Schema;
}

FBridgeToolResult UEditorSpawnActorTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString WorldParam = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));

	// PIE and game worlds: delegate to runtime base (no transaction, no label, no dirty)
	if (!WorldParam.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
	{
		return Super::Execute(Arguments, Context);
	}

	// --- Editor world path ---
	const FString ActorClass = GetStringArgOrDefault(Arguments, TEXT("actor_class"));
	const FString Label      = GetStringArgOrDefault(Arguments, TEXT("label"));

	if (ActorClass.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_class is required"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No editor world available. Open a level first."));
	}

	// Parse location
	FVector Location(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* LocationArray;
	if (Arguments->TryGetArrayField(TEXT("location"), LocationArray) && LocationArray->Num() >= 3)
	{
		Location.X = (*LocationArray)[0]->AsNumber();
		Location.Y = (*LocationArray)[1]->AsNumber();
		Location.Z = (*LocationArray)[2]->AsNumber();
	}

	// Parse rotation
	FRotator Rotation(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* RotationArray;
	if (Arguments->TryGetArrayField(TEXT("rotation"), RotationArray) && RotationArray->Num() >= 3)
	{
		Rotation.Pitch = (*RotationArray)[0]->AsNumber();
		Rotation.Yaw   = (*RotationArray)[1]->AsNumber();
		Rotation.Roll  = (*RotationArray)[2]->AsNumber();
	}

	// Resolve class
	UClass* SpawnClass = nullptr;
	if (ActorClass.StartsWith(TEXT("/")))
	{
		FString LoadError;
		UBlueprint* Blueprint = FBridgeAssetModifier::LoadAssetByPath<UBlueprint>(ActorClass, LoadError);
		if (Blueprint && Blueprint->GeneratedClass)
		{
			SpawnClass = Blueprint->GeneratedClass;
		}
		else
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Blueprint not found or invalid: %s"), *ActorClass));
		}
	}
	else
	{
		// Named native class shortcuts
		static const TMap<FString, UClass*> NativeClasses = {
			{ TEXT("PointLight"),      APointLight::StaticClass()      },
			{ TEXT("SpotLight"),       ASpotLight::StaticClass()       },
			{ TEXT("DirectionalLight"),ADirectionalLight::StaticClass()},
			{ TEXT("StaticMeshActor"), AStaticMeshActor::StaticClass() },
			{ TEXT("CameraActor"),     ACameraActor::StaticClass()     },
			{ TEXT("TriggerBox"),      ATriggerBox::StaticClass()      },
			{ TEXT("TriggerSphere"),   ATriggerSphere::StaticClass()   },
		};

		if (UClass* const* Found = NativeClasses.Find(ActorClass))
		{
			SpawnClass = *Found;
		}
		else
		{
			SpawnClass = FindFirstObject<UClass>(*ActorClass, EFindFirstObjectOptions::ExactClass);
			if (!SpawnClass)
			{
				SpawnClass = FindFirstObject<UClass>(*(TEXT("A") + ActorClass), EFindFirstObjectOptions::ExactClass);
			}
		}
	}

	if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "SpawnActor", "Spawn {0}"), FText::FromString(ActorClass)));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedActor = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, SpawnParams);
	if (!SpawnedActor)
	{
		return FBridgeToolResult::Error(TEXT("SpawnActor returned null"));
	}

	if (!Label.IsEmpty())
	{
		SpawnedActor->SetActorLabel(Label);
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_name"), SpawnedActor->GetName());
	Result->SetStringField(TEXT("actor_label"), SpawnedActor->GetActorLabel());
	Result->SetStringField(TEXT("actor_class"), SpawnClass->GetName());
	Result->SetStringField(TEXT("world"), TEXT("editor"));
	Result->SetBoolField(TEXT("needs_save"), true);

	TArray<TSharedPtr<FJsonValue>> LocationJson;
	LocationJson.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationJson.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationJson.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationJson);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("spawn-actor: spawned %s in editor world"), *SpawnedActor->GetName());
	return FBridgeToolResult::Json(Result);
}
