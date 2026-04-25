// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "TriggerLiveCodingTool.generated.h"

// Forward declarations
#if PLATFORM_WINDOWS
class ILiveCodingModule;
#endif

/**
 * Trigger Live Coding compilation for C++ code changes.
 * Uses UE's Live Coding system (Ctrl+Alt+F11 equivalent).
 * Supports both async and sync modes with compilation result tracking.
 * Windows only. Requires Live Coding to be enabled in Editor Preferences.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UTriggerLiveCodingTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("trigger-live-coding"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
#if PLATFORM_WINDOWS
	// Execute synchronous compilation (blocks until complete)
	FBridgeToolResult ExecuteSynchronous(ILiveCodingModule* LiveCodingModule);

	// Execute asynchronous compilation (fire and forget)
	FBridgeToolResult ExecuteAsynchronous(ILiveCodingModule* LiveCodingModule);
#endif
};
