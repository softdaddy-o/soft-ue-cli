// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/CaptureViewportTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "RenderingThread.h"
#endif

REGISTER_BRIDGE_TOOL(UCaptureViewportTool)

TMap<FString, FBridgeSchemaProperty> UCaptureViewportTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("source"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Which viewport to capture: 'auto' (default, tries game then editor), 'game' for PIE/standalone, 'editor' for the level editor viewport"),
		false,
		{TEXT("auto"), TEXT("game"), TEXT("editor")}
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' saves to temp dir and returns path (default), 'base64' returns encoded data"),
		false,
		{TEXT("file"), TEXT("base64")}
	});

	return Schema;
}

FBridgeToolResult UCaptureViewportTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Source = GetStringArgOrDefault(Arguments, TEXT("source"), TEXT("auto"));
	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString OutputMode = GetStringArgOrDefault(Arguments, TEXT("output"), TEXT("file"));

	if (Source == TEXT("editor"))
	{
#if WITH_EDITOR
		return CaptureEditorViewport(Format, OutputMode);
#else
		return FBridgeToolResult::Error(TEXT("Editor viewport capture is only available in editor builds"));
#endif
	}

	if (Source == TEXT("game"))
	{
		return CaptureGameViewport(Format, OutputMode);
	}

	if (Source == TEXT("auto"))
	{
		// Auto-detect: try game viewport first, fall back to editor viewport
		FBridgeToolResult GameResult = CaptureGameViewport(Format, OutputMode);
		if (!GameResult.bIsError)
		{
			return GameResult;
		}

#if WITH_EDITOR
		return CaptureEditorViewport(Format, OutputMode);
#else
		return GameResult; // Return the game viewport error in non-editor builds
#endif
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Unknown source '%s'. Valid values: 'auto', 'game', 'editor'"), *Source));
}

FBridgeToolResult UCaptureViewportTool::CaptureGameViewport(const FString& Format, const FString& OutputMode)
{
	// Find a game viewport — works for both PIE and standalone
	FViewport* GameViewport = nullptr;
	FString WorldTypeName;

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		const bool bHasViewport = WorldContext.GameViewport && WorldContext.GameViewport->Viewport;
		const bool bIsPlayable = WorldContext.WorldType == EWorldType::PIE
			|| WorldContext.WorldType == EWorldType::Game;

		if (!bHasViewport || !bIsPlayable)
		{
			continue;
		}

		GameViewport = WorldContext.GameViewport->Viewport;
		WorldTypeName = (WorldContext.WorldType == EWorldType::PIE) ? TEXT("PIE") : TEXT("Standalone");
		break;
	}

	if (!GameViewport)
	{
		return FBridgeToolResult::Error(
			TEXT("No game viewport found. Start a PIE session or run as standalone first."));
	}

	// ReadPixels internally enqueues a render command and flushes,
	// so it handles render thread synchronization.
	TArray<FColor> RawData;
	if (!GameViewport->ReadPixels(RawData))
	{
		return FBridgeToolResult::Error(TEXT("Failed to read pixels from game viewport"));
	}

	const FIntPoint ViewportSize = GameViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || RawData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Game viewport has no valid image data"));
	}

	TArray<uint8> ImageData = CompressImage(RawData, ViewportSize.X, ViewportSize.Y, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress viewport screenshot"));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured %s viewport %dx%d as %s (%d bytes)"),
		*WorldTypeName, ViewportSize.X, ViewportSize.Y, *Format, ImageData.Num());

	return OutputImage(ImageData, Format, OutputMode);
}

