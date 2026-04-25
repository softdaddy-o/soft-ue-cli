// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "OpenAssetTool.generated.h"

/**
 * Bridge tool to open assets or editor windows in the Unreal Editor.
 *
 * Supports two modes:
 * 1. Asset Mode: Opens an asset in its default editor (Blueprint Editor, Material Editor, etc.)
 * 2. Window Mode: Opens an editor window/tab by name (OutputLog, ContentBrowser, etc.)
 *
 * Optionally brings the window to focus.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UOpenAssetTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("open-asset"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Open an asset or editor window in the Unreal Editor. "
				   "Use 'asset_path' to open an asset in its default editor, "
				   "or 'window_name' to open a specific editor window/tab by name. "
				   "Optionally brings the window to focus.");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/**
	 * Execute asset opening mode.
	 * @param AssetPath - Path to the asset to open
	 * @param bBringToFront - Whether to bring the window to focus
	 * @return Tool result with success/error info
	 */
	FBridgeToolResult ExecuteAssetMode(const FString& AssetPath, bool bBringToFront);

	/**
	 * Execute window opening mode.
	 * @param WindowName - Name/ID of the editor window/tab to open
	 * @param bBringToFront - Whether to bring the window to focus
	 * @return Tool result with success/error info
	 */
	FBridgeToolResult ExecuteWindowMode(const FString& WindowName, bool bBringToFront);

	/**
	 * Get a human-readable editor type name from an asset.
	 * @param Asset - Asset to inspect
	 * @return Editor type string (e.g., "Blueprint Editor", "Material Editor")
	 */
	FString GetEditorTypeName(UObject* Asset) const;
};
