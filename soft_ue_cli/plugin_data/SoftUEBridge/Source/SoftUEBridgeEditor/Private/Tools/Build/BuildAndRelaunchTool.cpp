// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Build/BuildAndRelaunchTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"

namespace
{
	FString EscapePowerShellSingleQuotedString(const FString& Value)
	{
		return Value.Replace(TEXT("'"), TEXT("''"));
	}

	FString QuoteWindowsCommandLineArg(const FString& Value)
	{
		FString Escaped = Value.Replace(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

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
	const FBridgeToolContext& /*Context*/)
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

	// Create a PowerShell worker script to handle the workflow.
	// It is launched via a detached grandchild process so it survives editor shutdown.
	FString TempScriptPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.ps1"));
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
	FString WorkerStartedPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.started"));

	// Remove stale artifacts so CLI doesn't read an old result.
	PlatformFile.DeleteFile(*BuildStatusPath);
	PlatformFile.DeleteFile(*BuildLogPath);
	PlatformFile.DeleteFile(*WorkerStartedPath);

	const FString EscapedTempScriptPath = EscapePowerShellSingleQuotedString(TempScriptPath);
	const FString EscapedBuildLogPath = EscapePowerShellSingleQuotedString(BuildLogPath);
	const FString EscapedBuildStatusPath = EscapePowerShellSingleQuotedString(BuildStatusPath);
	const FString EscapedWorkerStartedPath = EscapePowerShellSingleQuotedString(WorkerStartedPath);
	const FString EscapedBuildBatchFile = EscapePowerShellSingleQuotedString(BuildBatchFile);
	const FString EscapedEditorExecutable = EscapePowerShellSingleQuotedString(EditorExecutable);
	const FString EscapedProjectPath = EscapePowerShellSingleQuotedString(ProjectPath);
	const FString EscapedProjectName = EscapePowerShellSingleQuotedString(ProjectName);
	const FString EscapedBuildConfig = EscapePowerShellSingleQuotedString(BuildConfig);

	FString WorkerScript = TEXT("$ErrorActionPreference = 'Stop'\n");
	WorkerScript += FString::Printf(TEXT("$WorkerScriptPath = '%s'\n"), *EscapedTempScriptPath);
	WorkerScript += FString::Printf(TEXT("$BuildLogPath = '%s'\n"), *EscapedBuildLogPath);
	WorkerScript += FString::Printf(TEXT("$BuildStatusPath = '%s'\n"), *EscapedBuildStatusPath);
	WorkerScript += FString::Printf(TEXT("$WorkerStartedPath = '%s'\n"), *EscapedWorkerStartedPath);
	WorkerScript += FString::Printf(TEXT("$BuildBatchFile = '%s'\n"), *EscapedBuildBatchFile);
	WorkerScript += FString::Printf(TEXT("$EditorExecutable = '%s'\n"), *EscapedEditorExecutable);
	WorkerScript += FString::Printf(TEXT("$ProjectPath = '%s'\n"), *EscapedProjectPath);
	WorkerScript += FString::Printf(TEXT("$ProjectName = '%s'\n"), *EscapedProjectName);
	WorkerScript += FString::Printf(TEXT("$BuildConfig = '%s'\n"), *EscapedBuildConfig);
	WorkerScript += FString::Printf(TEXT("$EditorPid = %u\n"), CurrentPID);
	WorkerScript += FString::Printf(TEXT("$SkipRelaunch = $%s\n"), bSkipRelaunch ? TEXT("true") : TEXT("false"));
	WorkerScript += TEXT("\n");
	WorkerScript += TEXT("$StartedAt = Get-Date -Format o\n");
	WorkerScript += TEXT("\n");
	WorkerScript += TEXT("function Write-BridgeStatus {\n");
	WorkerScript += TEXT("    param(\n");
	WorkerScript += TEXT("        [string]$Stage,\n");
	WorkerScript += TEXT("        [bool]$Complete = $false,\n");
	WorkerScript += TEXT("        [bool]$Success = $false,\n");
	WorkerScript += TEXT("        $ExitCode = $null,\n");
	WorkerScript += TEXT("        [string]$Message = '',\n");
	WorkerScript += TEXT("        [string]$ErrorText = ''\n");
	WorkerScript += TEXT("    )\n");
	WorkerScript += TEXT("    $Payload = [ordered]@{\n");
	WorkerScript += TEXT("        success = $Success\n");
	WorkerScript += TEXT("        complete = $Complete\n");
	WorkerScript += TEXT("        stage = $Stage\n");
	WorkerScript += TEXT("        last_stage = $Stage\n");
	WorkerScript += TEXT("        exit_code = $ExitCode\n");
	WorkerScript += TEXT("        message = $Message\n");
	WorkerScript += TEXT("        started_at = $StartedAt\n");
	WorkerScript += TEXT("        updated_at = (Get-Date -Format o)\n");
	WorkerScript += TEXT("        build_log_path = $BuildLogPath\n");
	WorkerScript += TEXT("        worker_pid = $PID\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("    if ($ErrorText) { $Payload.error = $ErrorText }\n");
	WorkerScript += TEXT("    $Payload | ConvertTo-Json -Compress | Set-Content -LiteralPath $BuildStatusPath -Encoding utf8\n");
	WorkerScript += TEXT("}\n");
	WorkerScript += TEXT("\n");
	WorkerScript += TEXT("try {\n");
	WorkerScript += TEXT("    \"started\" | Set-Content -Path $WorkerStartedPath -Encoding utf8\n");
	WorkerScript += TEXT("    \"build-and-relaunch worker started $(Get-Date -Format o)\" | Set-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    Write-BridgeStatus -Stage 'waiting_for_editor_shutdown' -Message \"Waiting for editor process $EditorPid to exit.\"\n");
	WorkerScript += TEXT("    $EditorProcess = Get-Process -Id $EditorPid -ErrorAction SilentlyContinue\n");
	WorkerScript += TEXT("    if ($EditorProcess) {\n");
	WorkerScript += TEXT("        Wait-Process -Id $EditorPid\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("    \"Editor closed.\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    \"Building $ProjectName ($BuildConfig)...\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    Write-BridgeStatus -Stage 'building' -Message \"Running Build.bat for $($ProjectName)Editor ($BuildConfig).\"\n");
	WorkerScript += TEXT("    & $BuildBatchFile \"$($ProjectName)Editor\" 'Win64' $BuildConfig $ProjectPath '-waitmutex' 2>&1 | Out-File -FilePath $BuildLogPath -Append -Encoding utf8\n");
	WorkerScript += TEXT("    $BuildExit = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } else { 0 }\n");
	WorkerScript += TEXT("    if ($BuildExit -ne 0) {\n");
	WorkerScript += TEXT("        Write-BridgeStatus -Stage 'build_failed' -Complete $true -Success $false -ExitCode $BuildExit -Message 'Build failed. See build log.'\n");
	WorkerScript += TEXT("        exit $BuildExit\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("    if (-not $SkipRelaunch) {\n");
	WorkerScript += TEXT("        \"Build completed successfully. Relaunching editor...\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("        Write-BridgeStatus -Stage 'relaunching_editor' -Complete $false -Success $true -ExitCode $BuildExit -Message 'Build completed; relaunching editor.'\n");
	WorkerScript += TEXT("        Start-Sleep -Seconds 2\n");
	WorkerScript += TEXT("        Start-Process -FilePath $EditorExecutable -ArgumentList @($ProjectPath) | Out-Null\n");
	WorkerScript += TEXT("        Write-BridgeStatus -Stage 'completed' -Complete $true -Success $true -ExitCode $BuildExit -Message 'Build completed and editor relaunch was requested.'\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("    else {\n");
	WorkerScript += TEXT("        \"Build completed successfully.\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("        Write-BridgeStatus -Stage 'completed' -Complete $true -Success $true -ExitCode $BuildExit -Message 'Build completed successfully; editor relaunch was skipped.'\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("}\n");
	WorkerScript += TEXT("catch {\n");
	WorkerScript += TEXT("    $WorkerError = $_.Exception.Message\n");
	WorkerScript += TEXT("    Write-BridgeStatus -Stage 'worker_error' -Complete $true -Success $false -ExitCode 1 -Message 'Build worker failed before completion.' -ErrorText $WorkerError\n");
	WorkerScript += TEXT("    \"Worker error: $WorkerError\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    exit 1\n");
	WorkerScript += TEXT("}\n");
	WorkerScript += TEXT("finally {\n");
	WorkerScript += TEXT("    Remove-Item -LiteralPath $WorkerScriptPath -ErrorAction SilentlyContinue\n");
	WorkerScript += TEXT("}\n");

	if (!FFileHelper::SaveStringToFile(WorkerScript, *TempScriptPath))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create worker script: %s"), *TempScriptPath));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Created worker script at: %s"), *TempScriptPath);

	const FString CmdArgs = FString::Printf(
		TEXT("-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File %s"),
		*QuoteWindowsCommandLineArg(TempScriptPath));
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		TEXT("powershell.exe"),
		*CmdArgs,
		true,  // bLaunchDetached
		true,  // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr,
		0,     // PriorityModifier
		nullptr,
		nullptr
	);

	if (!ProcHandle.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("worker_failed_to_start: failed to launch build worker process"));
	}

	bool bWorkerStarted = false;
	const double StartupDeadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < StartupDeadline)
	{
		if (PlatformFile.FileExists(*WorkerStartedPath) ||
			PlatformFile.FileExists(*BuildLogPath) ||
			PlatformFile.FileExists(*BuildStatusPath))
		{
			bWorkerStarted = true;
			break;
		}
		FPlatformProcess::Sleep(0.1f);
	}
	FPlatformProcess::CloseProc(ProcHandle);

	if (!bWorkerStarted)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("worker_failed_to_start: build worker did not create a startup marker within 5s (script: %s)"),
			*TempScriptPath));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Detached build worker launched successfully (PID: %d). Requesting editor shutdown..."), CurrentPID);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("initiated"));
	Result->SetStringField(TEXT("stage"), TEXT("initiated"));
	Result->SetStringField(TEXT("last_stage"), TEXT("initiated"));
	Result->SetBoolField(TEXT("complete"), false);
	Result->SetStringField(TEXT("project"), ProjectName);
	Result->SetStringField(TEXT("build_config"), BuildConfig);
	Result->SetBoolField(TEXT("will_relaunch"), !bSkipRelaunch);
	Result->SetNumberField(TEXT("editor_pid"), CurrentPID);
	Result->SetStringField(TEXT("build_log_path"), BuildLogPath);
	Result->SetStringField(TEXT("build_status_path"), BuildStatusPath);
	Result->SetStringField(TEXT("worker_started_path"), WorkerStartedPath);
	Result->SetStringField(TEXT("progress_status_path"), BuildStatusPath);
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
