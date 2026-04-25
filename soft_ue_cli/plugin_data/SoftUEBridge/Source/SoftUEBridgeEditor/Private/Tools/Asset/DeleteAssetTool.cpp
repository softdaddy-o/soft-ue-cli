// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/DeleteAssetTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "EditorAssetLibrary.h"

FString UDeleteAssetTool::GetToolDescription() const
{
	return TEXT("Delete an asset with proper cleanup of Blueprint generated classes. Handles the case where Blueprint generated classes persist in memory after deletion.");
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
		return FBridgeToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Special handling for Blueprints to cleanup generated classes
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint)
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Blueprint detected, cleaning up generated class"));

		// Get the generated class before deletion
		UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);

		if (GeneratedClass)
		{
			// Unregister the class from the class hierarchy
			// This prevents it from persisting in memory after deletion
			GeneratedClass->ClearFunctionMapsCaches();

			// Mark for pending kill
			GeneratedClass->MarkAsGarbage();

			UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Marked generated class for garbage collection: %s"),
				*GeneratedClass->GetName());
		}
	}

	// Delete the asset
	bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);

	if (!bSuccess)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to delete asset: %s"), *AssetPath));
	}

	// Force garbage collection if it was a Blueprint
	if (Blueprint)
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("delete-asset: Running garbage collection"));
		// First pass collects the Blueprint and marks generated class for deletion
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		// Second pass ensures complete cleanup of dependent objects
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Asset deleted successfully: %s"), *AssetPath));

	if (Blueprint)
	{
		Result->SetBoolField(TEXT("was_blueprint"), true);
		Result->SetStringField(TEXT("note"), TEXT("Blueprint generated class cleanup performed"));
	}

	return FBridgeToolResult::Json(Result);
}
