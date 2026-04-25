// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/BatchSpawnActorTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Camera/CameraActor.h"
#include "Engine/TriggerBox.h"
#include "Engine/TriggerSphere.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ScopedTransaction.h"

FString UBatchSpawnActorTool::GetToolDescription() const
{
	return TEXT("Batch-spawn multiple actors in the editor level within a single undo transaction. "
		"Each entry specifies actor_class, optional mesh, location, rotation, scale, and label.");
}

TMap<FString, FBridgeSchemaProperty> UBatchSpawnActorTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Actors;
	Actors.Type = TEXT("array");
	Actors.Description = TEXT("Array of actor entries. Each entry: {class (string, required), "
		"mesh (string, optional asset path for StaticMeshActor), "
		"location ([x,y,z], optional), rotation ([pitch,yaw,roll], optional), "
		"scale ([x,y,z], optional, default [1,1,1]), label (string, optional)}");
	Actors.bRequired = true;
	Schema.Add(TEXT("actors"), Actors);

	return Schema;
}

TArray<FString> UBatchSpawnActorTool::GetRequiredParams() const
{
	return { TEXT("actors") };
}

UClass* UBatchSpawnActorTool::ResolveActorClass(const FString& ActorClass, FString& OutError) const
{
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
			OutError = FString::Printf(TEXT("Blueprint not found: %s"), *ActorClass);
			return nullptr;
		}
	}
	else
	{
		static const TMap<FString, UClass*> NativeClasses = {
			{ TEXT("PointLight"),       APointLight::StaticClass()       },
			{ TEXT("SpotLight"),        ASpotLight::StaticClass()        },
			{ TEXT("DirectionalLight"), ADirectionalLight::StaticClass() },
			{ TEXT("StaticMeshActor"),  AStaticMeshActor::StaticClass()  },
			{ TEXT("CameraActor"),      ACameraActor::StaticClass()      },
			{ TEXT("TriggerBox"),       ATriggerBox::StaticClass()        },
			{ TEXT("TriggerSphere"),    ATriggerSphere::StaticClass()     },
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
		OutError = FString::Printf(TEXT("Actor class not found: %s"), *ActorClass);
		return nullptr;
	}

	return SpawnClass;
}

FBridgeToolResult UBatchSpawnActorTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* ActorsArray;
	if (!Arguments->TryGetArrayField(TEXT("actors"), ActorsArray))
	{
		return FBridgeToolResult::Error(TEXT("actors array is required"));
	}

	if (ActorsArray->Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("actors array is empty"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No editor world available. Open a level first."));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "BatchSpawn", "Batch spawn {0} actors"),
			FText::AsNumber(ActorsArray->Num())));

	TArray<TSharedPtr<FJsonValue>> SpawnedArray;
	TArray<TSharedPtr<FJsonValue>> FailedArray;

	for (int32 i = 0; i < ActorsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!(*ActorsArray)[i]->TryGetObject(EntryObj))
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("Entry is not a JSON object"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		FString ActorClass;
		if (!(*EntryObj)->TryGetStringField(TEXT("class"), ActorClass))
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("class is required"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		// Resolve class
		FString ClassError;
		UClass* SpawnClass = ResolveActorClass(ActorClass, ClassError);
		if (!SpawnClass)
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), ClassError);
			FailedArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		// Parse location
		FVector Location(0, 0, 0);
		const TArray<TSharedPtr<FJsonValue>>* LocArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("location"), LocArr) && LocArr->Num() >= 3)
		{
			Location.X = (*LocArr)[0]->AsNumber();
			Location.Y = (*LocArr)[1]->AsNumber();
			Location.Z = (*LocArr)[2]->AsNumber();
		}

		// Parse rotation
		FRotator Rotation(0, 0, 0);
		const TArray<TSharedPtr<FJsonValue>>* RotArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() >= 3)
		{
			Rotation.Pitch = (*RotArr)[0]->AsNumber();
			Rotation.Yaw   = (*RotArr)[1]->AsNumber();
			Rotation.Roll  = (*RotArr)[2]->AsNumber();
		}

		// Parse scale
		FVector Scale(1, 1, 1);
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr->Num() >= 3)
		{
			Scale.X = (*ScaleArr)[0]->AsNumber();
			Scale.Y = (*ScaleArr)[1]->AsNumber();
			Scale.Z = (*ScaleArr)[2]->AsNumber();
		}

		FString Label;
		(*EntryObj)->TryGetStringField(TEXT("label"), Label);

		FString MeshPath;
		(*EntryObj)->TryGetStringField(TEXT("mesh"), MeshPath);

		// Spawn
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, SpawnParams);
		if (!SpawnedActor)
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("SpawnActor returned null"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		// Set scale
		SpawnedActor->SetActorScale3D(Scale);

		// Set label
		if (!Label.IsEmpty())
		{
			SpawnedActor->SetActorLabel(Label);
		}

		// Set static mesh if provided
		FString MeshWarning;
		if (!MeshPath.IsEmpty())
		{
			if (AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(SpawnedActor))
			{
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
				if (Mesh && SMActor->GetStaticMeshComponent())
				{
					SMActor->GetStaticMeshComponent()->Modify();
					SMActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				}
				else if (!Mesh)
				{
					MeshWarning = FString::Printf(TEXT("Mesh not found: %s"), *MeshPath);
				}
			}
			else
			{
				MeshWarning = TEXT("mesh field ignored: actor is not a StaticMeshActor");
			}
		}

		// Build result entry
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetNumberField(TEXT("index"), i);
		if (!MeshWarning.IsEmpty())
		{
			Entry->SetStringField(TEXT("mesh_warning"), MeshWarning);
		}
		Entry->SetStringField(TEXT("actor_name"), SpawnedActor->GetName());
		Entry->SetStringField(TEXT("actor_label"), SpawnedActor->GetActorLabel());
		Entry->SetStringField(TEXT("actor_class"), SpawnClass->GetName());
		SpawnedArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), FailedArray.Num() == 0);
	Result->SetNumberField(TEXT("spawned_count"), SpawnedArray.Num());
	Result->SetNumberField(TEXT("failed_count"), FailedArray.Num());
	Result->SetArrayField(TEXT("spawned"), SpawnedArray);
	Result->SetArrayField(TEXT("failed"), FailedArray);
	Result->SetBoolField(TEXT("needs_save"), SpawnedArray.Num() > 0);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("batch-spawn-actors: spawned %d, failed %d"),
		SpawnedArray.Num(), FailedArray.Num());
	return FBridgeToolResult::Json(Result);
}
