// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/DeleteAssetTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "UObject/UObjectGlobals.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"

FString UDeleteAssetTool::GetToolDescription() const
{
	return TEXT("Delete an asset. For Blueprints, runs garbage collection to ensure generated classes are fully cleaned up.");
}

TMap<FString, FBridgeSchemaProperty> UDeleteAssetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to delete (e.g., '/Game/MyBlueprint')");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

FBridgeToolResult UDeleteAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Missing required argument: asset_path"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Deleting %s"), *AssetPath);

	// Load the asset first to check if it's a Blueprint
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		// Check for phantom asset: registry has entry but LoadAsset fails
		if (FBridgeAssetModifier::AssetExists(AssetPath))
		{
			UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("delete-asset: Phantom asset detected at %s (registry entry but not loadable). Cleaning up."), *AssetPath);

			// Force rescan and retry load
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FString> ScanPaths = { FPackageName::GetLongPackagePath(AssetPath) };
			AssetRegistryModule.Get().ScanPathsSynchronous(ScanPaths, /*bForceRescan=*/true);
			Asset = UEditorAssetLibrary::LoadAsset(AssetPath);

			if (!Asset)
			{
				// Still can't load — remove the stale registry entry by rescanning
				// Delete the on-disk package file if it exists
				FString PackageFilename;
				if (FPackageName::DoesPackageExist(AssetPath, &PackageFilename))
				{
					if (IFileManager::Get().Delete(*PackageFilename))
					{
						UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Deleted orphan package file: %s"), *PackageFilename);
					}
					else
					{
						UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("delete-asset: Failed to delete orphan package file: %s"), *PackageFilename);
					}
				}

				// Rescan to purge the registry entry
				AssetRegistryModule.Get().ScanPathsSynchronous(ScanPaths, /*bForceRescan=*/true);

				TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
				Result->SetStringField(TEXT("asset_path"), AssetPath);
				Result->SetBoolField(TEXT("success"), true);
				Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Phantom asset cleaned up: %s"), *AssetPath));
				Result->SetBoolField(TEXT("was_phantom"), true);
				return FBridgeToolResult::Json(Result);
			}
			// Rescan fixed the load — fall through to normal deletion
		}
		else
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		}
	}

	bool bIsBlueprint = Asset->IsA<UBlueprint>();

	// UEditorAssetLibrary handles Blueprint generated class cleanup internally
	bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);

	if (!bSuccess)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to delete asset: %s"), *AssetPath));
	}

	// Two GC passes for Blueprints: first collects the Blueprint and marks
	// generated class for deletion, second cleans up dependent objects
	if (bIsBlueprint)
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Running garbage collection for Blueprint"));
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Asset deleted successfully: %s"), *AssetPath));

	if (bIsBlueprint)
	{
		Result->SetBoolField(TEXT("was_blueprint"), true);
		Result->SetStringField(TEXT("note"), TEXT("Blueprint generated class cleanup performed"));
	}

	return FBridgeToolResult::Json(Result);
}
