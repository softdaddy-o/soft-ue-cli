// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "RunPythonScriptTool.generated.h"

/**
 * Execute Python scripts in Unreal Editor's Python environment.
 * Requires PythonScriptPlugin to be enabled.
 * Supports inline scripts or script files with optional arguments.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API URunPythonScriptTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("run-python-script"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; } // Either script or script_path required
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// Execute a Python command and capture output
	FString ExecutePython(const FString& Command, bool& bOutSuccess, FString& OutError);

	// Read script file from disk
	bool ReadScriptFile(const FString& ScriptPath, FString& OutScript, FString& OutError);

	// Build Python command with arguments and additional Python paths
	FString BuildPythonCommand(const FString& Script, const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& PythonPaths);
};
