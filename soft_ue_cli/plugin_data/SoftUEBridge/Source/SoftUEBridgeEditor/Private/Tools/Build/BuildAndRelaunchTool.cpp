// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Build/BuildAndRelaunchTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Editor.h"

FString UBuildAndRelaunchTool::GetToolDescription() const
{
	return TEXT("Close THIS editor instance (identified by PID), trigger a full project build, and relaunch the editor. Only affects the MCP-connected editor instance, not other running editors.");
}

TMap<FString, FBridgeSchemaProperty> UBuildAndRelaunchTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty BuildConfig;
	BuildConfig.Type = TEXT("string");
	BuildConfig.Description = TEXT("Build configuration: Development, Debug, or Shipping (default: Development)");
	BuildConfig.bRequired = false;
	Schema.Add(TEXT("build_config"), BuildConfig);

	FBridgeSchemaProperty SkipRelaunch;
	SkipRelaunch.Type = TEXT("boolean");
	SkipRelaunch.Description = TEXT("Skip relaunching the editor after build (default: false)");
	SkipRelaunch.bRequired = false;
	Schema.Add(TEXT("skip_relaunch"), SkipRelaunch);

	return Schema;
}

FBridgeToolResult UBuildAndRelaunchTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
#if PLATFORM_WINDOWS
	FString BuildConfig = GetStringArgOrDefault(Arguments, TEXT("build_config"), TEXT("Development"));
	bool bSkipRelaunch = GetBoolArgOrDefault(Arguments, TEXT("skip_relaunch"), false);

	// Validate build configuration
	if (BuildConfig != TEXT("Development") && BuildConfig != TEXT("Debug") && BuildConfig != TEXT("Shipping"))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid build configuration: %s. Must be Development, Debug, or Shipping."), *BuildConfig));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Starting build and relaunch workflow (Config: %s, SkipRelaunch: %s)"),
		*BuildConfig, bSkipRelaunch ? TEXT("true") : TEXT("false"));

	// Get project paths
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Could not determine project path"));
	}

	FString ProjectName = FPaths::GetBaseFilename(ProjectPath);
	FString ProjectDir = FPaths::GetPath(ProjectPath);

	// Get engine paths
	FString EngineDir = FPaths::EngineDir();
	FString BuildBatchFile = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
	FString EditorExecutable = FPaths::Combine(EngineDir, TEXT("Binaries/Win64/UnrealEditor.exe"));

	if (!FPaths::FileExists(BuildBatchFile))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Build script not found: %s"), *BuildBatchFile));
	}

	if (!bSkipRelaunch && !FPaths::FileExists(EditorExecutable))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Editor executable not found: %s"), *EditorExecutable));
	}

	// Create a batch script to handle the workflow
	FString TempScriptPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.bat"));
	FString TempScriptDir = FPaths::GetPath(TempScriptPath);

	// Ensure temp directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TempScriptDir))
	{
		if (!PlatformFile.CreateDirectoryTree(*TempScriptDir))
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create temp directory: %s"), *TempScriptDir));
		}
	}

	// Get current process ID to wait for specifically this instance
	uint32 CurrentPID = FPlatformProcess::GetCurrentProcessId();

	// Paths for build log and status file (used by CLI --wait)
	FString BuildLogPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.log"));
	FString BuildStatusPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.status.json"));

	// Remove stale status file so CLI doesn't read an old result
	PlatformFile.DeleteFile(*BuildStatusPath);

	// Build the batch script
	FString BatchScript = TEXT("@echo off\n");
	BatchScript += FString::Printf(TEXT("echo Waiting for Unreal Editor (PID: %d) to close...\n"), CurrentPID);
	BatchScript += TEXT("\n");

	// Wait for this specific process to exit (not just any editor)
	BatchScript += TEXT(":WAIT_LOOP\n");
	BatchScript += FString::Printf(TEXT("tasklist /FI \"PID eq %d\" 2>NUL | find \"%d\" >NUL\n"), CurrentPID, CurrentPID);
	BatchScript += TEXT("if %ERRORLEVEL% EQU 0 (\n");
	BatchScript += TEXT("    timeout /t 1 /nobreak >nul\n");
	BatchScript += TEXT("    goto WAIT_LOOP\n");
	BatchScript += TEXT(")\n");
	BatchScript += TEXT("echo Editor closed.\n");
	BatchScript += TEXT("\n");

	// Build command — redirect output to log file while still showing in console
	BatchScript += FString::Printf(TEXT("echo Building %s (%s)...\n"), *ProjectName, *BuildConfig);
	BatchScript += FString::Printf(TEXT("call \"%s\" %sEditor Win64 %s \"%s\" -waitmutex > \"%s\" 2>&1\n"),
		*BuildBatchFile,
		*ProjectName,
		*BuildConfig,
		*ProjectPath,
		*BuildLogPath);
	BatchScript += TEXT("set BUILD_EXIT=%ERRORLEVEL%\n");
	BatchScript += TEXT("\n");
	BatchScript += TEXT("if %BUILD_EXIT% NEQ 0 (\n");
	BatchScript += TEXT("    echo Build failed with error code %BUILD_EXIT%\n");
	BatchScript += FString::Printf(TEXT("    echo {\"success\":false,\"exit_code\":%%BUILD_EXIT%%} > \"%s\"\n"), *BuildStatusPath);
	BatchScript += TEXT("    pause\n");
	BatchScript += TEXT("    exit /b %BUILD_EXIT%\n");
	BatchScript += TEXT(")\n");
	BatchScript += TEXT("\n");

	// Write success status
	BatchScript += FString::Printf(TEXT("echo {\"success\":true,\"exit_code\":0} > \"%s\"\n"), *BuildStatusPath);

	// Relaunch command (if not skipped)
	if (!bSkipRelaunch)
	{
		BatchScript += TEXT("echo Build completed successfully. Relaunching editor...\n");
		BatchScript += TEXT("timeout /t 2 /nobreak >nul\n");
		BatchScript += FString::Printf(TEXT("start \"\" \"%s\" \"%s\"\n"), *EditorExecutable, *ProjectPath);
	}
	else
	{
		BatchScript += TEXT("echo Build completed successfully.\n");
	}

	BatchScript += TEXT("\n");
	BatchScript += FString::Printf(TEXT("del \"%s\"\n"), *TempScriptPath);

	// Write the batch script
	if (!FFileHelper::SaveStringToFile(BatchScript, *TempScriptPath))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create batch script: %s"), *TempScriptPath));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Created batch script at: %s"), *TempScriptPath);

	// Launch the batch script via cmd.exe (batch files can't be executed directly by CreateProc)
	FString CmdArgs = FString::Printf(TEXT("/c \"%s\""), *TempScriptPath);
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"),
		*CmdArgs,
		true,  // bLaunchDetached
		false, // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr,
		0,     // PriorityModifier
		nullptr,
		nullptr
	);

	if (!ProcHandle.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Failed to launch build script"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Batch script launched successfully (PID: %d). Requesting editor shutdown..."), CurrentPID);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("initiated"));
	Result->SetStringField(TEXT("project"), *ProjectName);
	Result->SetStringField(TEXT("build_config"), *BuildConfig);
	Result->SetBoolField(TEXT("will_relaunch"), !bSkipRelaunch);
	Result->SetNumberField(TEXT("editor_pid"), CurrentPID);
	Result->SetStringField(TEXT("build_log_path"), *BuildLogPath);
	Result->SetStringField(TEXT("build_status_path"), *BuildStatusPath);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Build and relaunch workflow initiated for this editor instance (PID: %d). Editor will close momentarily."), CurrentPID));

	// Request editor shutdown
	// Use a small delay to allow the response to be sent
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime) -> bool
	{
		FPlatformMisc::RequestExit(false);
		return false; // Don't repeat
	}), 1.0f);

	return FBridgeToolResult::Json(Result);
#else
	return FBridgeToolResult::Error(TEXT("build-and-relaunch is only supported on Windows"));
#endif
}
