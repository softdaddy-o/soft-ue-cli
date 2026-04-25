// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "Tools/BridgeToolBase.h"
#include "SetPropertyTool.generated.h"

UCLASS()
class USetPropertyTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("set-property"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("actor_name"), TEXT("property_name"), TEXT("value")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
