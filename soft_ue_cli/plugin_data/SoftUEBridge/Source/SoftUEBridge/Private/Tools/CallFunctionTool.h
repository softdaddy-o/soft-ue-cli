// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "Tools/BridgeToolBase.h"
#include "CallFunctionTool.generated.h"

/**
 * Call a Blueprint or native function against an actor, a class default object,
 * or a transient instance constructed just for the call.
 */
UCLASS()
class UCallFunctionTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("call-function"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("function_name")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
