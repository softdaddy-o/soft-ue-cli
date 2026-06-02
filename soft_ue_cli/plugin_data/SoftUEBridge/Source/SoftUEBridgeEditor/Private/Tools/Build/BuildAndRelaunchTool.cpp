// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Build/BuildAndRelaunchTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"

namespace
{
	FString EscapePowerShellSingleQuotedString(const FString& Value)
	{
		return Value.Replace(TEXT("'"), TEXT("''"));
	}

	FString QuoteWindowsCommandLineArg(const FString& Value)
	{
		const FString Escaped = Value.Replace(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	bool IsUsableEngineDir(const FString& EngineDir)
	{
		return !EngineDir.IsEmpty()
			&& FPaths::DirectoryExists(EngineDir)
			&& FPaths::FileExists(FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat")))
			&& FPaths::FileExists(FPaths::Combine(EngineDir, TEXT("Binaries/Win64/UnrealEditor.exe")));
	}

	FString NormalizeEngineDir(FString Candidate)
	{
		if (Candidate.IsEmpty())
		{
			return Candidate;
		}

		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (!FPaths::GetCleanFilename(Candidate).Equals(TEXT("Engine"), ESearchCase::IgnoreCase))
		{
			const FString NestedEngine = FPaths::Combine(Candidate, TEXT("Engine"));
			if (FPaths::DirectoryExists(NestedEngine))
			{
				Candidate = NestedEngine;
				FPaths::NormalizeDirectoryName(Candidate);
			}
		}
		return Candidate;
	}

	FString ReadProjectEngineAssociation(const FString& ProjectPath)
	{
		FString ProjectJsonText;
		if (!FFileHelper::LoadFileToString(ProjectJsonText, *ProjectPath))
		{
			return TEXT("");
		}

		TSharedPtr<FJsonObject> ProjectJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ProjectJsonText);
		if (!FJsonSerializer::Deserialize(Reader, ProjectJson) || !ProjectJson.IsValid())
		{
			return TEXT("");
		}

		FString EngineAssociation;
		ProjectJson->TryGetStringField(TEXT("EngineAssociation"), EngineAssociation);
		EngineAssociation.TrimStartAndEndInline();
		return EngineAssociation;
	}

	FString ResolveEngineDirForBuild(
		const FString& ProjectPath,
		const FString& CurrentEngineDir,
		FString& OutEngineAssociation,
		FString& OutEngineSource)
	{
		OutEngineAssociation = ReadProjectEngineAssociation(ProjectPath);

		if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
		{
			FString EngineIdentifier;
			if (DesktopPlatform->GetEngineIdentifierForProject(ProjectPath, EngineIdentifier))
			{
				FString EngineRootDir;
				if (DesktopPlatform->GetEngineRootDirFromIdentifier(EngineIdentifier, EngineRootDir))
				{
					const FString Candidate = NormalizeEngineDir(EngineRootDir);
					if (IsUsableEngineDir(Candidate))
					{
						OutEngineSource = TEXT("project_engine_association_desktop_platform");
						if (OutEngineAssociation.IsEmpty())
						{
							OutEngineAssociation = EngineIdentifier;
						}
						return Candidate;
					}
				}
			}
		}

		if (!OutEngineAssociation.IsEmpty())
		{
			TArray<FString> CandidateRoots;
			if (FPaths::IsRelative(OutEngineAssociation))
			{
				TArray<FString> EpicRoots;
				for (const TCHAR* EnvName : {TEXT("ProgramFiles"), TEXT("ProgramW6432"), TEXT("ProgramFiles(x86)")})
				{
					FString EnvValue = FPlatformMisc::GetEnvironmentVariable(EnvName);
					if (!EnvValue.IsEmpty())
					{
						EpicRoots.Add(FPaths::Combine(EnvValue, TEXT("Epic Games")));
					}
				}
				EpicRoots.Add(TEXT("C:/Program Files/Epic Games"));
				EpicRoots.Add(TEXT("D:/Program Files/Epic Games"));

				for (const FString& EpicRoot : EpicRoots)
				{
					CandidateRoots.Add(FPaths::Combine(EpicRoot, FString::Printf(TEXT("UE_%s"), *OutEngineAssociation)));
					CandidateRoots.Add(FPaths::Combine(EpicRoot, OutEngineAssociation));
				}
			}
			else
			{
				CandidateRoots.Add(OutEngineAssociation);
			}

			for (const FString& CandidateRoot : CandidateRoots)
			{
				const FString Candidate = NormalizeEngineDir(CandidateRoot);
				if (IsUsableEngineDir(Candidate))
				{
					OutEngineSource = TEXT("project_engine_association_search");
					return Candidate;
				}
			}
		}

		OutEngineSource = OutEngineAssociation.IsEmpty()
			? TEXT("current_editor_engine_no_association")
			: TEXT("current_editor_engine_association_unresolved");
		return NormalizeEngineDir(CurrentEngineDir);
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

	FBridgeSchemaProperty StartupMarkerTimeout;
	StartupMarkerTimeout.Type = TEXT("number");
	StartupMarkerTimeout.Description = TEXT("Seconds to wait for the detached worker startup marker (default: 30, minimum: 5)");
	StartupMarkerTimeout.bRequired = false;
	Schema.Add(TEXT("startup_marker_timeout"), StartupMarkerTimeout);

	FBridgeSchemaProperty Compiler;
	Compiler.Type = TEXT("string");
	Compiler.Description = TEXT("Optional Unreal Build Tool compiler override, forwarded as -Compiler=<value>");
	Compiler.bRequired = false;
	Schema.Add(TEXT("compiler"), Compiler);

	FBridgeSchemaProperty CompilerVersion;
	CompilerVersion.Type = TEXT("string");
	CompilerVersion.Description = TEXT("Optional Unreal Build Tool compiler version override, forwarded as -CompilerVersion=<value>");
	CompilerVersion.bRequired = false;
	Schema.Add(TEXT("compiler_version"), CompilerVersion);

	FBridgeSchemaProperty Toolchain;
	Toolchain.Type = TEXT("string");
	Toolchain.Description = TEXT("Alias for compiler_version when pinning an installed toolchain");
	Toolchain.bRequired = false;
	Schema.Add(TEXT("toolchain"), Toolchain);

	return Schema;
}

FBridgeToolResult UBuildAndRelaunchTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& /*Context*/)
{
#if PLATFORM_WINDOWS
	FString BuildConfig = GetStringArgOrDefault(Arguments, TEXT("build_config"), TEXT("Development"));
	bool bSkipRelaunch = GetBoolArgOrDefault(Arguments, TEXT("skip_relaunch"), false);
	const FString Compiler = GetStringArgOrDefault(Arguments, TEXT("compiler"), TEXT(""));
	const FString CompilerVersion = GetStringArgOrDefault(
		Arguments,
		TEXT("compiler_version"),
		GetStringArgOrDefault(Arguments, TEXT("toolchain"), TEXT("")));
	const FString Toolchain = GetStringArgOrDefault(Arguments, TEXT("toolchain"), TEXT(""));
	const float StartupMarkerTimeoutSeconds = FMath::Clamp(
		GetFloatArgOrDefault(Arguments, TEXT("startup_marker_timeout"), 30.0f),
		5.0f,
		120.0f);

	// Validate build configuration
	if (BuildConfig != TEXT("Development") && BuildConfig != TEXT("Debug") && BuildConfig != TEXT("Shipping"))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid build configuration: %s. Must be Development, Debug, or Shipping."), *BuildConfig));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("build-and-relaunch: Starting build and relaunch workflow (Config: %s, SkipRelaunch: %s)"),
		*BuildConfig, bSkipRelaunch ? TEXT("true") : TEXT("false"));

	// Get project paths
	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Could not determine project path"));
	}

