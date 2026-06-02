// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimRepointReferencesTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty AnimRepointSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

bool CheckoutAnimationAssetIfRequested(UAnimationAsset* Asset, bool bCheckout, FString& OutError)
{
	if (!bCheckout || !Asset)
	{
		return true;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Animation asset has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	return FBridgeAssetModifier::CheckoutFile(PackageFileName, OutError);
}

bool LoadReplacementMap(
	const TSharedPtr<FJsonObject>& Arguments,
	TMap<UAnimationAsset*, UAnimationAsset*>& OutReplacementMap,
	TSharedPtr<FJsonObject>& OutReplacementPaths,
	FString& OutError)
{
	const TSharedPtr<FJsonObject>* ReplacementObject = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(TEXT("replacement_map"), ReplacementObject) || !ReplacementObject)
	{
		OutError = TEXT("anim-repoint-references: replacement_map is required");
		return false;
	}

	OutReplacementPaths = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ReplacementObject)->Values)
	{
		const FString OldPath = Pair.Key;
		const FString NewPath = Pair.Value.IsValid() ? Pair.Value->AsString() : TEXT("");
		if (OldPath.IsEmpty() || NewPath.IsEmpty())
		{
			OutError = TEXT("anim-repoint-references: replacement_map entries must be old_path: new_path strings");
			return false;
		}

		FString LoadError;
		UAnimationAsset* OldAsset = FBridgeAssetModifier::LoadAssetByPath<UAnimationAsset>(OldPath, LoadError);
		if (!OldAsset)
		{
			OutError = FString::Printf(TEXT("Failed to load replacement source '%s': %s"), *OldPath, *LoadError);
			return false;
		}
		UAnimationAsset* NewAsset = FBridgeAssetModifier::LoadAssetByPath<UAnimationAsset>(NewPath, LoadError);
		if (!NewAsset)
		{
			OutError = FString::Printf(TEXT("Failed to load replacement target '%s': %s"), *NewPath, *LoadError);
			return false;
		}

		OutReplacementMap.Add(OldAsset, NewAsset);
		OutReplacementPaths->SetStringField(OldAsset->GetPathName(), NewAsset->GetPathName());
	}

	if (OutReplacementMap.IsEmpty())
	{
		OutError = TEXT("anim-repoint-references: replacement_map must contain at least one entry");
		return false;
	}
	return true;
}

TSet<UAnimationAsset*> CollectDirectReferredAnimations(UAnimationAsset* Asset)
{
	TArray<UAnimationAsset*> Referred;
	if (Asset)
	{
		Asset->GetAllAnimationSequencesReferred(Referred, false);
	}

	TSet<UAnimationAsset*> References;
	for (UAnimationAsset* Reference : Referred)
	{
		References.Add(Reference);
	}
	return References;
}

TArray<TSharedPtr<FJsonValue>> ReferencesToJson(const TSet<UAnimationAsset*>& References)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const UAnimationAsset* Reference : References)
	{
		Values.Add(MakeShared<FJsonValueString>(Reference ? Reference->GetPathName() : TEXT("")));
	}
	return Values;
}
}

FString UAnimRepointReferencesTool::GetToolDescription() const
{
	return TEXT("Safely repoint AnimSequence references inside AnimMontage and BlendSpace assets using Unreal animation APIs.");
}

TMap<FString, FBridgeSchemaProperty> UAnimRepointReferencesTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_paths"), AnimRepointSchemaProperty(TEXT("array"), TEXT("AnimMontage or BlendSpace asset paths to update"), true));
	Schema.Add(TEXT("replacement_map"), AnimRepointSchemaProperty(TEXT("object"), TEXT("Map of old AnimSequence asset path to new AnimSequence asset path"), true));
	Schema.Add(TEXT("target_skeleton"), AnimRepointSchemaProperty(TEXT("string"), TEXT("Optional target skeleton asset path to assign to changed assets")));
	Schema.Add(TEXT("save"), AnimRepointSchemaProperty(TEXT("boolean"), TEXT("Save each changed asset after mutation")));
	Schema.Add(TEXT("checkout"), AnimRepointSchemaProperty(TEXT("boolean"), TEXT("Checkout each changed asset before mutation")));
	return Schema;
}

TArray<FString> UAnimRepointReferencesTool::GetRequiredParams() const
{
	return { TEXT("asset_paths"), TEXT("replacement_map") };
}

