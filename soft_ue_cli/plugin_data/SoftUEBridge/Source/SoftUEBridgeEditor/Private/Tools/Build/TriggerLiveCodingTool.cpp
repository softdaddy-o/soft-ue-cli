// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Build/TriggerLiveCodingTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Live Coding module (Windows only)
#if PLATFORM_WINDOWS
	#include "ILiveCodingModule.h"
#endif

#if PLATFORM_WINDOWS
static FString NormalizeLiveCodingHeaderPath(FString RelativePath)
{
	RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (RelativePath.ReplaceInline(TEXT("//"), TEXT("/")) > 0) {}
	return RelativePath.TrimStartAndEnd();
}

static bool IsHeaderPathInLiveCodingScope(
	const FString& RelativePath,
	const FString& ModuleScope,
	const FString& PluginScope)
{
	const FString NormalizedPath = NormalizeLiveCodingHeaderPath(RelativePath);
	if (!PluginScope.IsEmpty())
	{
		const FString PluginPrefix = FString(TEXT("Plugins/")) + PluginScope + TEXT("/");
		const FString PluginSegment = FString(TEXT("/")) + PluginPrefix;
		const FString LegacyPluginPrefix = FString(TEXT("Plugin/")) + PluginScope + TEXT("/");
		const FString LegacyPluginSegment = FString(TEXT("/")) + LegacyPluginPrefix;
		if (!NormalizedPath.StartsWith(PluginPrefix, ESearchCase::IgnoreCase)
			&& !NormalizedPath.Contains(PluginSegment, ESearchCase::IgnoreCase)
			&& !NormalizedPath.StartsWith(LegacyPluginPrefix, ESearchCase::IgnoreCase)
			&& !NormalizedPath.Contains(LegacyPluginSegment, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	if (!ModuleScope.IsEmpty())
	{
		const FString ModuleSourcePrefix = FString(TEXT("Source/")) + ModuleScope + TEXT("/");
		const FString ModuleSourceSegment = FString(TEXT("/")) + ModuleSourcePrefix;
		if (!NormalizedPath.StartsWith(ModuleSourcePrefix, ESearchCase::IgnoreCase)
			&& !NormalizedPath.Contains(ModuleSourceSegment, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	return true;
}
#endif

FString UTriggerLiveCodingTool::GetToolDescription() const
{
	return TEXT("Trigger Live Coding compilation for C++ code changes. Supports synchronous mode with wait_for_completion. Windows only.");
}

TMap<FString, FBridgeSchemaProperty> UTriggerLiveCodingTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty WaitForCompletion;
	WaitForCompletion.Type = TEXT("boolean");
	WaitForCompletion.Description = TEXT("Wait for compilation to complete before returning (default: false, async mode)");
	WaitForCompletion.bRequired = false;
	Schema.Add(TEXT("wait_for_completion"), WaitForCompletion);

	FBridgeSchemaProperty AllowHeaderChanges;
	AllowHeaderChanges.Type = TEXT("boolean");
	AllowHeaderChanges.Description = TEXT("If true, bypass reflected-header preflight warnings and trigger Live Coding anyway");
	AllowHeaderChanges.bRequired = false;
	Schema.Add(TEXT("allow_header_changes"), AllowHeaderChanges);

	FBridgeSchemaProperty ModuleScope;
	ModuleScope.Type = TEXT("string");
	ModuleScope.Description = TEXT("Limit reflected-header preflight checks to this module name, e.g. SoftUEBridgeEditor");
	ModuleScope.bRequired = false;
	Schema.Add(TEXT("module"), ModuleScope);

	FBridgeSchemaProperty PluginScope;
	PluginScope.Type = TEXT("string");
	PluginScope.Description = TEXT("Limit reflected-header preflight checks to this plugin directory, e.g. SoftUEBridge");
	PluginScope.bRequired = false;
	Schema.Add(TEXT("plugin"), PluginScope);

	return Schema;
}

FBridgeToolResult UTriggerLiveCodingTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	bool bWaitForCompletion = GetBoolArgOrDefault(Arguments, TEXT("wait_for_completion"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("trigger-live-coding: Triggering Live Coding compilation (wait=%d)"),
		bWaitForCompletion);

#if !PLATFORM_WINDOWS
	return FBridgeToolResult::Error(TEXT("Live Coding is only supported on Windows"));
#else
	const bool bAllowHeaderChanges = GetBoolArgOrDefault(Arguments, TEXT("allow_header_changes"), false);
	const FString ModuleScope = GetStringArgOrDefault(Arguments, TEXT("module")).TrimStartAndEnd();
	const FString PluginScope = GetStringArgOrDefault(Arguments, TEXT("plugin")).TrimStartAndEnd();
	if (!bAllowHeaderChanges)
	{
		TArray<FString> RiskyHeaders;
		if (DetectReflectedHeaderChanges(RiskyHeaders, ModuleScope, PluginScope))
		{
			const FString ScopeMessage = (ModuleScope.IsEmpty() && PluginScope.IsEmpty())
				? TEXT("")
				: FString::Printf(TEXT(" within scope module='%s' plugin='%s'"), *ModuleScope, *PluginScope);
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("trigger-live-coding: reflected header changes detected%s (%s). Live Coding will likely cancel; run build-and-relaunch instead or pass allow_header_changes=true."),
				*ScopeMessage,
				*FString::Join(RiskyHeaders, TEXT(", "))));
		}
	}

	// Try to use ILiveCodingModule for better control
	ILiveCodingModule* LiveCodingModule = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");

	if (!LiveCodingModule)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: LiveCoding module not available, falling back to console command"));

		// Fallback to console command
		if (!GEngine || !GEngine->Exec(nullptr, TEXT("LiveCoding.Compile")))
		{
			return FBridgeToolResult::Error(TEXT("Failed to trigger Live Coding. Enable it in Editor Preferences > General > Live Coding."));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("status"), TEXT("triggered_async"));
		Result->SetStringField(TEXT("message"), TEXT("Live Coding triggered via console command"));
		return FBridgeToolResult::Json(Result);
	}

	// Check if Live Coding is enabled
	if (!LiveCodingModule->IsEnabledForSession())
	{
		return FBridgeToolResult::Error(TEXT("Live Coding is not enabled for this session. Enable it in Editor Preferences > General > Live Coding."));
	}

	if (bWaitForCompletion)
	{
		// Synchronous mode: Wait for compilation to complete
		return ExecuteSynchronous(LiveCodingModule);
	}
	else
	{
		// Asynchronous mode: Just trigger and return immediately
		return ExecuteAsynchronous(LiveCodingModule);
	}
#endif
}

#if PLATFORM_WINDOWS
bool UTriggerLiveCodingTool::DetectReflectedHeaderChanges(
	TArray<FString>& OutFiles,
	const FString& ModuleScope,
	const FString& PluginScope) const
{
	OutFiles.Reset();

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const FString GitArgs = FString::Printf(TEXT("-C \"%s\" status --porcelain --untracked-files=all -- \"*.h\""), *ProjectDir);
	FPlatformProcess::ExecProcess(TEXT("git"), *GitArgs, &ReturnCode, &StdOut, &StdErr);
	if (ReturnCode != 0 || StdOut.IsEmpty())
	{
		return false;
	}

	TArray<FString> Lines;
	StdOut.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		if (Line.Len() < 4)
		{
			continue;
		}

		FString RelativePath = Line.Mid(3).TrimStartAndEnd();
		int32 RenameArrowIndex = INDEX_NONE;
		if (RelativePath.FindLastChar(TEXT('>'), RenameArrowIndex))
		{
			const int32 ArrowStart = RelativePath.Find(TEXT("->"));
			if (ArrowStart != INDEX_NONE)
			{
				RelativePath = RelativePath.Mid(ArrowStart + 2).TrimStartAndEnd();
			}
		}
		RelativePath = NormalizeLiveCodingHeaderPath(RelativePath);
		if (!IsHeaderPathInLiveCodingScope(RelativePath, ModuleScope, PluginScope))
		{
			continue;
		}

		FString AbsolutePath = RelativePath;
		if (FPaths::IsRelative(AbsolutePath))
		{
			AbsolutePath = FPaths::Combine(ProjectDir, RelativePath);
		}

		FString HeaderText;
		if (!FFileHelper::LoadFileToString(HeaderText, *AbsolutePath))
		{
			continue;
		}

		if (HeaderText.Contains(TEXT("UCLASS("))
			|| HeaderText.Contains(TEXT("USTRUCT("))
			|| HeaderText.Contains(TEXT("UINTERFACE("))
			|| HeaderText.Contains(TEXT("UENUM("))
			|| HeaderText.Contains(TEXT("GENERATED_BODY("))
			|| HeaderText.Contains(TEXT("GENERATED_UCLASS_BODY(")))
		{
			OutFiles.Add(RelativePath);
		}
	}

	return OutFiles.Num() > 0;
}

FBridgeToolResult UTriggerLiveCodingTool::ExecuteSynchronous(ILiveCodingModule* LiveCodingModule)
{
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("trigger-live-coding: Starting synchronous compilation..."));

	// Record start time for log filtering
	const double StartTime = FPlatformTime::Seconds();

	// Use WaitForCompletion flag - this blocks until compilation finishes
	ELiveCodingCompileResult CompileResult;
	bool bStarted = LiveCodingModule->Compile(ELiveCodingCompileFlags::WaitForCompletion, &CompileResult);

	const double CompilationTime = FPlatformTime::Seconds() - StartTime;

	// Build result based on CompileResult
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetNumberField(TEXT("compilation_time_seconds"), CompilationTime);
	Result->SetStringField(TEXT("shortcut"), TEXT("Ctrl+Alt+F11"));

	// Map ELiveCodingCompileResult to response
	FString StatusStr;
	FString MessageStr;
	bool bSuccess = false;

	switch (CompileResult)
	{
	case ELiveCodingCompileResult::Success:
		bSuccess = true;
		StatusStr = TEXT("completed");
		MessageStr = FString::Printf(TEXT("Live Coding compilation completed successfully in %.1fs"), CompilationTime);
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("trigger-live-coding: Compilation successful (%.1fs)"), CompilationTime);
		break;

	case ELiveCodingCompileResult::NoChanges:
		bSuccess = true;
		StatusStr = TEXT("no_changes");
		MessageStr = TEXT("No code changes detected - nothing to compile");
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("trigger-live-coding: No changes detected"));
		break;

	case ELiveCodingCompileResult::Failure:
		bSuccess = false;
		StatusStr = TEXT("failed");
		MessageStr = TEXT("Live Coding compilation failed. Check Output Log for errors.");
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: Compilation failed (%.1fs)"), CompilationTime);
		break;

	case ELiveCodingCompileResult::Cancelled:
		bSuccess = false;
		StatusStr = TEXT("unsupported_change");
		MessageStr = TEXT("Live Coding compilation was cancelled by Unreal, commonly because the change set is not supported by Live Coding. Run build-and-relaunch --wait for a full rebuild.");
		Result->SetBoolField(TEXT("needs_full_build"), true);
		Result->SetStringField(TEXT("cancelled_reason"), TEXT("unsupported_or_rejected_change_set"));
		Result->SetStringField(TEXT("recovery_hint"), TEXT("Run soft-ue-cli build-and-relaunch --wait, then retry after the editor relaunches."));
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: Compilation cancelled"));
		break;

	case ELiveCodingCompileResult::CompileStillActive:
		bSuccess = false;
		StatusStr = TEXT("busy");
		MessageStr = TEXT("A prior Live Coding compile is still in progress");
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: Compile still active"));
		break;

	case ELiveCodingCompileResult::NotStarted:
		bSuccess = false;
		StatusStr = TEXT("not_started");
		MessageStr = TEXT("Live Coding monitor could not be started");
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: Could not start"));
		break;

	case ELiveCodingCompileResult::InProgress:
	default:
		bSuccess = false;
		StatusStr = TEXT("unknown");
		MessageStr = FString::Printf(TEXT("Unexpected compile result: %d"), static_cast<int32>(CompileResult));
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("trigger-live-coding: Unexpected result %d"), static_cast<int32>(CompileResult));
		break;
	}

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("status"), StatusStr);
	Result->SetStringField(TEXT("message"), MessageStr);

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UTriggerLiveCodingTool::ExecuteAsynchronous(ILiveCodingModule* LiveCodingModule)
{
	// Just trigger compilation and return immediately
	LiveCodingModule->Compile(ELiveCodingCompileFlags::None, nullptr);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("triggered_async"));
	Result->SetStringField(TEXT("message"), TEXT("Live Coding compilation initiated. Check Output Log for results."));
	Result->SetStringField(TEXT("shortcut"), TEXT("Ctrl+Alt+F11"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("trigger-live-coding: Async compilation triggered"));

	return FBridgeToolResult::Json(Result);
}
#endif
