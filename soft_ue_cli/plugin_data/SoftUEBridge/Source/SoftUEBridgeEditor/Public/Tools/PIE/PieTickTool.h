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
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("frames")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UWorld* GetPIEWorld() const;
	bool StartPIEForTick(const FString& MapPath, FString& OutError);
	float TickWorldFrames(UWorld* World, int32 Frames, float DeltaSeconds);
};
