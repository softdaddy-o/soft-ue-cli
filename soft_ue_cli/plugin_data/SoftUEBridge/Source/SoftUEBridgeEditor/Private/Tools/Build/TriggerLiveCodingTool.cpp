// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Build/TriggerLiveCodingTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"

// Live Coding module (Windows only)
#if PLATFORM_WINDOWS
	#include "ILiveCodingModule.h"
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
		StatusStr = TEXT("cancelled");
		MessageStr = TEXT("Live Coding compilation was cancelled");
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
