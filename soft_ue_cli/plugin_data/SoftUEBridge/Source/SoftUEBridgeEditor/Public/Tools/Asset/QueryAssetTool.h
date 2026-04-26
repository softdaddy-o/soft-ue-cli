// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "QueryAssetTool.generated.h"

/**
 * Consolidated tool for asset operations.
 * Replaces: search-assets, inspect-asset, inspect-data-asset
 *
 * Usage modes:
 * - query param: Search for assets (like search-assets)
 * - asset_path param: Inspect specific asset (like inspect-asset/inspect-data-asset)
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UQueryAssetTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-asset"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// === Search mode ===

	/** Search for assets matching criteria */
	FBridgeToolResult SearchAssets(const FString& Query, const FString& ClassFilter,
		const FString& PathFilter, int32 Limit) const;

	// === Inspect mode ===

	/** Inspect a specific asset */
	FBridgeToolResult InspectAsset(const FString& AssetPath, int32 MaxDepth,
		bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter,
		const FString& RowFilter) const;

	/** Inspect DataTable */
	TSharedPtr<FJsonObject> InspectDataTable(class UDataTable* DataTable, const FString& RowFilter) const;

	/** Inspect DataAsset */
	TSharedPtr<FJsonObject> InspectDataAsset(class UDataAsset* DataAsset) const;

	/** Inspect a Blueprint whose generated class derives from UDataAsset */
	TSharedPtr<FJsonObject> InspectDataAssetBlueprint(class UBlueprint* Blueprint, int32 MaxDepth,
		bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const;

	/** Inspect a world asset and expose WorldSettings helpers such as DefaultGameMode. */
	TSharedPtr<FJsonObject> InspectWorldAsset(class UWorld* World, int32 MaxDepth,
		bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const;

	/** Inspect LandscapeGrassType */
	TSharedPtr<FJsonObject> InspectGrassType(class ULandscapeGrassType* GrassType) const;

	/** Inspect general UObject */
	TSharedPtr<FJsonObject> InspectObject(UObject* Object, int32 MaxDepth,
		bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const;

	// === Helpers ===

	/** Convert property to JSON */
	TSharedPtr<FJsonObject> PropertyToJson(class FProperty* Property, void* Container,
		UObject* Owner, int32 CurrentDepth, int32 MaxDepth, bool bIncludeDefaults) const;

	/** Get property type as string */
	FString GetPropertyTypeString(class FProperty* Property) const;
};
