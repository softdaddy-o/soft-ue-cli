// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/SetNodePositionTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString USetNodePositionTool::GetToolDescription() const
{
	return TEXT("Batch-set editor positions for nodes in a Material, Blueprint, or AnimBlueprint graph. "
		"Nodes are identified by GUID. All moves happen in a single undo transaction. "
		"Use query-material (with --include-positions) or query-blueprint-graph to get node GUIDs and current positions.");
}

TMap<FString, FBridgeSchemaProperty> USetNodePositionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Material, Blueprint, or AnimBlueprint");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty GraphName;
	GraphName.Type = TEXT("string");
	GraphName.Description = TEXT("Target graph name (for Blueprint/Anim). Default: 'EventGraph'. Ignored for Material assets.");
	GraphName.bRequired = false;
	Schema.Add(TEXT("graph_name"), GraphName);

	FBridgeSchemaProperty Positions;
	Positions.Type = TEXT("array");
	Positions.Description = TEXT("Array of {guid: string, x: number, y: number} entries specifying new positions for each node");
	Positions.bRequired = true;
	Schema.Add(TEXT("positions"), Positions);

	return Schema;
}

TArray<FString> USetNodePositionTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("positions") };
}

FBridgeToolResult USetNodePositionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString GraphName = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT("EventGraph"));

	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	// Parse positions array
	const TArray<TSharedPtr<FJsonValue>>* PositionsArray;
	if (!Arguments->TryGetArrayField(TEXT("positions"), PositionsArray))
	{
		return FBridgeToolResult::Error(TEXT("positions array is required"));
	}

	struct FNodePosition
	{
		FGuid Guid;
		FString GuidString;
		int32 X;
		int32 Y;
	};

	TArray<FNodePosition> RequestedPositions;
	for (const TSharedPtr<FJsonValue>& Entry : *PositionsArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Entry->TryGetObject(EntryObj))
		{
			return FBridgeToolResult::Error(TEXT("Each position entry must be an object with guid, x, y"));
		}

		FString GuidStr;
		if (!(*EntryObj)->TryGetStringField(TEXT("guid"), GuidStr))
		{
			return FBridgeToolResult::Error(TEXT("Each position entry must have a 'guid' string field"));
		}

		FGuid ParsedGuid;
		if (!FGuid::Parse(GuidStr, ParsedGuid))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid GUID format: %s"), *GuidStr));
		}

		double XVal, YVal;
		if (!(*EntryObj)->TryGetNumberField(TEXT("x"), XVal) || !(*EntryObj)->TryGetNumberField(TEXT("y"), YVal))
		{
			return FBridgeToolResult::Error(TEXT("Each position entry must have 'x' and 'y' number fields"));
		}

		FNodePosition Pos;
		Pos.Guid = ParsedGuid;
		Pos.GuidString = GuidStr;
		Pos.X = FMath::RoundToInt(XVal);
		Pos.Y = FMath::RoundToInt(YVal);
		RequestedPositions.Add(Pos);
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-node-position: %s, %d positions"), *AssetPath, RequestedPositions.Num());

	// Handle empty positions array as no-op success
	if (RequestedPositions.Num() == 0)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset"), AssetPath);
		Result->SetArrayField(TEXT("moved"), {});
		Result->SetArrayField(TEXT("not_found"), {});
		Result->SetBoolField(TEXT("needs_save"), false);
		return FBridgeToolResult::Json(Result);
	}

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "SetNodePos", "Set {0} node positions in {1}"),
			FText::AsNumber(RequestedPositions.Num()),
			FText::FromString(AssetPath)));

	TArray<TSharedPtr<FJsonValue>> MovedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	// Handle Material
	if (UMaterial* Material = Cast<UMaterial>(Object))
	{
		FBridgeAssetModifier::MarkModified(Material);

		TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressionCollection().Expressions;

		for (const FNodePosition& Pos : RequestedPositions)
		{
			bool bFound = false;
			for (UMaterialExpression* Expr : Expressions)
			{
				if (Expr && Expr->MaterialExpressionGuid == Pos.Guid)
				{
					Expr->MaterialExpressionEditorX = Pos.X;
					Expr->MaterialExpressionEditorY = Pos.Y;

					TSharedPtr<FJsonObject> MovedEntry = MakeShareable(new FJsonObject);
					MovedEntry->SetStringField(TEXT("guid"), Pos.Guid.ToString(EGuidFormats::DigitsWithHyphens));
					MovedEntry->SetNumberField(TEXT("x"), Pos.X);
					MovedEntry->SetNumberField(TEXT("y"), Pos.Y);
					MovedArray.Add(MakeShareable(new FJsonValueObject(MovedEntry)));

					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				NotFoundArray.Add(MakeShareable(new FJsonValueString(Pos.GuidString)));
			}
		}

		FBridgeAssetModifier::RefreshMaterial(Material);
		FBridgeAssetModifier::MarkPackageDirty(Material);
	}
	// Handle Blueprint / AnimBlueprint
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		UEdGraph* TargetGraph = FBridgeAssetModifier::FindGraphByName(Blueprint, GraphName);
		if (!TargetGraph)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		}

		FBridgeAssetModifier::MarkModified(Blueprint);

		for (const FNodePosition& Pos : RequestedPositions)
		{
			bool bFound = false;
			for (UEdGraphNode* Node : TargetGraph->Nodes)
			{
				if (Node && Node->NodeGuid == Pos.Guid)
				{
					Node->NodePosX = Pos.X;
					Node->NodePosY = Pos.Y;

					TSharedPtr<FJsonObject> MovedEntry = MakeShareable(new FJsonObject);
					MovedEntry->SetStringField(TEXT("guid"), Pos.Guid.ToString(EGuidFormats::DigitsWithHyphens));
					MovedEntry->SetNumberField(TEXT("x"), Pos.X);
					MovedEntry->SetNumberField(TEXT("y"), Pos.Y);
					MovedArray.Add(MakeShareable(new FJsonValueObject(MovedEntry)));

					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				NotFoundArray.Add(MakeShareable(new FJsonValueString(Pos.GuidString)));
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FBridgeAssetModifier::MarkPackageDirty(Blueprint);
	}
	else
	{
		return FBridgeToolResult::Error(TEXT("Asset must be a Material, Blueprint, or AnimBlueprint"));
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetArrayField(TEXT("moved"), MovedArray);
	Result->SetArrayField(TEXT("not_found"), NotFoundArray);
	Result->SetBoolField(TEXT("needs_save"), MovedArray.Num() > 0);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-node-position: Moved %d nodes, %d not found"),
		MovedArray.Num(), NotFoundArray.Num());

	return FBridgeToolResult::Json(Result);
}
