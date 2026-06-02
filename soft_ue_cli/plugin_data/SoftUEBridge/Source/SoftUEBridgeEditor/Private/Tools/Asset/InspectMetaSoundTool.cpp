// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/InspectMetaSoundTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "MetasoundDocumentInterface.h"
#include "Tools/Asset/MetaSoundGraphSerializer.h"

FString UInspectMetaSoundTool::GetToolDescription() const
{
	return TEXT(
		"Inspect a MetaSound Source or Patch asset. Returns the declared interface inputs/outputs, "
		"graph nodes with their class names and input defaults, and the edges between nodes.");
}

TMap<FString, FBridgeSchemaProperty> UInspectMetaSoundTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the MetaSound Source or Patch (e.g., /Game/Audio/MS_Footsteps)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UInspectMetaSoundTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UInspectMetaSoundTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	UObject* AssetObject = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
	if (!AssetObject)
	{
		AssetObject = LoadObject<UObject>(nullptr, *AssetPath);
	}

	if (!AssetObject)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	const IMetaSoundDocumentInterface* DocumentInterface = Cast<IMetaSoundDocumentInterface>(AssetObject);
	if (!DocumentInterface)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Asset '%s' is '%s', not a MetaSound Source or Patch"),
			*AssetPath,
			*AssetObject->GetClass()->GetName()));
	}

	const FMetasoundFrontendDocument& Document = DocumentInterface->GetConstDocument();
	const FString AssetType = AssetObject->GetClass()->GetName();

	return FBridgeToolResult::Json(MetaSoundGraphSerializer::SerializeDocument(Document, AssetType));
}
