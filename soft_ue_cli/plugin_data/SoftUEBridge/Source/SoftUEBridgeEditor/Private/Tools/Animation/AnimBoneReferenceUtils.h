// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeleton;
class FJsonObject;
class FJsonValue;

namespace SoftUE::AnimBoneReferenceUtils
{
struct FBoneReferenceRecord
{
	FString OwnerPath;
	FString PropertyPath;
	FName BoneName;
};

struct FBoneReferenceChange
{
	FString OwnerPath;
	FString PropertyPath;
	FName OldBoneName;
	FName NewBoneName;
};

bool LoadBoneMap(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& ToolName,
	TMap<FName, FName>& OutBoneMap,
	TSharedPtr<FJsonObject>& OutBoneMapJson,
	FString& OutError);

void CollectBoneReferences(UObject* RootObject, TArray<FBoneReferenceRecord>& OutRecords);

int32 RemapBoneReferences(
	UObject* RootObject,
	const TMap<FName, FName>& BoneMap,
	TArray<FBoneReferenceChange>& OutChanges);

void CollectSkeletonReferences(UObject* RootObject, TArray<TSharedPtr<FJsonValue>>& OutSkeletons);

int32 SetSkeletonReferences(
	UObject* RootObject,
	USkeleton* TargetSkeleton,
	TArray<TSharedPtr<FJsonValue>>& OutChanges);

TArray<TSharedPtr<FJsonValue>> BoneRecordsToJson(const TArray<FBoneReferenceRecord>& Records);
TArray<TSharedPtr<FJsonValue>> BoneChangesToJson(const TArray<FBoneReferenceChange>& Changes);
TArray<TSharedPtr<FJsonValue>> UniqueBoneNamesToJson(const TArray<FBoneReferenceRecord>& Records);

bool CheckoutObjectPackageIfRequested(UObject* Object, bool bCheckout, FString& OutError);
bool LooksLikePoseSearchSchema(UObject* Object);
}
