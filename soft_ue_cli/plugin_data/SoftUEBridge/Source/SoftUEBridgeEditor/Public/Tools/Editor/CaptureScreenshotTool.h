// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "CaptureScreenshotTool.generated.h"

/**
 * Tool to capture screenshots of editor windows, tabs, regions, or the game viewport.
 *
 * Supports four capture modes:
 * 1. Window Mode: Captures the entire editor window
 * 2. Tab Mode: Captures a specific editor tab/panel (Blueprint Editor, Material Editor, etc.)
 * 3. Region Mode: Captures a specific rectangular region by coordinates
 * 4. Viewport Mode: Captures the PIE game screen (delegates to runtime capture-viewport tool)
 *
 * Output can be saved to a file or returned as base64-encoded image data.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UCaptureScreenshotTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-screenshot"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Capture screenshots of editor windows, tabs, regions, or the game viewport. "
				   "Use 'mode' to specify capture type: 'window' (entire editor), "
				   "'tab' (specific editor panel), 'region' (rectangular area), "
				   "or 'viewport' (PIE game screen). "
				   "Output as file or base64-encoded image data.");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/**
	 * Capture the PIE game viewport.
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureViewport(const FString& Format, const FString& OutputMode);

	/**
	 * Capture the entire editor window.
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureWindow(const FString& Format, const FString& OutputMode);

	/**
	 * Capture a specific editor tab/panel.
	 * @param TabName - Name of the tab to capture
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureTab(const FString& TabName, const FString& Format, const FString& OutputMode);

	/**
	 * Capture a specific rectangular region.
	 * @param Region - Rectangle to capture [x, y, width, height]
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureRegion(const FIntRect& Region, const FString& Format, const FString& OutputMode);

	/**
	 * Capture screenshot from a specific window.
	 * @param Window - Window to capture
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureFromWindow(
		TSharedPtr<SWindow> Window,
		const FString& Format,
		const FString& OutputMode);

	/**
	 * Take a screenshot of a widget and return raw RGBA data.
	 * @param Widget - Widget to capture
	 * @param OutImageData - Output raw RGBA pixel data
	 * @param OutWidth - Output image width
	 * @param OutHeight - Output image height
	 * @return True if successful
	 */
	bool TakeWidgetScreenshot(
		TSharedRef<SWidget> Widget,
		TArray<FColor>& OutImageData,
		int32& OutWidth,
		int32& OutHeight);

	/**
	 * Compress raw RGBA image data to PNG or JPEG.
	 * @param RawData - Raw RGBA pixel data
	 * @param Width - Image width
	 * @param Height - Image height
	 * @param Format - Target format ("png" or "jpeg")
	 * @return Compressed image data
	 */
	TArray<uint8> CompressImage(
		const TArray<FColor>& RawData,
		int32 Width,
		int32 Height,
		const FString& Format);

	/**
	 * Write image data to file or return as base64.
	 * @param ImageData - Compressed image data
	 * @param Format - Image format ("png" or "jpeg")
	 * @param OutputMode - Output mode ("file" or "base64")
	 * @param CaptureMode - Description of what was captured (for response message)
	 * @return Tool result with file path or base64 data
	 */
	FBridgeToolResult OutputImage(
		const TArray<uint8>& ImageData,
		const FString& Format,
		const FString& OutputMode,
		const FString& CaptureMode);

	/**
	 * Delete previous screenshot files.
	 * @param TempDir - Directory to clean
	 */
	void CleanupPreviousScreenshots(const FString& TempDir);
};
