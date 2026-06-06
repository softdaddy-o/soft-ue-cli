// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/AssetRepointReferencesTool.h"

#include "Tools/Asset/AssetReferenceRepointUtils.h"
#include "Utils/BridgeAssetModifier.h"

#include "Dom/JsonObject.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty AssetRepointSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}
}

FString UAssetRepointReferencesTool::GetToolDescription() const
{
	return TEXT("Repoint nested hard and soft object references inside arbitrary assets using reflected property traversal.");
}

TMap<FString, FBridgeSchemaProperty> UAssetRepointReferencesTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_paths"), AssetRepointSchemaProperty(TEXT("array"), TEXT("Asset paths to update"), true));
	Schema.Add(TEXT("replacement_map"), AssetRepointSchemaProperty(TEXT("object"), TEXT("Map of old asset path to new asset path"), true));
	Schema.Add(TEXT("save"), AssetRepointSchemaProperty(TEXT("boolean"), TEXT("Save each changed asset after mutation")));
	Schema.Add(TEXT("checkout"), AssetRepointSchemaProperty(TEXT("boolean"), TEXT("Checkout each changed asset before mutation")));
	return Schema;
}

TArray<FString> UAssetRepointReferencesTool::GetRequiredParams() const
{
	return { TEXT("asset_paths"), TEXT("replacement_map") };
}

FBridgeToolResult UAssetRepointReferencesTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathValues = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("asset_paths"), AssetPathValues) || !AssetPathValues || AssetPathValues->IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset-repoint-references: asset_paths is required"));
	}

	SoftUE::AssetReferenceRepointUtils::FReplacementMap ReplacementMap;
	FString Error;
	if (!SoftUE::AssetReferenceRepointUtils::LoadReplacementMap(
		Arguments,
		TEXT("replacement_map"),
		TEXT("asset-repoint-references"),
		ReplacementMap,
		Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Repoint Nested Asset References")));

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
		UObject* Asset = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset)
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), LoadError);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		if (!SoftUE::AssetReferenceRepointUtils::CheckoutObjectPackageIfRequested(Asset, bCheckout, Error))
		{
			AssetJson->SetBoolField(TEXT("success"), false);
			AssetJson->SetStringField(TEXT("error"), Error);
			AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
			continue;
		}

		TArray<SoftUE::AssetReferenceRepointUtils::FAssetReferenceChange> Changes;
		const int32 BeforeCount = Changes.Num();
		SoftUE::AssetReferenceRepointUtils::RepointObjectReferences(Asset, ReplacementMap, Changes);
		const bool bChanged = Changes.Num() > BeforeCount;

		if (bChanged)
		{
			FBridgeAssetModifier::MarkModified(Asset);
			Asset->PostEditChange();
			FBridgeAssetModifier::MarkPackageDirty(Asset);
			if (bSave && !FBridgeAssetModifier::SaveAsset(Asset, false, Error))
			{
				AssetJson->SetBoolField(TEXT("success"), false);
				AssetJson->SetStringField(TEXT("error"), Error);
				AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
				continue;
			}
			++ChangedAssetCount;
			ReplacedReferenceCount += Changes.Num();
		}

		AssetJson->SetBoolField(TEXT("success"), true);
		AssetJson->SetBoolField(TEXT("changed"), bChanged);
		AssetJson->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
		AssetJson->SetNumberField(TEXT("changed_reference_count"), Changes.Num());
		AssetJson->SetArrayField(TEXT("changes"), SoftUE::AssetReferenceRepointUtils::ChangesToJson(Changes));
		AssetJson->SetBoolField(TEXT("saved"), bChanged && bSave);
		AssetResults.Add(MakeShared<FJsonValueObject>(AssetJson));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("changed_asset_count"), ChangedAssetCount);
	Result->SetNumberField(TEXT("replaced_reference_count"), ReplacedReferenceCount);
	Result->SetObjectField(TEXT("replacement_map"), ReplacementMap.ReplacementPaths);
	Result->SetArrayField(TEXT("assets"), AssetResults);
	return FBridgeToolResult::Json(Result);
}
