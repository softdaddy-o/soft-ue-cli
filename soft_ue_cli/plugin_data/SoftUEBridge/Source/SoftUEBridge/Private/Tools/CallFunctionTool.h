// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "Tools/BridgeToolBase.h"
#include "CallFunctionTool.generated.h"

UCLASS()
class UCallFunctionTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("call-function"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("actor_name"), TEXT("function_name")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
