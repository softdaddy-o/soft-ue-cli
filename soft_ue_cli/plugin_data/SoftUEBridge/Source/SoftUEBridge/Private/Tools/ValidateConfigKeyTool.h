// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "Tools/BridgeToolBase.h"
#include "ValidateConfigKeyTool.generated.h"

UCLASS()
class UValidateConfigKeyTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("validate-config-key"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("section"), TEXT("key"), TEXT("config_type")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
