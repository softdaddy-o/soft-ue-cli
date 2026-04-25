// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "SetNodePositionTool.generated.h"

/**
 * Batch-set editor positions for nodes in a Material, Blueprint, or AnimBlueprint graph.
 * Nodes are identified by GUID. All moves happen in a single undo transaction.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API USetNodePositionTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("set-node-position"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
