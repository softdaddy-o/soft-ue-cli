// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/DeleteAssetTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "UObject/UObjectGlobals.h"
#include "EditorAssetLibrary.h"

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
		return FBridgeToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
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
