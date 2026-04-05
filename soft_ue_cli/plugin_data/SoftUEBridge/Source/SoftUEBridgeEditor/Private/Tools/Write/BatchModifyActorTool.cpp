// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/BatchModifyActorTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"

FString UBatchModifyActorTool::GetToolDescription() const
{
	return TEXT("Batch-modify transforms of existing actors in the editor level. "
		"Each entry identifies an actor by name or label and sets location, rotation, and/or scale. "
		"All modifications happen in a single undo transaction.");
}

TMap<FString, FBridgeSchemaProperty> UBatchModifyActorTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Modifications;
	Modifications.Type = TEXT("array");
	Modifications.Description = TEXT("Array of modification entries. Each entry: {actor (string, required, name or label), "
		"location ([x,y,z], optional), rotation ([pitch,yaw,roll], optional), "
		"scale ([x,y,z], optional)}. Only specified fields are changed.");
	Modifications.bRequired = true;
	Schema.Add(TEXT("modifications"), Modifications);

	return Schema;
}

TArray<FString> UBatchModifyActorTool::GetRequiredParams() const
{
	return { TEXT("modifications") };
}

AActor* UBatchModifyActorTool::FindActorByNameOrLabel(UWorld* World, const FString& ActorName) const
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase) ||
			Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}
	return nullptr;
}

FBridgeToolResult UBatchModifyActorTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* ModArray;
	if (!Arguments->TryGetArrayField(TEXT("modifications"), ModArray))
	{
		return FBridgeToolResult::Error(TEXT("modifications array is required"));
	}

	if (ModArray->Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("modifications array is empty"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No editor world available. Open a level first."));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "BatchModify", "Batch modify {0} actors"),
			FText::AsNumber(ModArray->Num())));

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	for (int32 i = 0; i < ModArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!(*ModArray)[i]->TryGetObject(EntryObj))
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("Entry is not a JSON object"));
			NotFoundArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		FString ActorName;
		if (!(*EntryObj)->TryGetStringField(TEXT("actor"), ActorName))
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("actor field is required"));
			NotFoundArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		AActor* Actor = FindActorByNameOrLabel(World, ActorName);
		if (!Actor)
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("actor"), ActorName);
			Fail->SetStringField(TEXT("error"), TEXT("Actor not found"));
			NotFoundArray.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		Actor->Modify();

		// Apply location if provided
		const TArray<TSharedPtr<FJsonValue>>* LocArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("location"), LocArr) && LocArr->Num() >= 3)
		{
			FVector NewLocation;
			NewLocation.X = (*LocArr)[0]->AsNumber();
			NewLocation.Y = (*LocArr)[1]->AsNumber();
			NewLocation.Z = (*LocArr)[2]->AsNumber();
			Actor->SetActorLocation(NewLocation);
		}

		// Apply rotation if provided
		const TArray<TSharedPtr<FJsonValue>>* RotArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() >= 3)
		{
			FRotator NewRotation;
			NewRotation.Pitch = (*RotArr)[0]->AsNumber();
			NewRotation.Yaw   = (*RotArr)[1]->AsNumber();
			NewRotation.Roll  = (*RotArr)[2]->AsNumber();
			Actor->SetActorRotation(NewRotation);
		}

		// Apply scale if provided
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
		if ((*EntryObj)->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr->Num() >= 3)
		{
			FVector NewScale;
			NewScale.X = (*ScaleArr)[0]->AsNumber();
			NewScale.Y = (*ScaleArr)[1]->AsNumber();
			NewScale.Z = (*ScaleArr)[2]->AsNumber();
			Actor->SetActorScale3D(NewScale);
		}

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("actor_name"), Actor->GetName());
		Entry->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
		ModifiedArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), NotFoundArray.Num() == 0);
	Result->SetNumberField(TEXT("modified_count"), ModifiedArray.Num());
	Result->SetNumberField(TEXT("not_found_count"), NotFoundArray.Num());
	Result->SetArrayField(TEXT("modified"), ModifiedArray);
	Result->SetArrayField(TEXT("not_found"), NotFoundArray);
	Result->SetBoolField(TEXT("needs_save"), ModifiedArray.Num() > 0);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("batch-modify-actors: modified %d, not found %d"),
		ModifiedArray.Num(), NotFoundArray.Num());
	return FBridgeToolResult::Json(Result);
}
