// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "CaptureViewportTool.generated.h"

/**
 * Capture the game viewport (PIE or standalone).
 * Saves to a temp file and returns the path, or returns base64 data.
 */
UCLASS()
class UCaptureViewportTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-viewport"); }
	virtual FString GetToolDescription() const override
	{
		return TEXT("Capture the game viewport screenshot (PIE or standalone). "
				   "Returns a temp file path by default, or base64-encoded data.");
	}
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	TArray<uint8> CompressImage(
		const TArray<FColor>& RawData,
		int32 Width,
		int32 Height,
		const FString& Format);

	FBridgeToolResult OutputImage(
		const TArray<uint8>& ImageData,
		const FString& Format,
		const FString& OutputMode);

	void CleanupPreviousCaptures(const FString& TempDir);
};
