// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class UBlueprint;
class FScopedTransaction;

/**
 * Utility class for Bridge write operations.
 * Provides transaction management, asset modification, and validation helpers.
 */
class SOFTUEBRIDGEEDITOR_API FBridgeAssetModifier
{
public:
	// ========== Transaction Management ==========

	/**
	 * Begin a transaction for undo/redo support.
	 * @param Description - Human-readable description of the operation
	 * @return Shared pointer to the scoped transaction (keep alive until operation complete)
	 */
	static TSharedPtr<FScopedTransaction> BeginTransaction(const FText& Description);

	// ========== Asset Loading ==========

	/**
	 * Load an asset by path with validation.
	 * @param AssetPath - Asset path (e.g., "/Game/Blueprints/BP_Player")
	 * @param OutError - Error message if loading fails
	 * @return Loaded object or nullptr on failure
	 */
	static UObject* LoadAssetByPath(const FString& AssetPath, FString& OutError);

	/**
	 * Load an asset by path and cast to expected type.
	 * @param AssetPath - Asset path
	 * @param OutError - Error message if loading fails
	 * @return Loaded and cast object or nullptr on failure
	 */
	template<typename T>
	static T* LoadAssetByPath(const FString& AssetPath, FString& OutError)
	{
		UObject* Object = LoadAssetByPath(AssetPath, OutError);
		if (!Object)
		{
			return nullptr;
		}

		T* CastObject = Cast<T>(Object);
		if (!CastObject)
		{
			OutError = FString::Printf(TEXT("Asset '%s' is not of expected type %s"), *AssetPath, *T::StaticClass()->GetName());
			return nullptr;
		}

		return CastObject;
	}

	// ========== Asset Modification ==========

	/**
	 * Mark an object as modified for undo/redo and dirty tracking.
	 * Call before making any modifications.
	 * @param Object - Object to mark
	 * @return true if successful
	 */
	static bool MarkModified(UObject* Object);

	/**
	 * Mark an asset's package as dirty (needs save).
	 * @param Object - Object whose package to mark
	 * @return true if successful
	 */
	static bool MarkPackageDirty(UObject* Object);

	// ========== Asset Saving ==========

	/**
	 * Save an asset to disk.
	 * @param Object - Object to save
	 * @param bPromptUser - If true, show save dialog on failure
	 * @param OutError - Error message if saving fails
	 * @return true if saved successfully
	 */
	static bool SaveAsset(UObject* Object, bool bPromptUser, FString& OutError);

	// ========== Blueprint Utilities ==========

	/**
	 * Compile a Blueprint.
	 * @param Blueprint - Blueprint to compile
	 * @param OutError - Error message if compilation fails
	 * @return true if compiled successfully
	 */
	static bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError);

	/**
	 * Refresh all nodes in a Blueprint (updates pins, etc.).
	 * @param Blueprint - Blueprint to refresh
	 */
	static void RefreshBlueprintNodes(UBlueprint* Blueprint);

	/**
	 * Refresh a Material to notify all open Material Editors of changes.
	 * Triggers shader recompilation and updates the Material Editor UI.
	 * @param Material - Material to refresh
	 */
	static void RefreshMaterial(class UMaterial* Material);

	/**
	 * Find a graph by name in a Blueprint, including AnimBlueprint-specific graphs.
	 * @param Blueprint - Blueprint to search
	 * @param GraphName - Name of the graph to find
	 * @return Found graph or nullptr
	 */
	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

	/**
	 * Find a node by GUID in a Blueprint, searching all graphs including AnimBlueprint graphs.
	 * @param Blueprint - Blueprint to search
	 * @param NodeGuid - GUID of the node to find
	 * @param OutGraph - Optional output for the graph containing the node
	 * @return Found node or nullptr
	 */
	static UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid, UEdGraph** OutGraph = nullptr);

	/**
	 * Get all searchable graphs from a Blueprint, including AnimBlueprint-specific graphs.
	 * @param Blueprint - Blueprint to get graphs from
	 * @param OutGraphs - Output array of all graphs
	 */
	static void GetAllSearchableGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs);

	// ========== Validation ==========

	/**
	 * Validate an asset path format.
	 * @param AssetPath - Path to validate
	 * @param OutError - Error message if invalid
	 * @return true if valid format
	 */
	static bool ValidateAssetPath(const FString& AssetPath, FString& OutError);

	/**
	 * Check if an asset exists at the given path.
	 * @param AssetPath - Path to check
	 * @return true if asset exists
	 */
	static bool AssetExists(const FString& AssetPath);

	// ========== Property Utilities ==========

	/**
	 * Find a property by path on an object.
	 * Supports nested paths like "Stats.MaxHealth" and array indices like "Items[0]".
	 * @param Object - Object to search
	 * @param PropertyPath - Dot-separated property path
	 * @param OutProperty - Found property
	 * @param OutContainer - Pointer to the container holding the property value
	 * @param OutError - Error message if not found
	 * @return true if found
	 */
	static bool FindPropertyByPath(
		UObject* Object,
		const FString& PropertyPath,
		FProperty*& OutProperty,
		void*& OutContainer,
		FString& OutError);

	/**
	 * Set a property value from a JSON value.
	 * @param Property - Property to set
	 * @param Container - Pointer to container holding the property
	 * @param Value - JSON value to set
	 * @param OutError - Error message if setting fails
	 * @return true if set successfully
	 */
	static bool SetPropertyFromJson(
		FProperty* Property,
		void* Container,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

private:
	/**
	 * Parse array index from property path segment.
	 * @param Segment - Path segment like "Items[0]"
	 * @param OutName - Property name without index
	 * @param OutIndex - Array index or -1 if not an array access
	 * @return true if parsed successfully
	 */
	static bool ParseArrayIndex(const FString& Segment, FString& OutName, int32& OutIndex);
};
