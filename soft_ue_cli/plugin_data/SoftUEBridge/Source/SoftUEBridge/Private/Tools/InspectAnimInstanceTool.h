// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InspectAnimInstanceTool.generated.h"

class UAnimInstance;

/**
 * One-shot snapshot of a target actor's anim instance state.
 */
UCLASS()
class UInspectAnimInstanceTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("inspect-anim-instance"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("actor_tag")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UAnimInstance* ResolveAnimInstance(const FString& ActorTag, const FString& MeshComponentName, FString& OutError);
	TArray<TSharedPtr<FJsonValue>> ReadStateMachines(UAnimInstance* AnimInstance);
	TArray<TSharedPtr<FJsonValue>> ReadActiveMontages(UAnimInstance* AnimInstance);
	TArray<TSharedPtr<FJsonValue>> ReadSlots(UAnimInstance* AnimInstance);
	TArray<TSharedPtr<FJsonValue>> ReadNotifies(UAnimInstance* AnimInstance);
	TSharedPtr<FJsonObject> ReadBlendWeights(UAnimInstance* AnimInstance, const TArray<FString>& PropertyNames);
};
