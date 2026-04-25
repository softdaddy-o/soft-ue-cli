// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/GetAssetPreviewTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"

// Note: This tool is manually registered in SoftUEBridgeEditorModule.cpp
// REGISTER_BRIDGE_TOOL macro removed to avoid static initialization issues

TMap<FString, FBridgeSchemaProperty> UGetAssetPreviewTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Asset path (e.g., /Game/Textures/T_Player)"),
		true
	});

	Schema.Add(TEXT("resolution"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Output resolution in pixels (64-1024, default: 256)"),
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

FBridgeToolResult UGetAssetPreviewTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	const int32 Resolution = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("resolution"), 256), 64, 1024);
	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString OutputMode = GetStringArgOrDefault(Arguments, TEXT("output"), TEXT("file"));

	// Load asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Generate image data - use texture export for Texture2D, thumbnail for everything else
	TArray<uint8> ImageData;
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		ImageData = ExportTexture(Texture, Resolution, Format);
	}
	else
	{
		ImageData = RenderThumbnail(Asset, Resolution, Format);
	}

	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to generate preview image"));
	}

	// Return base64 encoded image as JSON
	if (OutputMode == TEXT("base64"))
	{
		const FString MimeType = Format == TEXT("jpeg") ? TEXT("image/jpeg") : TEXT("image/png");
		TSharedPtr<FJsonObject> ImageResult = MakeShareable(new FJsonObject);
		ImageResult->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));
		ImageResult->SetStringField(TEXT("mime_type"), MimeType);
		return FBridgeToolResult::Json(ImageResult);
	}

	// Write to temp file (default mode)
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	CleanupPreviousPreviewFiles(TempDir);

	const FString Hash = FMD5::HashBytes(ImageData.GetData(), FMath::Min(1024, ImageData.Num()));
	const FString FileName = FString::Printf(TEXT("preview_%s.%s"), *Hash.Left(8), *Format);
	const FString FilePath = FPaths::Combine(TempDir, FileName);

	if (!FFileHelper::SaveArrayToFile(ImageData, *FilePath))
	{
		return FBridgeToolResult::Error(TEXT("Failed to write image file"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("file_path"), FilePath);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	Result->SetNumberField(TEXT("width"), Resolution);
	Result->SetNumberField(TEXT("height"), Resolution);
	Result->SetStringField(TEXT("format"), Format);
	return FBridgeToolResult::Json(Result);
}

TArray<uint8> UGetAssetPreviewTool::ExportTexture(UTexture2D* Texture, int32 Resolution, const FString& Format)
{
	if (!Texture)
	{
		return TArray<uint8>();
	}

	// Get texture source data (editor only)
	FTextureSource& Source = Texture->Source;
	if (!Source.IsValid())
	{
		return RenderThumbnail(Texture, Resolution, Format);
	}

	TArray64<uint8> RawData;
	if (!Source.GetMipData(RawData, 0))
	{
		return RenderThumbnail(Texture, Resolution, Format);
	}

	const int32 Width = Source.GetSizeX();
	const int32 Height = Source.GetSizeY();
	if (Width == 0 || Height == 0)
	{
		return TArray<uint8>();
	}

	const ETextureSourceFormat SourceFormat = Source.GetFormat();
	const bool bIsGrayscale = (SourceFormat == TSF_G8 || SourceFormat == TSF_G16);

	return CompressImage(RawData.GetData(), RawData.Num(), Width, Height, bIsGrayscale, Format);
}

TArray<uint8> UGetAssetPreviewTool::RenderThumbnail(UObject* Asset, int32 Resolution, const FString& Format)
{
	if (!Asset)
	{
		return TArray<uint8>();
	}

	FObjectThumbnail Thumbnail;
	ThumbnailTools::RenderThumbnail(
		Asset,
		Resolution,
		Resolution,
		ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
		nullptr,
		&Thumbnail
	);

	if (Thumbnail.IsEmpty())
	{
		return TArray<uint8>();
	}

	const TArray<uint8>& ThumbData = Thumbnail.GetUncompressedImageData();
	const int32 ThumbWidth = Thumbnail.GetImageWidth();
	const int32 ThumbHeight = Thumbnail.GetImageHeight();

	if (ThumbWidth == 0 || ThumbHeight == 0 || ThumbData.Num() == 0)
	{
		return TArray<uint8>();
	}

	return CompressImage(ThumbData.GetData(), ThumbData.Num(), ThumbWidth, ThumbHeight, false, Format);
}

TArray<uint8> UGetAssetPreviewTool::CompressImage(const uint8* RawData, int32 DataSize, int32 Width, int32 Height, bool bIsGrayscale, const FString& Format)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const EImageFormat ImageFormat = Format == TEXT("jpeg") ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid())
	{
		return TArray<uint8>();
	}

	const ERGBFormat RGBFormat = bIsGrayscale ? ERGBFormat::Gray : ERGBFormat::BGRA;
	if (!ImageWrapper->SetRaw(RawData, DataSize, Width, Height, RGBFormat, 8))
	{
		return TArray<uint8>();
	}

	const int32 Quality = Format == TEXT("jpeg") ? 85 : 100;
	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(Quality);

	// Convert TArray64 to TArray for return
	TArray<uint8> Result;
	Result.SetNumUninitialized(CompressedData.Num());
	FMemory::Memcpy(Result.GetData(), CompressedData.GetData(), CompressedData.Num());
	return Result;
}

void UGetAssetPreviewTool::CleanupPreviousPreviewFiles(const FString& TempDir)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(TempDir, TEXT("preview_*.*")), true, false);

	for (const FString& File : Files)
	{
		const FString FilePath = FPaths::Combine(TempDir, File);
		IFileManager::Get().Delete(*FilePath, false, true);
	}
}
