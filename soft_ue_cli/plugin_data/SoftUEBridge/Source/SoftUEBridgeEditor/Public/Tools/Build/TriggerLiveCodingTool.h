// Copyright soft-ue-expert. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "TriggerLiveCodingTool.generated.h"

#if PLATFORM_WINDOWS
class ILiveCodingModule;
#endif

/**
 * Trigger Live Coding compilation for C++ code changes.
 * Equivalent to pressing Ctrl+Alt+F11 in the editor.
 * Supports async (fire-and-forget) and sync (wait-for-result) modes.
 * Windows only. Requires Live Coding enabled in Editor Preferences.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UTriggerLiveCodingTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override
	{
		return TEXT("trigger-live-coding");
	}

	virtual FString GetToolDescription() const override;
	
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	
	virtual TArray<FString> GetRequiredParams() const override
	{
		return {};
	}

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
#if PLATFORM_WINDOWS
	FBridgeToolResult ExecuteSynchronous(ILiveCodingModule* LiveCodingModule);
	FBridgeToolResult ExecuteAsynchronous(ILiveCodingModule* LiveCodingModule);
#endif
};
