// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/SaveAssetTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"

FString USaveAssetTool::GetToolDescription() const
{
	return TEXT("Save a modified asset to disk. Use after mutation commands (add-graph-node, modify-interface, etc.) to persist changes and prevent data loss from editor crashes.");
}

TMap<FString, FBridgeSchemaProperty> USaveAssetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to save (e.g. /Game/Blueprints/BP_Player)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> USaveAssetTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult USaveAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("save-asset: %s"), *AssetPath);

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Save
	FString SaveError;
	bool bSaved = FBridgeAssetModifier::SaveAsset(Object, false, SaveError);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSaved);

	if (!bSaved)
	{
		Result->SetStringField(TEXT("error"), SaveError);
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("save-asset: Failed to save %s: %s"), *AssetPath, *SaveError);
	}
	else
	{
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("save-asset: Saved %s"), *AssetPath);
	}

	return FBridgeToolResult::Json(Result);
}