	FString ProjectName = FPaths::GetBaseFilename(ProjectPath);

	// Get engine paths. Prefer the project's EngineAssociation so a live editor
	// cannot accidentally build through a different installed engine.
	FString EngineAssociation;
	FString EngineSource;
	FString EngineDir = ResolveEngineDirForBuild(
		ProjectPath,
		FPaths::ConvertRelativePathToFull(FPaths::EngineDir()),
		EngineAssociation,
		EngineSource);
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
	FString StartupMarkerPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.started"));

	// Remove stale artifacts so CLI doesn't read an old result.
	PlatformFile.DeleteFile(*BuildStatusPath);
	PlatformFile.DeleteFile(*BuildLogPath);
	PlatformFile.DeleteFile(*StartupMarkerPath);

	const FString EscapedTempScriptPath = EscapePowerShellSingleQuotedString(TempScriptPath);
	const FString EscapedBuildLogPath = EscapePowerShellSingleQuotedString(BuildLogPath);
	const FString EscapedBuildStatusPath = EscapePowerShellSingleQuotedString(BuildStatusPath);
	const FString EscapedStartupMarkerPath = EscapePowerShellSingleQuotedString(StartupMarkerPath);
	const FString EscapedBuildBatchFile = EscapePowerShellSingleQuotedString(BuildBatchFile);
	const FString EscapedEditorExecutable = EscapePowerShellSingleQuotedString(EditorExecutable);
	const FString EscapedProjectPath = EscapePowerShellSingleQuotedString(ProjectPath);
	const FString EscapedProjectName = EscapePowerShellSingleQuotedString(ProjectName);
	const FString EscapedBuildConfig = EscapePowerShellSingleQuotedString(BuildConfig);
	const FString EscapedCompiler = EscapePowerShellSingleQuotedString(Compiler);
	const FString EscapedCompilerVersion = EscapePowerShellSingleQuotedString(CompilerVersion);
	const FString EscapedToolchain = EscapePowerShellSingleQuotedString(Toolchain);

