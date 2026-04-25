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

REGISTER_BRIDGE_TOOL(UCaptureViewportTool)

TMap<FString, FBridgeSchemaProperty> UCaptureViewportTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

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
	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString OutputMode = GetStringArgOrDefault(Arguments, TEXT("output"), TEXT("file"));

	// Find a game viewport — works for both PIE and standalone
	FViewport* GameViewport = nullptr;
	FString WorldTypeName;

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.GameViewport && WorldContext.GameViewport->Viewport)
		{
			if (WorldContext.WorldType == EWorldType::PIE)
			{
				GameViewport = WorldContext.GameViewport->Viewport;
				WorldTypeName = TEXT("PIE");
				break;
			}
			else if (WorldContext.WorldType == EWorldType::Game)
			{
				GameViewport = WorldContext.GameViewport->Viewport;
				WorldTypeName = TEXT("Standalone");
				break;
			}
		}
	}

	if (!GameViewport)
	{
		return FBridgeToolResult::Error(
			TEXT("No game viewport found. Start a PIE session or run as standalone first."));
	}

	// Read pixels from the game viewport
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

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, ViewportSize.X, ViewportSize.Y, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress viewport screenshot"));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured %s viewport %dx%d as %s (%d bytes)"),
		*WorldTypeName, ViewportSize.X, ViewportSize.Y, *Format, ImageData.Num());

	return OutputImage(ImageData, Format, OutputMode);
}

TArray<uint8> UCaptureViewportTool::CompressImage(
	const TArray<FColor>& RawData,
	int32 Width,
	int32 Height,
	const FString& Format)
{
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = (Format == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		return {};
	}

	// FColor is BGRA internally; IImageWrapper expects RGBA
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

	if (!ImageWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		return {};
	}

	int32 Quality = (Format == TEXT("jpeg")) ? 85 : 0;
	TArray<uint8, FDefaultAllocator64> CompressedData64 = ImageWrapper->GetCompressed(Quality);
	TArray<uint8> CompressedData;
	CompressedData.Append(CompressedData64);
	return CompressedData;
}

FBridgeToolResult UCaptureViewportTool::OutputImage(
	const TArray<uint8>& ImageData,
	const FString& Format,
	const FString& OutputMode)
{
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Empty image data"));
	}

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
