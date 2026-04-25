// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "GetAssetDiffTool.generated.h"

class UBlueprint;
class UMaterial;
class UMaterialInstance;
class UDataTable;

/**
 * Tool for diffing binary Unreal assets against SCM (Git/Perforce) base versions.
 * Returns structured JSON diff that can be consumed programmatically by AI assistants.
 *
 * Unlike visual diff tools (IAssetTools::DiffAssets), this returns text-based output.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UGetAssetDiffTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-asset-diff"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// === SCM Detection ===

	/** Detected SCM type */
	enum class EScmType
	{
		None,
		Git,
		Perforce
	};

	/** Detect which SCM is in use for the project */
	EScmType DetectScmType() const;

	/** Get the absolute file path for an asset */
	FString GetAssetFilePath(const FString& AssetPath) const;

	// === Base Version Extraction ===

	/** Extract base version from Git to temp file */
	bool ExtractGitBaseVersion(const FString& FilePath, const FString& Revision, FString& OutTempPath, FString& OutError) const;

	/** Extract base version from Perforce to temp file */
	bool ExtractPerforceBaseVersion(const FString& FilePath, const FString& Revision, FString& OutTempPath, FString& OutError) const;

	// === Asset Loading ===

	/** Load asset from an external file path (like DiffUtils::LoadAssetFromExternalPath) */
	UObject* LoadAssetFromExternalPath(const FString& FilePath, FString& OutError) const;

	// === Diff Generation ===

	/** Compare two assets and generate diff */
	TSharedPtr<FJsonObject> CompareAssets(UObject* CurrentAsset, UObject* BaseAsset) const;

	/** Compare two Blueprints */
	TSharedPtr<FJsonObject> CompareBlueprints(UBlueprint* Current, UBlueprint* Base) const;

	/** Compare two Materials */
	TSharedPtr<FJsonObject> CompareMaterials(UMaterial* Current, UMaterial* Base) const;

	/** Compare two Material Instances */
	TSharedPtr<FJsonObject> CompareMaterialInstances(UMaterialInstance* Current, UMaterialInstance* Base) const;

	/** Compare two DataTables */
	TSharedPtr<FJsonObject> CompareDataTables(UDataTable* Current, UDataTable* Base) const;

	/** Compare two generic UObjects via reflection */
	TSharedPtr<FJsonObject> CompareObjects(UObject* Current, UObject* Base) const;

	// === Blueprint Comparison Helpers ===

	/** Compare Blueprint variables */
	TSharedPtr<FJsonObject> CompareBlueprintVariables(UBlueprint* Current, UBlueprint* Base) const;

	/** Compare Blueprint functions */
	TSharedPtr<FJsonObject> CompareBlueprintFunctions(UBlueprint* Current, UBlueprint* Base) const;

	/** Compare Blueprint components */
	TSharedPtr<FJsonObject> CompareBlueprintComponents(UBlueprint* Current, UBlueprint* Base) const;

	// === Utility ===

	/** Get property value as string */
	FString GetPropertyValueString(FProperty* Property, void* Container, UObject* Owner) const;

	/** Clean up temp files */
	void CleanupTempFile(const FString& TempPath) const;
};
