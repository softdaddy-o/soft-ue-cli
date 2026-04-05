// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/BatchDeleteActorTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"

FString UBatchDeleteActorTool::GetToolDescription() const
{
	return TEXT("Batch-delete multiple actors from the editor level within a single undo transaction. "
		"Actors are identified by name or label.");
}

TMap<FString, FBridgeSchemaProperty> UBatchDeleteActorTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Actors;
	Actors.Type = TEXT("array");
	Actors.Description = TEXT("Array of actor names or labels (strings) to delete");
	Actors.bRequired = true;
	Schema.Add(TEXT("actors"), Actors);

	return Schema;
}

TArray<FString> UBatchDeleteActorTool::GetRequiredParams() const
{
	return { TEXT("actors") };
}

AActor* UBatchDeleteActorTool::FindActorByNameOrLabel(UWorld* World, const FString& ActorName) const
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

FBridgeToolResult UBatchDeleteActorTool::Execute(
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
		FText::Format(NSLOCTEXT("SoftUEBridge", "BatchDelete", "Batch delete {0} actors"),
			FText::AsNumber(ActorsArray->Num())));

	TArray<TSharedPtr<FJsonValue>> DeletedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	for (int32 i = 0; i < ActorsArray->Num(); ++i)
	{
		FString ActorName = (*ActorsArray)[i]->AsString();
		if (ActorName.IsEmpty())
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("error"), TEXT("Empty or non-string entry"));
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

		FString DeletedName = Actor->GetName();
		FString DeletedLabel = Actor->GetActorLabel();

		bool bDestroyed = World->EditorDestroyActor(Actor, false);
		if (bDestroyed)
		{
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetNumberField(TEXT("index"), i);
			Entry->SetStringField(TEXT("actor_name"), DeletedName);
			Entry->SetStringField(TEXT("actor_label"), DeletedLabel);
			DeletedArray.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
		else
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetNumberField(TEXT("index"), i);
			Fail->SetStringField(TEXT("actor"), ActorName);
			Fail->SetStringField(TEXT("error"), TEXT("Actor found but could not be destroyed"));
			NotFoundArray.Add(MakeShareable(new FJsonValueObject(Fail)));
		}
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), NotFoundArray.Num() == 0);
	Result->SetNumberField(TEXT("deleted_count"), DeletedArray.Num());
	Result->SetNumberField(TEXT("not_found_count"), NotFoundArray.Num());
	Result->SetArrayField(TEXT("deleted"), DeletedArray);
	Result->SetArrayField(TEXT("not_found"), NotFoundArray);
	Result->SetBoolField(TEXT("needs_save"), DeletedArray.Num() > 0);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("batch-delete-actors: deleted %d, not found %d"),
		DeletedArray.Num(), NotFoundArray.Num());
	return FBridgeToolResult::Json(Result);
}
