// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InsertGraphNodeTool.generated.h"

/**
 * Atomically insert a new node between two connected nodes in a Blueprint graph.
 * Disconnects source→target, creates the new node, and wires source→new→target.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInsertGraphNodeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("insert-graph-node"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
