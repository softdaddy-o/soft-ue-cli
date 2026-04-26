// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PythonScriptTypes.h"
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
	// Execute a Python command or file and capture log output and error text.
	bool ExecutePythonCommand(
		const FString& Command,
		EPythonCommandExecutionMode ExecutionMode,
		EPythonFileExecutionScope FileExecutionScope,
		bool& bOutSuccess,
		FString& OutOutput,
		FString& OutError);

	// Resolve and read a script file from disk for validation before execution.
	bool ReadScriptFile(const FString& ScriptPath, FString& OutResolvedPath, FString& OutScript, FString& OutError);

	// Build a Python preamble with arguments, world helpers, and additional Python paths.
	FString BuildPythonPreamble(
		const TSharedPtr<FJsonObject>& Arguments,
		const TArray<FString>& PythonPaths,
		const FString& WorldType);

	bool ContainsUnsafeLevelLoad(const FString& Script) const;
};
