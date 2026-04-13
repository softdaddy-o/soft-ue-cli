// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/QueryEnumTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/UserDefinedEnum.h"
#include "SoftUEBridgeEditorModule.h"
#include "Tools/Asset/AssetIntrospectionUtils.h"

FString UQueryEnumTool::GetToolDescription() const
{
	return TEXT("Inspect a UserDefinedEnum asset and return authored names, display names, tooltips, and numeric values.");
}

TMap<FString, FBridgeSchemaProperty> UQueryEnumTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the UserDefinedEnum (e.g., /Game/Data/E_State)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UQueryEnumTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UQueryEnumTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FName(*AssetPath));
	UObject* AssetObject = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
	UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(AssetObject);
	if (!UserEnum)
	{
		UserEnum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
	}

	if (!UserEnum)
	{
		if (AssetData.IsValid())
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Asset '%s' is '%s', not UserDefinedEnum"),
				*AssetPath,
				*AssetData.AssetClassPath.GetAssetName().ToString()));
		}

		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load UserDefinedEnum: %s"), *AssetPath));
	}

	return FBridgeToolResult::Json(AssetIntrospectionUtils::InspectUserDefinedEnum(UserEnum, AssetPath));
}
