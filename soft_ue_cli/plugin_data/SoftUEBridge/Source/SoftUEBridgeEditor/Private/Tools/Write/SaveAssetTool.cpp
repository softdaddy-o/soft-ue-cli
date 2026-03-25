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

	FBridgeSchemaProperty Checkout;
	Checkout.Type = TEXT("boolean");
	Checkout.Description = TEXT("Attempt to check out the file from source control before saving (default: false)");
	Checkout.bRequired = false;
	Schema.Add(TEXT("checkout"), Checkout);

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

	// Optionally check out from source control before saving
	bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	bool bCheckedOut = false;

	if (bCheckout)
	{
		UPackage* Package = Object->GetOutermost();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		if (IFileManager::Get().FileExists(*PackageFileName) && IFileManager::Get().IsReadOnly(*PackageFileName))
		{
			FString CheckoutError;
			if (FBridgeAssetModifier::CheckoutFile(PackageFileName, CheckoutError))
			{
				bCheckedOut = true;
				UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("save-asset: Checked out %s"), *PackageFileName);
			}
			else
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Checkout failed: %s"), *CheckoutError));
			}
		}
	}

	// Save
	FString SaveError;
	bool bSaved = FBridgeAssetModifier::SaveAsset(Object, false, SaveError);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSaved);

	if (bCheckout)
	{
		Result->SetBoolField(TEXT("checked_out"), bCheckedOut);
	}

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