FBridgeToolResult UAnimRepointReferencesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathValues = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("asset_paths"), AssetPathValues) || !AssetPathValues || AssetPathValues->IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("anim-repoint-references: asset_paths is required"));
	}

	TMap<UAnimationAsset*, UAnimationAsset*> ReplacementMap;
	TSharedPtr<FJsonObject> ReplacementPaths;
	FString Error;
	if (!LoadReplacementMap(Arguments, ReplacementMap, ReplacementPaths, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	USkeleton* TargetSkeleton = nullptr;
	const FString TargetSkeletonPath = GetStringArgOrDefault(Arguments, TEXT("target_skeleton"), TEXT(""));
	if (!TargetSkeletonPath.IsEmpty())
	{
		TargetSkeleton = FBridgeAssetModifier::LoadAssetByPath<USkeleton>(TargetSkeletonPath, Error);
		if (!TargetSkeleton)
		{
			return FBridgeToolResult::Error(Error);
		}
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Repoint Animation References")));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AssetResults;
	int32 ChangedAssetCount = 0;
	int32 ReplacedReferenceCount = 0;

	for (const TSharedPtr<FJsonValue>& Value : *AssetPathValues)
	{
		const FString AssetPath = Value.IsValid() ? Value->AsString() : TEXT("");
		TSharedPtr<FJsonObject> AssetJson = MakeShared<FJsonObject>();
		AssetJson->SetStringField(TEXT("asset_path"), AssetPath);

		FString LoadError;
		UAnimationAsset* Asset = FBridgeAssetModifier::LoadAssetByPath<UAnimationAsset>(AssetPath, LoadError);
		if (!Asset)
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), LoadError);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		if (!Asset->IsA<UAnimMontage>() && !Asset->IsA<UBlendSpace>())
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), TEXT("Asset is not an AnimMontage or BlendSpace"));
			AssetJson->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		const TSet<UAnimationAsset*> BeforeReferences = CollectDirectReferredAnimations(Asset);
		int32 MatchedReferences = 0;
		for (const TPair<UAnimationAsset*, UAnimationAsset*>& Replacement : ReplacementMap)
		{
			if (BeforeReferences.Contains(Replacement.Key))
			{
				++MatchedReferences;
			}
		}

		if (MatchedReferences == 0 && !TargetSkeleton)
		{
			AssetJson->SetBoolField(TEXT("success"), true);
			AssetJson->SetBoolField(TEXT("changed"), false);
			AssetJson->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
			AssetJson->SetArrayField(TEXT("references_before"), ReferencesToJson(BeforeReferences));
			AssetJson->SetNumberField(TEXT("matched_reference_count"), 0);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		if (!CheckoutAnimationAssetIfRequested(Asset, bCheckout, Error))
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), Error);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		FBridgeAssetModifier::MarkModified(Asset);
		Asset->ReplaceReferredAnimations(ReplacementMap);
		if (TargetSkeleton)
		{
			Asset->ReplaceSkeleton(TargetSkeleton, false);
		}
		Asset->PostEditChange();
		FBridgeAssetModifier::MarkPackageDirty(Asset);

		if (bSave && !FBridgeAssetModifier::SaveAsset(Asset, false, Error))
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), Error);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		const TSet<UAnimationAsset*> AfterReferences = CollectDirectReferredAnimations(Asset);
		++ChangedAssetCount;
		ReplacedReferenceCount += MatchedReferences;

		AssetJson->SetBoolField(TEXT("success"), true);
		AssetJson->SetBoolField(TEXT("changed"), true);
		AssetJson->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
		AssetJson->SetStringField(TEXT("skeleton"), Asset->GetSkeleton() ? Asset->GetSkeleton()->GetPathName() : TEXT(""));
		AssetJson->SetNumberField(TEXT("matched_reference_count"), MatchedReferences);
		AssetJson->SetArrayField(TEXT("references_before"), ReferencesToJson(BeforeReferences));
		AssetJson->SetArrayField(TEXT("references_after"), ReferencesToJson(AfterReferences));
		AssetJson->SetBoolField(TEXT("saved"), bSave);
		AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("changed_asset_count"), ChangedAssetCount);
	Result->SetNumberField(TEXT("replaced_reference_count"), ReplacedReferenceCount);
	Result->SetObjectField(TEXT("replacement_map"), ReplacementPaths);
	Result->SetArrayField(TEXT("assets"), AssetResults);
	if (TargetSkeleton)
	{
		Result->SetStringField(TEXT("target_skeleton"), TargetSkeleton->GetPathName());
	}
	return FBridgeToolResult::Json(Result);
}
