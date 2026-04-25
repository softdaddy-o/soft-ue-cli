// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "SpawnActorTool.generated.h"

UCLASS()
class SOFTUEBRIDGE_API USpawnActorTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("spawn-actor"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("actor_class")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
