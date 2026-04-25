// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Editor/CaptureScreenshotTool.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"
#include "SoftUEBridgeEditorModule.h"

TMap<FString, FBridgeSchemaProperty> UCaptureScreenshotTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Capture mode: 'window' (entire editor), 'tab' (specific panel), or 'region' (coordinates)"),
		true,
		{TEXT("window"), TEXT("tab"), TEXT("region")}
	});

	Schema.Add(TEXT("window_name"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("For 'tab' mode: Name of the editor tab/panel to capture (e.g., Blueprint, Material, OutputLog)"),
		false
	});

	Schema.Add(TEXT("region"), FBridgeSchemaProperty{
		TEXT("array"),
		TEXT("For 'region' mode: Screen coordinates [x, y, width, height] to capture"),
		false
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' writes to temp directory (default), 'base64' returns encoded data"),
		false,
		{TEXT("file"), TEXT("base64")}
	});

	return Schema;
}

TArray<FString> UCaptureScreenshotTool::GetRequiredParams() const
{
	return {TEXT("mode")};
}

FBridgeToolResult UCaptureScreenshotTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("window"));
	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString OutputMode = GetStringArgOrDefault(Arguments, TEXT("output"), TEXT("file"));

	// Validate Slate is available
	if (!FSlateApplication::IsInitialized())
	{
		return FBridgeToolResult::Error(TEXT("Slate application not initialized"));
	}

	// Route to appropriate capture mode
	if (Mode == TEXT("window"))
	{
		return CaptureWindow(Format, OutputMode);
	}
	else if (Mode == TEXT("tab"))
	{
		const FString WindowName = GetStringArgOrDefault(Arguments, TEXT("window_name"), TEXT(""));
		if (WindowName.IsEmpty())
		{
			return FBridgeToolResult::Error(TEXT("'window_name' is required for 'tab' mode"));
		}
		return CaptureTab(WindowName, Format, OutputMode);
	}
	else if (Mode == TEXT("region"))
	{
		const TArray<TSharedPtr<FJsonValue>>* RegionArray = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("region"), RegionArray) || !RegionArray || RegionArray->Num() != 4)
		{
			return FBridgeToolResult::Error(TEXT("'region' must be an array of 4 integers [x, y, width, height]"));
		}

		FIntRect Region(
			(*RegionArray)[0]->AsNumber(),
			(*RegionArray)[1]->AsNumber(),
			(*RegionArray)[0]->AsNumber() + (*RegionArray)[2]->AsNumber(),
			(*RegionArray)[1]->AsNumber() + (*RegionArray)[3]->AsNumber()
		);

		return CaptureRegion(Region, Format, OutputMode);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid mode '%s'. Must be 'window', 'tab', or 'region'."), *Mode));
	}
}

FBridgeToolResult UCaptureScreenshotTool::CaptureWindow(const FString& Format, const FString& OutputMode)
{
	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No active editor window found"));
	}

	return CaptureFromWindow(ActiveWindow, Format, OutputMode);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureTab(
	const FString& TabName,
	const FString& Format,
	const FString& OutputMode)
{
	// Try to find the tab by name
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*TabName));

	if (!Tab.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' not found or not currently open"), *TabName));
	}

	// Get the tab's content widget
	TSharedPtr<SWidget> Content = Tab->GetContent();
	if (!Content.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' has no content to capture"), *TabName));
	}

	// Capture the widget
	TArray<FColor> RawData;
	int32 Width, Height;
	if (!TakeWidgetScreenshot(Content.ToSharedRef(), RawData, Width, Height))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture tab screenshot"));
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, Width, Height, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(ImageData, Format, OutputMode, FString::Printf(TEXT("tab '%s'"), *TabName));
}

FBridgeToolResult UCaptureScreenshotTool::CaptureRegion(
	const FIntRect& Region,
	const FString& Format,
	const FString& OutputMode)
{
	// Get the virtual desktop geometry
	FSlateRect VirtualDesktopRect = FSlateApplication::Get().GetWorkArea(FSlateRect());
	FVector2D ScreenSize = VirtualDesktopRect.GetSize();

	// Validate region is within screen bounds
	if (Region.Min.X < 0 || Region.Min.Y < 0 ||
		Region.Max.X > ScreenSize.X || Region.Max.Y > ScreenSize.Y)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Region [%d, %d, %d, %d] is outside screen bounds [0, 0, %d, %d]"),
			Region.Min.X, Region.Min.Y, Region.Max.X, Region.Max.Y,
			(int32)ScreenSize.X, (int32)ScreenSize.Y));
	}

	// Get active window for screenshot
	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No active editor window found for region capture"));
	}

	// Take screenshot using Slate
	TArray<FColor> RawData;
	FIntVector OutSize;

	if (!FSlateApplication::Get().TakeScreenshot(
		ActiveWindow.ToSharedRef(),
		Region,
		RawData,
		OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture region screenshot"));
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, OutSize.X, OutSize.Y, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(ImageData, Format, OutputMode,
		FString::Printf(TEXT("region [%d, %d, %dx%d]"),
			Region.Min.X, Region.Min.Y, Region.Width(), Region.Height()));
}

FBridgeToolResult UCaptureScreenshotTool::CaptureFromWindow(
	TSharedPtr<SWindow> Window,
	const FString& Format,
	const FString& OutputMode)
{
	if (!Window.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Invalid window"));
	}

	TArray<FColor> RawData;
	FIntVector OutSize;

	// Capture the window
	if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), RawData, OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture window screenshot"));
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, OutSize.X, OutSize.Y, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(ImageData, Format, OutputMode, TEXT("editor window"));
}

