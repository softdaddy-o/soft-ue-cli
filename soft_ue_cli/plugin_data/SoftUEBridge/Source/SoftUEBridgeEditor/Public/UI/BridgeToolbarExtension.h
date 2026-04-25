// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Manages the SoftUEBridge status indicator in the Unreal Editor status bar.
 * Uses UToolMenus to extend LevelEditor.StatusBar.ToolBar.
 */
class SOFTUEBRIDGEEDITOR_API FBridgeToolbarExtension
{
public:
	/** Initialize and register the status bar widget */
	static void Initialize();

	/** Cleanup and unregister */
	static void Shutdown();

private:
	/** Register the status bar extension via UToolMenus */
	static void RegisterStatusBarExtension();

	/** Build the status widget */
	static TSharedRef<SWidget> CreateStatusWidget();

	/** Get status icon brush */
	static const FSlateBrush* GetStatusBrush();

	/** Get status tooltip text */
	static FText GetStatusTooltip();

	/** Get status color */
	static FSlateColor GetStatusColor();

	/** Handle button click — restarts the bridge server */
	static FReply OnStatusButtonClicked();

	/** Get the engine subsystem */
	static class USoftUEBridgeSubsystem* GetSubsystem();

	/** Whether we've been initialized */
	static bool bIsInitialized;
};
