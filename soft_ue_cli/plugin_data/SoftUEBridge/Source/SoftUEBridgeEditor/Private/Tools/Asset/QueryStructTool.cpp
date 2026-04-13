// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/QueryStructTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "SoftUEBridgeEditorModule.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Tools/Asset/AssetIntrospectionUtils.h"

FString UQueryStructTool::GetToolDescription() const
{
	return TEXT("Inspect a UserDefinedStruct asset and return authored member names, types, defaults, and metadata.");
}

TMap<FString, FBridgeSchemaProperty> UQueryStructTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the UserDefinedStruct (e.g., /Game/Data/S_Result)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UQueryStructTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UQueryStructTool::Execute(
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
	UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(AssetObject);
	if (!UserStruct)
	{
		UserStruct = LoadObject<UUserDefinedStruct>(nullptr, *AssetPath);
	}

	if (!UserStruct)
	{
		if (AssetData.IsValid())
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Asset '%s' is '%s', not UserDefinedStruct"),
				*AssetPath,
				*AssetData.AssetClassPath.GetAssetName().ToString()));
		}

		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load UserDefinedStruct: %s"), *AssetPath));
	}

	return FBridgeToolResult::Json(AssetIntrospectionUtils::InspectUserDefinedStruct(UserStruct, AssetPath));
}
