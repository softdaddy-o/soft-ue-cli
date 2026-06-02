// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "PieTickTool.generated.h"

/**
 * Advance the PIE world by a fixed number of frames at a pinned delta time.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UPieTickTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-tick"); }
	virtual FString GetToolDescription() const override;
	virtual EBridgeToolExecutionContext GetExecutionContextRequirement() const override
	{
		return EBridgeToolExecutionContext::PIEWorldTickSafe;
	}
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("frames")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UWorld* GetPIEWorld() const;
	bool StartPIEForTick(
		const FString& MapPath,
		double OperationStartSeconds,
		double OperationDeadlineSeconds,
		int32 RequestedFrames,
		TSharedPtr<FJsonObject>& OutFailure);
	bool TickWorldFrames(
		UWorld* World,
		int32 Frames,
		float DeltaSeconds,
		float TimeoutSeconds,
		double OperationStartSeconds,
		double OperationDeadlineSeconds,
		int32& OutFramesTicked,
		float& OutWorldTimeBefore,
		float& OutWorldTimeAfter,
		TSharedPtr<FJsonObject>& OutFailure);
};
