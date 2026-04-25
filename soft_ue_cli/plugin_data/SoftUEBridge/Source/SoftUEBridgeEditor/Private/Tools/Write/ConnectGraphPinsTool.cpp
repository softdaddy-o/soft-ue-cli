// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/ConnectGraphPinsTool.h"
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

FString UConnectGraphPinsTool::GetToolDescription() const
{
	return TEXT("Connect two pins in a Blueprint or Material graph. For AnimBlueprints, also supports blend_stack, state_machine, and other animation graphs.");
}

TMap<FString, FBridgeSchemaProperty> UConnectGraphPinsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint or Material");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty SourceNode;
	SourceNode.Type = TEXT("string");
	SourceNode.Description = TEXT("Source node GUID (for Blueprints) or expression name (for Materials)");
	SourceNode.bRequired = true;
	Schema.Add(TEXT("source_node"), SourceNode);

	FBridgeSchemaProperty SourcePin;
	SourcePin.Type = TEXT("string");
	SourcePin.Description = TEXT("Source pin name");
	SourcePin.bRequired = true;
	Schema.Add(TEXT("source_pin"), SourcePin);

	FBridgeSchemaProperty TargetNode;
	TargetNode.Type = TEXT("string");
	TargetNode.Description = TEXT("Target node GUID (for Blueprints) or expression name (for Materials)");
	TargetNode.bRequired = true;
	Schema.Add(TEXT("target_node"), TargetNode);

	FBridgeSchemaProperty TargetPin;
	TargetPin.Type = TEXT("string");
	TargetPin.Description = TEXT("Target pin name");
	TargetPin.bRequired = true;
	Schema.Add(TEXT("target_pin"), TargetPin);

	return Schema;
}

TArray<FString> UConnectGraphPinsTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("source_node"), TEXT("source_pin"), TEXT("target_node"), TEXT("target_pin") };
}

FBridgeToolResult UConnectGraphPinsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString SourceNode = GetStringArgOrDefault(Arguments, TEXT("source_node"));
	FString SourcePin = GetStringArgOrDefault(Arguments, TEXT("source_pin"));
	FString TargetNode = GetStringArgOrDefault(Arguments, TEXT("target_node"));
	FString TargetPin = GetStringArgOrDefault(Arguments, TEXT("target_pin"));

	if (AssetPath.IsEmpty() || SourceNode.IsEmpty() || SourcePin.IsEmpty() ||
		TargetNode.IsEmpty() || TargetPin.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("All parameters are required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("connect-graph-pins: %s.%s -> %s.%s in %s"),
		*SourceNode, *SourcePin, *TargetNode, *TargetPin, *AssetPath);

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		NSLOCTEXT("MCP", "ConnectPins", "Connect graph pins"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);

	// Handle Blueprint
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		FBridgeAssetModifier::MarkModified(Blueprint);

		// Parse GUIDs
		FGuid SourceGuid, TargetGuid;
		if (!FGuid::Parse(SourceNode, SourceGuid) || !FGuid::Parse(TargetNode, TargetGuid))
		{
			return FBridgeToolResult::Error(TEXT("Invalid node GUID format"));
		}

		// Find nodes using shared helper (supports AnimBlueprint graphs)
		UEdGraphNode* SourceGraphNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, SourceGuid);
		UEdGraphNode* TargetGraphNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, TargetGuid);

		if (!SourceGraphNode)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Source node not found: %s"), *SourceNode));
		}
		if (!TargetGraphNode)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNode));
		}

		// Find pins
		UEdGraphPin* SourceGraphPin = nullptr;
		UEdGraphPin* TargetGraphPin = nullptr;

		for (UEdGraphPin* Pin : SourceGraphNode->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(SourcePin, ESearchCase::IgnoreCase))
			{
				SourceGraphPin = Pin;
				break;
			}
		}

		for (UEdGraphPin* Pin : TargetGraphNode->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(TargetPin, ESearchCase::IgnoreCase))
			{
				TargetGraphPin = Pin;
				break;
			}
		}

		if (!SourceGraphPin)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Source pin not found: %s"), *SourcePin));
		}
		if (!TargetGraphPin)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target pin not found: %s"), *TargetPin));
		}

		// Check if connection is valid
		const UEdGraphSchema* Schema = SourceGraphNode->GetGraph()->GetSchema();
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourceGraphPin, TargetGraphPin);

		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Cannot connect pins: %s"), *Response.Message.ToString()));
		}

		// Make the connection
		bool bConnected = Schema->TryCreateConnection(SourceGraphPin, TargetGraphPin);

		if (bConnected)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FBridgeAssetModifier::MarkPackageDirty(Blueprint);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetBoolField(TEXT("needs_compile"), true);
			Result->SetBoolField(TEXT("needs_save"), true);

			UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("connect-graph-pins: Successfully connected pins"));
		}
		else
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("Failed to connect pins"));
		}

		return FBridgeToolResult::Json(Result);
	}

	// Handle Material
	if (UMaterial* Material = Cast<UMaterial>(Object))
	{
		FBridgeAssetModifier::MarkModified(Material);

		// Find expressions by name
		UMaterialExpression* SourceExpression = nullptr;
		UMaterialExpression* TargetExpression = nullptr;

		for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
		{
			if (Expr)
			{
				if (Expr->GetName().Equals(SourceNode, ESearchCase::IgnoreCase))
				{
					SourceExpression = Expr;
				}
				if (Expr->GetName().Equals(TargetNode, ESearchCase::IgnoreCase))
				{
					TargetExpression = Expr;
				}
			}
		}

		if (!SourceExpression)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Source expression not found: %s"), *SourceNode));
		}
		if (!TargetExpression)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target expression not found: %s"), *TargetNode));
		}

		// For materials, we need to connect expression inputs
		// Find the target input by name using GetInput() iteration
		FExpressionInput* TargetInput = nullptr;

		for (int32 i = 0; ; i++)
		{
			FExpressionInput* Input = TargetExpression->GetInput(i);
			if (!Input)
			{
				break; // No more inputs
			}

			FString InputName = TargetExpression->GetInputName(i).ToString();
			if (InputName.Equals(TargetPin, ESearchCase::IgnoreCase))
			{
				TargetInput = Input;
				break;
			}

			// Also check common input names A=0, B=1
			if (TargetPin.Equals(TEXT("A"), ESearchCase::IgnoreCase) && i == 0)
			{
				TargetInput = Input;
				break;
			}
			else if (TargetPin.Equals(TEXT("B"), ESearchCase::IgnoreCase) && i == 1)
			{
				TargetInput = Input;
				break;
			}
		}

		if (!TargetInput)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Target input not found: %s"), *TargetPin));
		}

		// Connect
		TargetInput->Expression = SourceExpression;

		// Find output index if specified
		int32 OutputIndex = 0;
		if (!SourcePin.IsEmpty() && FCString::IsNumeric(*SourcePin))
		{
			OutputIndex = FCString::Atoi(*SourcePin);
		}
		TargetInput->OutputIndex = OutputIndex;

		// Refresh the material to notify editors and trigger recompilation
		FBridgeAssetModifier::RefreshMaterial(Material);
		FBridgeAssetModifier::MarkPackageDirty(Material);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("connect-graph-pins: Connected material expressions"));

		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(TEXT("Asset must be a Blueprint or Material"));
}