#if WITH_EDITOR
FBridgeToolResult UCaptureViewportTool::CaptureEditorViewport(const FString& Format, const FString& OutputMode)
{
	if (!GEditor)
	{
		return FBridgeToolResult::Error(
			TEXT("GEditor is not available. The editor may not be fully initialized."));
	}

	FViewport* EditorViewport = GEditor->GetActiveViewport();
	if (!EditorViewport)
	{
		return FBridgeToolResult::Error(
			TEXT("No active editor viewport found. Click on the level editor viewport first."));
	}

	const FIntPoint ViewportSize = EditorViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return FBridgeToolResult::Error(TEXT("Editor viewport has invalid size"));
	}

	// Editor viewports render to a render target (not a swapchain backbuffer),
	// so we can force a draw and then read pixels directly.
	EditorViewport->Draw(false);
	FlushRenderingCommands();

	TArray<FColor> RawData;
	if (!EditorViewport->ReadPixels(RawData))
	{
		return FBridgeToolResult::Error(TEXT("Failed to read pixels from editor viewport"));
	}

	if (RawData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Editor viewport returned no pixel data"));
	}

	TArray<uint8> ImageData = CompressImage(RawData, ViewportSize.X, ViewportSize.Y, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress editor viewport screenshot"));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured editor viewport %dx%d as %s (%d bytes)"),
		ViewportSize.X, ViewportSize.Y, *Format, ImageData.Num());

	return OutputImage(ImageData, Format, OutputMode);
}
#endif

TArray<uint8> UCaptureViewportTool::CompressImage(
	TArray<FColor>& RawData,
	int32 Width,
	int32 Height,
	const FString& Format)
{
	// Validate pixel count matches dimensions (can diverge if viewport resizes mid-capture)
	if (RawData.Num() != Width * Height)
	{
		return {};
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const EImageFormat ImageFormat = (Format == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		return {};
	}

	// DX12/Vulkan backbuffers may have undefined alpha (often 0),
	// which produces a fully transparent PNG, so force alpha to 255.
	for (FColor& Pixel : RawData)
	{
		Pixel.A = 255;
	}

	// FColor is BGRA in memory on little-endian; pass directly using ERGBFormat::BGRA
	if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num() * 4, Width, Height, ERGBFormat::BGRA, 8))
	{
		return {};
	}

	const int32 Quality = (Format == TEXT("jpeg")) ? 85 : 0;
	const TArray<uint8, FDefaultAllocator64> CompressedData64 = ImageWrapper->GetCompressed(Quality);
	TArray<uint8> Result;
	Result.Append(CompressedData64);
	return Result;
}

FBridgeToolResult UCaptureViewportTool::OutputImage(
	const TArray<uint8>& ImageData,
	const FString& Format,
	const FString& OutputMode)
{
	// Base64 mode
	if (OutputMode == TEXT("base64"))
	{
		const FString MimeType = Format == TEXT("jpeg") ? TEXT("image/jpeg") : TEXT("image/png");
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));
		Result->SetStringField(TEXT("mime_type"), MimeType);
		Result->SetStringField(TEXT("mode"), TEXT("base64"));
		Result->SetStringField(TEXT("format"), Format);
		Result->SetNumberField(TEXT("size_bytes"), ImageData.Num());
		return FBridgeToolResult::Json(Result);
	}

	// File mode — save to temp directory
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	CleanupPreviousCaptures(TempDir);

	const FString Hash = FMD5::HashBytes(ImageData.GetData(), FMath::Min(1024, ImageData.Num()));
	const FString FileName = FString::Printf(TEXT("viewport_%s.%s"), *Hash.Left(8), *Format);
	const FString FilePath = FPaths::Combine(TempDir, FileName);

	if (!FFileHelper::SaveArrayToFile(ImageData, *FilePath))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to write viewport capture to %s"), *FilePath));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("mode"), TEXT("file"));
	Result->SetStringField(TEXT("file_path"), FilePath);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetNumberField(TEXT("size_bytes"), ImageData.Num());
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Viewport screenshot saved to %s"), *FilePath));
	return FBridgeToolResult::Json(Result);
}

void UCaptureViewportTool::CleanupPreviousCaptures(const FString& TempDir)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *(TempDir / TEXT("viewport_*")), true, false);

	for (const FString& FileName : FoundFiles)
	{
		const FString FullPath = FPaths::Combine(TempDir, FileName);
		IFileManager::Get().Delete(*FullPath, false, true);
	}
}
