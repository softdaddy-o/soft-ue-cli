// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "Tools/BridgeToolBase.h"
#include "QueryLevelTool.generated.h"

UCLASS()
class UQueryLevelTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("query-level"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;

private:
	TSharedPtr<FJsonObject> ActorToJson(AActor* Actor, bool bComponents, bool bTransform) const;
	TSharedPtr<FJsonObject> TransformToJson(const FTransform& T) const;
};
