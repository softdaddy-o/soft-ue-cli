// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InsightsListTracesTool.generated.h"

/**
 * Tool for listing available Unreal Insights trace files
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInsightsListTracesTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("insights-list-traces"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;
};
