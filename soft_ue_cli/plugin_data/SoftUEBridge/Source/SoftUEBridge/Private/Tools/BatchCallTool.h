// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "BatchCallTool.generated.h"

/**
 * Dispatch multiple bridge tool calls in-process, in order.
 */
UCLASS()
class UBatchCallTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("batch-call"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("calls")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
