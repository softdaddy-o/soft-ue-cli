// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "GetAssetPreviewTool.generated.h"

/**
 * Tool for generating visual previews of assets (textures, meshes, materials, etc.)
 * Supports both file output (default) and base64-encoded image content
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UGetAssetPreviewTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-asset-preview"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Generate a visual preview of an asset (texture, mesh, material, etc.). "
		           "Returns either a file path to a PNG/JPEG image or base64-encoded image data.");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context
	) override;

private:
	/** Export texture to compressed image data */
	TArray<uint8> ExportTexture(UTexture2D* Texture, int32 Resolution, const FString& Format);

	/** Render asset thumbnail to compressed image data */
	TArray<uint8> RenderThumbnail(UObject* Asset, int32 Resolution, const FString& Format);

	/** Compress raw image data to PNG or JPEG */
	TArray<uint8> CompressImage(const uint8* RawData, int32 DataSize, int32 Width, int32 Height, bool bIsGrayscale, const FString& Format);

	/** Delete previous preview files */
	void CleanupPreviousPreviewFiles(const FString& TempDir);
};
