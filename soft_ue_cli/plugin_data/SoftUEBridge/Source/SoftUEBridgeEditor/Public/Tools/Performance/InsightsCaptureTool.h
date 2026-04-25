// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InsightsCaptureTool.generated.h"

/**
 * Tool for controlling Unreal Insights trace capture
 * Actions: start, stop, status
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInsightsCaptureTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("insights-capture"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;

private:
	/** Start trace capture */
	FBridgeToolResult StartCapture(const TSharedPtr<FJsonObject>& Arguments);

	/** Stop trace capture */
	FBridgeToolResult StopCapture();

	/** Get capture status */
	FBridgeToolResult GetStatus();
};
