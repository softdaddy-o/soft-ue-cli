// Copyright soft-ue-expert. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "BuildAndRelaunchTool.generated.h"

/**
 * Close THIS editor instance, trigger a full project build, and relaunch the editor.
 * This tool handles the complete workflow for rebuilding the project.
 * Uses process ID (PID) to ensure only the MCP-connected editor instance is affected.
 * Other running editor instances are not affected.
 * Windows only.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UBuildAndRelaunchTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override
	{
		return TEXT("build-and-relaunch");
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
};
