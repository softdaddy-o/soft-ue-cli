// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/RemoveGraphNodeTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString URemoveGraphNodeTool::GetToolDescription() const
{
	return TEXT("Remove a node from a Blueprint or Material graph. For AnimBlueprints, also supports blend_stack, state_machine, and other animation graphs.");
}

TMap<FString, FBridgeSchemaProperty> URemoveGraphNodeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint or Material");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty NodeId;
	NodeId.Type = TEXT("string");
	NodeId.Description = TEXT("Node GUID (for Blueprints) or expression name (for Materials)");
	NodeId.bRequired = true;
	Schema.Add(TEXT("node_id"), NodeId);

	return Schema;
}

TArray<FString> URemoveGraphNodeTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("node_id") };
}

FBridgeToolResult URemoveGraphNodeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeId = GetStringArgOrDefault(Arguments, TEXT("node_id"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and node_id are required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("remove-graph-node: %s from %s"), *NodeId, *AssetPath);

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		NSLOCTEXT("MCP", "RemoveNode", "Remove graph node"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("node_id"), NodeId);

	// Handle Blueprint
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		FBridgeAssetModifier::MarkModified(Blueprint);

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeId, NodeGuid))
		{
			return FBridgeToolResult::Error(TEXT("Invalid node GUID format"));
		}

		// Find and remove the node using shared helper (supports AnimBlueprint graphs)
		UEdGraph* FoundGraph = nullptr;
		UEdGraphNode* FoundNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, NodeGuid, &FoundGraph);

		if (!FoundNode || !FoundGraph)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
		}

		// Remove the node
		FoundGraph->RemoveNode(FoundNode);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FBridgeAssetModifier::MarkPackageDirty(Blueprint);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_compile"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("remove-graph-node: Removed Blueprint node %s"), *NodeId);

		return FBridgeToolResult::Json(Result);
	}

	// Handle Material
	if (UMaterial* Material = Cast<UMaterial>(Object))
	{
		FBridgeAssetModifier::MarkModified(Material);

		// Find expression by name
		UMaterialExpression* FoundExpression = nullptr;
		int32 FoundIndex = INDEX_NONE;

		TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
		for (int32 i = 0; i < Expressions.Num(); ++i)
		{
			if (Expressions[i] && Expressions[i]->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
			{
				FoundExpression = Expressions[i];
				FoundIndex = i;
				break;
			}
		}

		if (!FoundExpression)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Expression not found: %s"), *NodeId));
		}

		// Remove the expression
		Material->GetExpressionCollection().RemoveExpression(FoundExpression);

		// Refresh the material to notify editors and trigger recompilation
		FBridgeAssetModifier::RefreshMaterial(Material);
		FBridgeAssetModifier::MarkPackageDirty(Material);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("remove-graph-node: Removed material expression %s"), *NodeId);

		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(TEXT("Asset must be a Blueprint or Material"));
}