	FString WorkerScript = TEXT("$ErrorActionPreference = 'Stop'\n");
	WorkerScript += FString::Printf(TEXT("$WorkerScriptPath = '%s'\n"), *EscapedTempScriptPath);
	WorkerScript += FString::Printf(TEXT("$BuildLogPath = '%s'\n"), *EscapedBuildLogPath);
	WorkerScript += FString::Printf(TEXT("$BuildStatusPath = '%s'\n"), *EscapedBuildStatusPath);
	WorkerScript += FString::Printf(TEXT("$StartupMarkerPath = '%s'\n"), *EscapedStartupMarkerPath);
	WorkerScript += FString::Printf(TEXT("$BuildBatchFile = '%s'\n"), *EscapedBuildBatchFile);
	WorkerScript += FString::Printf(TEXT("$EditorExecutable = '%s'\n"), *EscapedEditorExecutable);
	WorkerScript += FString::Printf(TEXT("$ProjectPath = '%s'\n"), *EscapedProjectPath);
	WorkerScript += FString::Printf(TEXT("$ProjectName = '%s'\n"), *EscapedProjectName);
	WorkerScript += FString::Printf(TEXT("$BuildConfig = '%s'\n"), *EscapedBuildConfig);
	WorkerScript += FString::Printf(TEXT("$Compiler = '%s'\n"), *EscapedCompiler);
	WorkerScript += FString::Printf(TEXT("$CompilerVersion = '%s'\n"), *EscapedCompilerVersion);
	WorkerScript += FString::Printf(TEXT("$Toolchain = '%s'\n"), *EscapedToolchain);
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
	WorkerScript += TEXT("    \"build-and-relaunch worker started $(Get-Date -Format o)\" | Set-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    \"started $(Get-Date -Format o)\" | Set-Content -Path $StartupMarkerPath -Encoding utf8\n");
	WorkerScript += TEXT("    Write-BridgeStatus -Stage 'waiting_for_editor_shutdown' -Message \"Waiting for editor process $EditorPid to exit.\"\n");
	WorkerScript += TEXT("    $EditorProcess = Get-Process -Id $EditorPid -ErrorAction SilentlyContinue\n");
	WorkerScript += TEXT("    if ($EditorProcess) {\n");
	WorkerScript += TEXT("        Wait-Process -Id $EditorPid\n");
	WorkerScript += TEXT("    }\n");
	WorkerScript += TEXT("    \"Editor closed.\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    \"Building $ProjectName ($BuildConfig)...\" | Add-Content -Path $BuildLogPath -Encoding utf8\n");
	WorkerScript += TEXT("    Write-BridgeStatus -Stage 'building' -Message \"Running Build.bat for $($ProjectName)Editor ($BuildConfig).\"\n");
	WorkerScript += TEXT("    $BuildArgs = @(\"$($ProjectName)Editor\", 'Win64', $BuildConfig, $ProjectPath, '-waitmutex')\n");
	WorkerScript += TEXT("    if ($Compiler) { $BuildArgs += \"-Compiler=$Compiler\" }\n");
	WorkerScript += TEXT("    if ($CompilerVersion) { $BuildArgs += \"-CompilerVersion=$CompilerVersion\" }\n");
	WorkerScript += TEXT("    elseif ($Toolchain) { $BuildArgs += \"-CompilerVersion=$Toolchain\" }\n");
	WorkerScript += TEXT("    & $BuildBatchFile @BuildArgs 2>&1 | Out-File -FilePath $BuildLogPath -Append -Encoding utf8\n");
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
		true,  // bLaunchReallyHidden
		nullptr,
		0,     // PriorityModifier
		nullptr,
		nullptr
	);

	if (!ProcHandle.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("worker_failed_to_start: failed to launch build worker process"));
	}

	FPlatformProcess::CloseProc(ProcHandle);

	const double StartupWaitStart = FPlatformTime::Seconds();
	while (FPlatformTime::Seconds() - StartupWaitStart < StartupMarkerTimeoutSeconds)
	{
		if (PlatformFile.FileExists(*StartupMarkerPath))
		{
			break;
		}

		if (PlatformFile.FileExists(*BuildStatusPath))
		{
			FString StatusText;
			if (FFileHelper::LoadFileToString(StatusText, *BuildStatusPath) && StatusText.Contains(TEXT("error")))
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("worker_failed_to_start: build worker wrote an error before startup marker (script: %s): %s"),
					*TempScriptPath,
					*StatusText));
			}
		}

		FPlatformProcess::Sleep(0.1f);
	}

	if (!PlatformFile.FileExists(*StartupMarkerPath))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("worker_failed_to_start: build worker did not create a startup marker within %.0fs (script: %s)"),
			StartupMarkerTimeoutSeconds,
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
	Result->SetStringField(TEXT("engine_dir"), EngineDir);
	Result->SetStringField(TEXT("engine_source"), EngineSource);
	if (!EngineAssociation.IsEmpty())
	{
		Result->SetStringField(TEXT("engine_association"), EngineAssociation);
	}
	if (!Compiler.IsEmpty())
	{
		Result->SetStringField(TEXT("compiler"), Compiler);
	}
	if (!CompilerVersion.IsEmpty())
	{
		Result->SetStringField(TEXT("compiler_version"), CompilerVersion);
	}
	if (!Toolchain.IsEmpty())
	{
		Result->SetStringField(TEXT("toolchain"), Toolchain);
	}
	Result->SetBoolField(TEXT("will_relaunch"), !bSkipRelaunch);
	Result->SetNumberField(TEXT("editor_pid"), CurrentPID);
	Result->SetStringField(TEXT("build_log_path"), BuildLogPath);
	Result->SetStringField(TEXT("build_status_path"), BuildStatusPath);
	Result->SetStringField(TEXT("startup_marker_path"), StartupMarkerPath);
	Result->SetStringField(TEXT("worker_started_path"), StartupMarkerPath);
	Result->SetStringField(TEXT("progress_status_path"), BuildStatusPath);
	Result->SetNumberField(TEXT("startup_marker_timeout"), StartupMarkerTimeoutSeconds);
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
