// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/CaptureViewportTool.h"
#include "SoftUEBridgeModule.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
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

namespace
{
	FString NormalizeColorMode(FString ColorMode)
	{
		ColorMode.TrimStartAndEndInline();
		ColorMode.ToLowerInline();
		return ColorMode.IsEmpty() ? TEXT("color") : ColorMode;
	}

	FIntPoint ResolveCaptureSize(
		int32 OriginalWidth,
		int32 OriginalHeight,
		float ScalePercent,
		int32 TargetWidth,
		int32 TargetHeight)
	{
		float Scale = 1.0f;
		if (TargetWidth > 0 && TargetHeight > 0)
		{
			Scale = FMath::Min(
				static_cast<float>(TargetWidth) / static_cast<float>(OriginalWidth),
				static_cast<float>(TargetHeight) / static_cast<float>(OriginalHeight));
		}
		else if (TargetWidth > 0)
		{
			Scale = static_cast<float>(TargetWidth) / static_cast<float>(OriginalWidth);
		}
		else if (TargetHeight > 0)
		{
			Scale = static_cast<float>(TargetHeight) / static_cast<float>(OriginalHeight);
		}
		else
		{
			Scale = ScalePercent / 100.0f;
		}

		Scale = FMath::Clamp(Scale, 0.0f, 1.0f);
		if (Scale >= 1.0f)
		{
			return FIntPoint(OriginalWidth, OriginalHeight);
		}

		return FIntPoint(
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(OriginalWidth) * Scale)),
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(OriginalHeight) * Scale)));
	}

	bool ResizeImage(TArray<FColor>& ImageData, int32& Width, int32& Height, const FIntPoint& TargetSize)
	{
		if (TargetSize.X == Width && TargetSize.Y == Height)
		{
			return true;
		}

		if (TargetSize.X <= 0 || TargetSize.Y <= 0 || Width <= 0 || Height <= 0)
		{
			return false;
		}

		TArray<FColor> ResizedData;
		ResizedData.SetNumUninitialized(TargetSize.X * TargetSize.Y);

		for (int32 Y = 0; Y < TargetSize.Y; ++Y)
		{
			const int32 SourceY = FMath::Clamp(
				FMath::FloorToInt((static_cast<float>(Y) + 0.5f) * static_cast<float>(Height) / static_cast<float>(TargetSize.Y)),
				0,
				Height - 1);
			for (int32 X = 0; X < TargetSize.X; ++X)
			{
				const int32 SourceX = FMath::Clamp(
					FMath::FloorToInt((static_cast<float>(X) + 0.5f) * static_cast<float>(Width) / static_cast<float>(TargetSize.X)),
					0,
					Width - 1);
				ResizedData[Y * TargetSize.X + X] = ImageData[SourceY * Width + SourceX];
			}
		}

		ImageData = MoveTemp(ResizedData);
		Width = TargetSize.X;
		Height = TargetSize.Y;
		return true;
	}

	void ApplyColorMode(TArray<FColor>& ImageData, const FString& ColorMode)
	{
		for (FColor& Pixel : ImageData)
		{
			Pixel.A = 255;
			if (ColorMode == TEXT("color"))
			{
				continue;
			}

			const uint8 Luma = static_cast<uint8>((77 * Pixel.R + 150 * Pixel.G + 29 * Pixel.B) >> 8);
			const uint8 Value = (ColorMode == TEXT("monochrome"))
				? (Luma >= 128 ? 255 : 0)
				: Luma;
			Pixel.R = Value;
			Pixel.G = Value;
			Pixel.B = Value;
		}
	}

	bool ApplyImageTransform(
		TArray<FColor>& ImageData,
		int32& Width,
		int32& Height,
		float ScalePercent,
		int32 TargetWidth,
		int32 TargetHeight,
		const FString& ColorMode,
		FString& OutColorMode,
		FString& OutError)
	{
		if (Width <= 0 || Height <= 0 || ImageData.Num() != Width * Height)
		{
			OutError = TEXT("Invalid image dimensions");
			return false;
		}

		if (ScalePercent <= 0.0f)
		{
			OutError = TEXT("'scale' must be greater than 0");
			return false;
		}

		if (TargetWidth < 0 || TargetHeight < 0)
		{
			OutError = TEXT("'width' and 'height' must be greater than or equal to 0");
			return false;
		}

		OutColorMode = NormalizeColorMode(ColorMode);
		if (OutColorMode != TEXT("color") &&
			OutColorMode != TEXT("grayscale") &&
			OutColorMode != TEXT("monochrome"))
		{
			OutError = FString::Printf(
				TEXT("Invalid color_mode '%s'. Valid values: color, grayscale, monochrome"),
				*ColorMode);
			return false;
		}

		const FIntPoint TargetSize = ResolveCaptureSize(Width, Height, ScalePercent, TargetWidth, TargetHeight);
		if (!ResizeImage(ImageData, Width, Height, TargetSize))
		{
			OutError = TEXT("Failed to resize viewport screenshot");
			return false;
		}

		ApplyColorMode(ImageData, OutColorMode);
		return true;
	}
}

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

	Schema.Add(TEXT("scale"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Output scale percentage. 100 keeps current size; 50 halves width and height. Requests above 100 are clamped."),
		false
	});

	Schema.Add(TEXT("width"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output width in pixels. Height is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("height"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output height in pixels. Width is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("color_mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output color mode: color (default), grayscale, or monochrome"),
		false,
		{TEXT("color"), TEXT("grayscale"), TEXT("monochrome")}
	});

	Schema.Add(TEXT("cleanup_previous"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Delete previous viewport capture files before writing this file output (default: false)"),
		false
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
	const float ScalePercent = GetFloatArgOrDefault(Arguments, TEXT("scale"), 100.0f);
	const int32 TargetWidth = GetIntArgOrDefault(Arguments, TEXT("width"), 0);
	const int32 TargetHeight = GetIntArgOrDefault(Arguments, TEXT("height"), 0);
	const FString ColorMode = GetStringArgOrDefault(Arguments, TEXT("color_mode"), TEXT("color"));
	const bool bCleanupPrevious = GetBoolArgOrDefault(Arguments, TEXT("cleanup_previous"), false);

	if (Source == TEXT("editor"))
	{
#if WITH_EDITOR
		return CaptureEditorViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
#else
		return FBridgeToolResult::Error(TEXT("Editor viewport capture is only available in editor builds"));
#endif
	}

	if (Source == TEXT("game"))
	{
		return CaptureGameViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
	}

	if (Source == TEXT("auto"))
	{
		// Auto-detect: try game viewport first, fall back to editor viewport
		FBridgeToolResult GameResult = CaptureGameViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
		if (!GameResult.bIsError)
		{
			return GameResult;
		}

#if WITH_EDITOR
		return CaptureEditorViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
#else
		return GameResult; // Return the game viewport error in non-editor builds
#endif
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Unknown source '%s'. Valid values: 'auto', 'game', 'editor'"), *Source));
}

FBridgeToolResult UCaptureViewportTool::CaptureGameViewport(
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
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

	const int32 OriginalWidth = ViewportSize.X;
	const int32 OriginalHeight = ViewportSize.Y;
	int32 OutputWidth = OriginalWidth;
	int32 OutputHeight = OriginalHeight;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress viewport screenshot"));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured %s viewport %dx%d -> %dx%d as %s/%s (%d bytes)"),
		*WorldTypeName, OriginalWidth, OriginalHeight, OutputWidth, OutputHeight, *Format, *NormalizedColorMode, ImageData.Num());

	return OutputImage(
		ImageData,
		Format,
		OutputMode,
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious);
}

#if WITH_EDITOR
FBridgeToolResult UCaptureViewportTool::CaptureEditorViewport(
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
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

	const int32 OriginalWidth = ViewportSize.X;
	const int32 OriginalHeight = ViewportSize.Y;
	int32 OutputWidth = OriginalWidth;
	int32 OutputHeight = OriginalHeight;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress editor viewport screenshot"));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured editor viewport %dx%d -> %dx%d as %s/%s (%d bytes)"),
		OriginalWidth, OriginalHeight, OutputWidth, OutputHeight, *Format, *NormalizedColorMode, ImageData.Num());

	return OutputImage(
		ImageData,
		Format,
		OutputMode,
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious);
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
	const FString& OutputMode,
	int32 Width,
	int32 Height,
	int32 OriginalWidth,
	int32 OriginalHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
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
		Result->SetNumberField(TEXT("width"), Width);
		Result->SetNumberField(TEXT("height"), Height);
		Result->SetNumberField(TEXT("original_width"), OriginalWidth);
		Result->SetNumberField(TEXT("original_height"), OriginalHeight);
		Result->SetStringField(TEXT("color_mode"), ColorMode);
		return FBridgeToolResult::Json(Result);
	}

	// File mode — save to temp directory
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	if (bCleanupPrevious)
	{
		CleanupPreviousCaptures(TempDir);
	}

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
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("original_width"), OriginalWidth);
	Result->SetNumberField(TEXT("original_height"), OriginalHeight);
	Result->SetStringField(TEXT("color_mode"), ColorMode);
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
