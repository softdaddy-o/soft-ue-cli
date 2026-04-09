// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "Tools/BridgeToolBase.h"
#include "GetConfigValueTool.generated.h"

UCLASS()
class UGetConfigValueTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-config-value"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("section"), TEXT("key"), TEXT("config_type")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;

	static FString ConfigTypeToFilename(const FString& ConfigType);
	static bool TryGetConfigValue(const FString& Section, const FString& Key, const FString& Filename, FString& OutValue, bool* bOutHasSection = nullptr);
};
