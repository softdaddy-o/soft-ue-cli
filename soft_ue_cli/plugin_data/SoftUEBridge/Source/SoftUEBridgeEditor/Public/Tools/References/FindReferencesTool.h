// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "AssetRegistry/AssetData.h"
#include "FindReferencesTool.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class FStreamSearch;
struct FSearchData;

/**
 * Tool for finding references to assets, Blueprint variables, and nodes.
 *
 * Supports three reference types:
 * - "asset": Find all assets that reference the given asset
 * - "property": Find where a Blueprint variable is used within its own graphs
 * - "node": Find all usages of a specific node type or function call
 *
 * Uses UE5's Find in Blueprints (FiB) cache for faster searching when available.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UFindReferencesTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("find-references"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Find assets that reference the given asset */
	FBridgeToolResult FindAssetReferences(const FString& AssetPath, int32 Limit, const FString& SearchFilter = TEXT(""));

	/** Find usages of a variable within a Blueprint */
	FBridgeToolResult FindPropertyReferences(const FString& AssetPath, const FString& VariableName, int32 Limit);

	/** Find usages of a node type or function within a Blueprint - uses FiB cache */
	FBridgeToolResult FindNodeReferences(const FString& AssetPath, const FString& NodeClass,
									   const FString& FunctionName, int32 Limit);

	/** Find usages of a node type or function - legacy method loading each Blueprint */
	FBridgeToolResult FindNodeReferencesLegacy(const FString& AssetPath, const FString& NodeClass,
											const FString& FunctionName, int32 Limit);

	/** Use FiB cache to quickly find matching Blueprints, then load for detailed inspection */
	TArray<FSoftObjectPath> FindMatchingBlueprintsViaFiB(const FString& SearchTerm, const FString& PathFilter);

	/** Helper to get all graphs from a Blueprint */
	TArray<UEdGraph*> GetAllGraphs(UBlueprint* Blueprint) const;

	/** Helper to determine graph type string */
	FString GetGraphType(UBlueprint* Blueprint, UEdGraph* Graph) const;

	/** Convert a node reference to JSON */
	TSharedPtr<FJsonObject> NodeReferenceToJson(UEdGraphNode* Node, UEdGraph* Graph,
												 const FString& GraphType) const;

	/** Get all Blueprints in a directory path */
	TArray<FAssetData> GetBlueprintsInPath(const FString& Path) const;
};
