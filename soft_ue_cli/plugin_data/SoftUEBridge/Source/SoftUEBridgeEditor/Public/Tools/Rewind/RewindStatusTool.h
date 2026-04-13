// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "RewindStatusTool.generated.h"

UCLASS()
class SOFTUEBRIDGEEDITOR_API URewindStatusTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("rewind-status"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