bool UCaptureScreenshotTool::TakeWidgetScreenshot(
	TSharedRef<SWidget> Widget,
	TArray<FColor>& OutImageData,
	int32& OutWidth,
	int32& OutHeight)
{
	// Get widget geometry
	FGeometry WidgetGeometry = Widget->GetCachedGeometry();
	FVector2D LocalSize = WidgetGeometry.GetLocalSize();

	OutWidth = FMath::TruncToInt(LocalSize.X);
	OutHeight = FMath::TruncToInt(LocalSize.Y);

	if (OutWidth <= 0 || OutHeight <= 0)
	{
		return false;
	}

	// Create render target
	FIntPoint Size(OutWidth, OutHeight);
	TArray<FColor> ColorData;

	// Use Slate's screenshot functionality
	FIntVector OutSize;
	FSlateRect WidgetRect = WidgetGeometry.GetRenderBoundingRect();

	// Get the window containing this widget
	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
	if (!WidgetWindow.IsValid())
	{
		return false;
	}

	// Take screenshot of the widget area
	FIntRect CaptureRect(
		FMath::TruncToInt(WidgetRect.Left),
		FMath::TruncToInt(WidgetRect.Top),
		FMath::TruncToInt(WidgetRect.Right),
		FMath::TruncToInt(WidgetRect.Bottom)
	);

	if (!FSlateApplication::Get().TakeScreenshot(WidgetWindow.ToSharedRef(), CaptureRect, ColorData, OutSize))
	{
		return false;
	}

	OutImageData = MoveTemp(ColorData);
	OutWidth = OutSize.X;
	OutHeight = OutSize.Y;

	return true;
}

TArray<uint8> UCaptureScreenshotTool::CompressImage(
	const TArray<FColor>& RawData,
	int32 Width,
	int32 Height,
	const FString& Format)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = (Format == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		return {};
	}

	// Convert FColor to raw RGBA bytes
	// Note: FColor stores data as BGRA internally, but IImageWrapper expects RGBA
	// Manual channel reordering is required for correct color output
	const int32 DataSize = Width * Height * 4;
	TArray<uint8> RawBytes;
	RawBytes.SetNumUninitialized(DataSize);

	for (int32 i = 0; i < RawData.Num(); ++i)
	{
		RawBytes[i * 4 + 0] = RawData[i].R;
		RawBytes[i * 4 + 1] = RawData[i].G;
		RawBytes[i * 4 + 2] = RawData[i].B;
		RawBytes[i * 4 + 3] = RawData[i].A;
	}

	// Set image data
	if (!ImageWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		return {};
	}

	// Get compressed data
	int32 Quality = (Format == TEXT("jpeg")) ? 85 : 0; // PNG ignores quality
	TArray<uint8, FDefaultAllocator64> CompressedData64 = ImageWrapper->GetCompressed(Quality);
	// Convert from FDefaultAllocator64 to FDefaultAllocator
	TArray<uint8> CompressedData;
	CompressedData.Append(CompressedData64);
	return CompressedData;
}

FBridgeToolResult UCaptureScreenshotTool::OutputImage(
	const TArray<uint8>& ImageData,
	const FString& Format,
	const FString& OutputMode,
	const FString& CaptureMode)
{
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Empty image data"));
	}

	// Base64 mode - return image content
	if (OutputMode == TEXT("base64"))
	{
		const FString MimeType = Format == TEXT("jpeg") ? TEXT("image/jpeg") : TEXT("image/png");

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-screenshot: Captured %s as %s (%d bytes)"),
			*CaptureMode, *Format, ImageData.Num());

		TSharedPtr<FJsonObject> ImageResult = MakeShareable(new FJsonObject);
		ImageResult->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));
		ImageResult->SetStringField(TEXT("mime_type"), MimeType);
		ImageResult->SetStringField(TEXT("mode"), TEXT("base64"));
		ImageResult->SetStringField(TEXT("captured"), CaptureMode);
		ImageResult->SetStringField(TEXT("format"), Format);
		ImageResult->SetNumberField(TEXT("size_bytes"), ImageData.Num());
		return FBridgeToolResult::Json(ImageResult);
	}

	// File mode - write to temp directory
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	CleanupPreviousScreenshots(TempDir);

	// Generate filename from content hash
	const FString Hash = FMD5::HashBytes(ImageData.GetData(), FMath::Min(1024, ImageData.Num()));
	const FString FileName = FString::Printf(TEXT("screenshot_%s.%s"), *Hash.Left(8), *Format);
	const FString FilePath = FPaths::Combine(TempDir, FileName);

	// Write file
	if (!FFileHelper::SaveArrayToFile(ImageData, *FilePath))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to write screenshot to %s"), *FilePath));
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("mode"), TEXT("file"));
	Result->SetStringField(TEXT("file_path"), FilePath);
	Result->SetStringField(TEXT("captured"), CaptureMode);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetNumberField(TEXT("size_bytes"), ImageData.Num());
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Screenshot of %s saved to %s"), *CaptureMode, *FilePath));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-screenshot: Captured %s as %s -> %s (%d bytes)"),
		*CaptureMode, *Format, *FilePath, ImageData.Num());

	return FBridgeToolResult::Json(Result);
}

void UCaptureScreenshotTool::CleanupPreviousScreenshots(const FString& TempDir)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *(TempDir / TEXT("screenshot_*")), true, false);

	for (const FString& FileName : FoundFiles)
	{
		const FString FullPath = FPaths::Combine(TempDir, FileName);
		IFileManager::Get().Delete(*FullPath, false, true);
	}
}
