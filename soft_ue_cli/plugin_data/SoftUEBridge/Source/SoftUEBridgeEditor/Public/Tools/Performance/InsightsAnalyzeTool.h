// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InsightsAnalyzeTool.generated.h"

/**
 * Tool for analyzing Unreal Insights trace files
 * Analysis types: frame_stats, basic_info
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInsightsAnalyzeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("insights-analyze"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;

private:
	/** Get basic trace file info */
	FBridgeToolResult AnalyzeBasicInfo(const FString& TraceFile);
};
