// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/DisconnectGraphPinTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString UDisconnectGraphPinTool::GetToolDescription() const
{
	return TEXT("Break connections from a pin in a Blueprint or Material graph. "
		"By default, disconnects ALL connections from the pin. "
		"With optional target_node and target_pin, disconnects only the specific connection to that target, preserving other wires. "
		"For AnimBlueprints, also supports blend_stack, state_machine, and other animation graphs.");
}

TMap<FString, FBridgeSchemaProperty> UDisconnectGraphPinTool::GetInputSchema() const
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

	FBridgeSchemaProperty PinName;
	PinName.Type = TEXT("string");
	PinName.Description = TEXT("Pin name to disconnect");
	PinName.bRequired = true;
	Schema.Add(TEXT("pin_name"), PinName);

	FBridgeSchemaProperty TargetNodeId;
	TargetNodeId.Type = TEXT("string");
	TargetNodeId.Description = TEXT("Target node GUID to disconnect from (optional). When specified with target_pin, only the specific connection to this node/pin is broken.");
	TargetNodeId.bRequired = false;
	Schema.Add(TEXT("target_node"), TargetNodeId);

	FBridgeSchemaProperty TargetPinName;
	TargetPinName.Type = TEXT("string");
	TargetPinName.Description = TEXT("Target pin name to disconnect from (optional). Must be specified together with target_node.");
	TargetPinName.bRequired = false;
	Schema.Add(TEXT("target_pin"), TargetPinName);

	return Schema;
}

TArray<FString> UDisconnectGraphPinTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("node_id"), TEXT("pin_name") };
}

FBridgeToolResult UDisconnectGraphPinTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeId = GetStringArgOrDefault(Arguments, TEXT("node_id"));
	FString PinName = GetStringArgOrDefault(Arguments, TEXT("pin_name"));
	FString TargetNodeId = GetStringArgOrDefault(Arguments, TEXT("target_node"));
	FString TargetPinName = GetStringArgOrDefault(Arguments, TEXT("target_pin"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, node_id, and pin_name are required"));
	}

	// Validate target params are either both present or both absent
	bool bSpecificTarget = !TargetNodeId.IsEmpty() || !TargetPinName.IsEmpty();
	if (bSpecificTarget && (TargetNodeId.IsEmpty() || TargetPinName.IsEmpty()))
	{
		return FBridgeToolResult::Error(TEXT("target_node and target_pin must both be specified together"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("disconnect-graph-pin: %s.%s in %s"), *NodeId, *PinName, *AssetPath);

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		NSLOCTEXT("MCP", "DisconnectPin", "Disconnect graph pin"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("pin_name"), PinName);

	// Handle Blueprint
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		FBridgeAssetModifier::MarkModified(Blueprint);

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeId, NodeGuid))
		{
			return FBridgeToolResult::Error(TEXT("Invalid node GUID format"));
		}

		// Find the node using shared helper (supports AnimBlueprint graphs)
		UEdGraphNode* FoundNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, NodeGuid);

		if (!FoundNode)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
		}

		// Find the pin
		UEdGraphPin* FoundPin = nullptr;
		for (UEdGraphPin* Pin : FoundNode->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				FoundPin = Pin;
				break;
			}
		}

		if (!FoundPin)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Pin not found: %s"), *PinName));
		}

		if (bSpecificTarget)
		{
			// Disconnect a specific pin-to-pin connection
			FGuid TargetGuid;
			if (!FGuid::Parse(TargetNodeId, TargetGuid))
			{
				return FBridgeToolResult::Error(TEXT("Invalid target_node GUID format"));
			}

			UEdGraphNode* TargetNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, TargetGuid);
			if (!TargetNode)
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
			}

			UEdGraphPin* TargetPin = nullptr;
			for (UEdGraphPin* Pin : TargetNode->Pins)
			{
				if (Pin && Pin->PinName.ToString().Equals(TargetPinName, ESearchCase::IgnoreCase))
				{
					TargetPin = Pin;
					break;
				}
			}

			if (!TargetPin)
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Target pin not found: %s"), *TargetPinName));
			}

			if (!FoundPin->LinkedTo.Contains(TargetPin))
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("No connection exists between %s.%s and %s.%s"),
					*NodeId, *PinName, *TargetNodeId, *TargetPinName));
			}

			FoundPin->BreakLinkTo(TargetPin);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FBridgeAssetModifier::MarkPackageDirty(Blueprint);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetNumberField(TEXT("connections_broken"), 1);
			Result->SetStringField(TEXT("target_node"), TargetNodeId);
			Result->SetStringField(TEXT("target_pin"), TargetPinName);
			Result->SetBoolField(TEXT("needs_compile"), true);
			Result->SetBoolField(TEXT("needs_save"), true);

			UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("disconnect-graph-pin: Broke specific connection to %s.%s"), *TargetNodeId, *TargetPinName);
		}
		else
		{
			// Break all connections (original behavior)
			int32 ConnectionsCount = FoundPin->LinkedTo.Num();
			FoundPin->BreakAllPinLinks();

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FBridgeAssetModifier::MarkPackageDirty(Blueprint);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetNumberField(TEXT("connections_broken"), ConnectionsCount);
			Result->SetBoolField(TEXT("needs_compile"), true);
			Result->SetBoolField(TEXT("needs_save"), true);

			UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("disconnect-graph-pin: Broke %d connections"), ConnectionsCount);
		}

		return FBridgeToolResult::Json(Result);
	}

	// Handle Material
	if (UMaterial* Material = Cast<UMaterial>(Object))
	{
		FBridgeAssetModifier::MarkModified(Material);

		// Find expression by name
		UMaterialExpression* FoundExpression = nullptr;

		for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
		{
			if (Expr && Expr->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
			{
				FoundExpression = Expr;
				break;
			}
		}

		if (!FoundExpression)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Expression not found: %s"), *NodeId));
		}

		// Find and disconnect the input using GetInput() iteration
		bool bDisconnected = false;
		for (int32 i = 0; ; i++)
		{
			FExpressionInput* Input = FoundExpression->GetInput(i);
			if (!Input)
			{
				break; // No more inputs
			}

			FString InputName = FoundExpression->GetInputName(i).ToString();
			if (InputName.Equals(PinName, ESearchCase::IgnoreCase))
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				bDisconnected = true;
				break;
			}

			// Also check common input indices A=0, B=1
			if (PinName.Equals(TEXT("A"), ESearchCase::IgnoreCase) && i == 0)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				bDisconnected = true;
				break;
			}
			else if (PinName.Equals(TEXT("B"), ESearchCase::IgnoreCase) && i == 1)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				bDisconnected = true;
				break;
			}
		}

		if (!bDisconnected)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Input not found: %s"), *PinName));
		}

		// Refresh the material to notify editors and trigger recompilation
		FBridgeAssetModifier::RefreshMaterial(Material);
		FBridgeAssetModifier::MarkPackageDirty(Material);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("disconnect-graph-pin: Disconnected material input"));

		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(TEXT("Asset must be a Blueprint or Material"));
}
