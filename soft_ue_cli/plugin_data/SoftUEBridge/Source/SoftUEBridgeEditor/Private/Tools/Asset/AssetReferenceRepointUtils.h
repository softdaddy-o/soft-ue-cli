// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/SoftObjectPath.h"

namespace SoftUE::AssetReferenceRepointUtils
{
struct FAssetReferenceChange
{
	FString OwnerPath;
	FString PropertyPath;
	FString OldReference;
	FString NewReference;
	FString Kind;
};

struct FReplacementMap
{
	TMap<UObject*, UObject*> HardObjectMap;
	TMap<FString, FSoftObjectPath> SoftObjectPathMap;
	TSharedPtr<FJsonObject> ReplacementPaths;
};

bool LoadReplacementMap(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& FieldName,
	const FString& ToolName,
	FReplacementMap& OutReplacementMap,
	FString& OutError);

int32 RepointObjectReferences(
	UObject* RootObject,
	const FReplacementMap& ReplacementMap,
	TArray<FAssetReferenceChange>& OutChanges);

int32 SetNamedObjectReferences(
	UObject* RootObject,
	const TSet<FName>& PropertyNames,
	UObject* TargetObject,
	TArray<FAssetReferenceChange>& OutChanges);

TArray<TSharedPtr<FJsonValue>> ChangesToJson(const TArray<FAssetReferenceChange>& Changes);
bool CheckoutObjectPackageIfRequested(UObject* Object, bool bCheckout, FString& OutError);
bool LooksLikePoseSearchDatabase(UObject* Object);
}
