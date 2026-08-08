// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Performance/InsightsCaptureTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	bool GBridgeInsightsCaptureRequested = false;
	FString GBridgeInsightsTraceFile;
}

FString UInsightsCaptureTool::GetToolDescription() const
{
	return TEXT("Control Unreal Insights trace capture. Actions: start (with optional channels), stop, status. Returns trace file path on stop.");
}

TMap<FString, FBridgeSchemaProperty> UInsightsCaptureTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action to perform: 'start', 'stop', 'status'");
	Action.bRequired = true;
	Action.Enum = {TEXT("start"), TEXT("stop"), TEXT("status")};
	Schema.Add(TEXT("action"), Action);

	FBridgeSchemaProperty Channels;
	Channels.Type = TEXT("array");
	Channels.ItemsType = TEXT("string");
	Channels.Description = TEXT("Trace channels to enable (for 'start' action): 'cpu', 'gpu', 'memory', 'stats', 'loadtime', etc. Default: ['cpu', 'gpu', 'frame']");
	Channels.bRequired = false;
	Schema.Add(TEXT("channels"), Channels);

	FBridgeSchemaProperty OutputFile;
	OutputFile.Type = TEXT("string");
	OutputFile.Description = TEXT("Output filename for trace (for 'start' action). Default: auto-generated with timestamp");
	OutputFile.bRequired = false;
	Schema.Add(TEXT("output_file"), OutputFile);

	return Schema;
}

FBridgeToolResult UInsightsCaptureTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString Action = GetStringArgOrDefault(Arguments, TEXT("action"));

	if (Action == TEXT("start"))
	{
		return StartCapture(Arguments);
	}
	else if (Action == TEXT("stop"))
	{
		return StopCapture();
	}
	else if (Action == TEXT("status"))
	{
		return GetStatus();
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid action: %s. Use 'start', 'stop', or 'status'"), *Action));
	}
}

FBridgeToolResult UInsightsCaptureTool::StartCapture(const TSharedPtr<FJsonObject>& Arguments)
{
	// Check if already running
	if (FTraceAuxiliary::IsConnected())
	{
		return FBridgeToolResult::Error(TEXT("Trace capture is already running. Stop current trace before starting a new one."));
	}

	// Get channels
	TArray<FString> Channels;
	const TArray<TSharedPtr<FJsonValue>>* ChannelsArray;
	if (Arguments->TryGetArrayField(TEXT("channels"), ChannelsArray))
	{
		for (const TSharedPtr<FJsonValue>& ChannelValue : *ChannelsArray)
		{
			Channels.Add(ChannelValue->AsString());
		}
	}
	else
	{
		// Default channels
		Channels = {TEXT("cpu"), TEXT("gpu"), TEXT("frame")};
	}

	// Get output file name
	FString OutputFile = GetStringArgOrDefault(Arguments, TEXT("output_file"));
	if (OutputFile.IsEmpty())
	{
		// Generate filename with timestamp
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		OutputFile = FString::Printf(TEXT("BridgeTrace_%s.utrace"), *Timestamp);
	}

	// Build trace file path. ProjectSavedDir() is relative to the engine binaries
	// directory, so make it absolute - the trace path is reported back to callers
	// and is later handed to insights-analyze.
	FString TraceDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling")));
	IFileManager::Get().MakeDirectory(*TraceDir, true);

	if (!OutputFile.EndsWith(TEXT(".utrace")))
	{
		OutputFile += TEXT(".utrace");
	}
	FString TraceFilePath = FPaths::Combine(TraceDir, OutputFile);

	// Build channel string
	FString ChannelString = FString::Join(Channels, TEXT(","));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-capture: Starting trace with channels: %s"), *ChannelString);
	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-capture: Output file: %s"), *TraceFilePath);

	// Call FTraceAuxiliary directly rather than issuing "Trace.File <path> <channels>".
	// UE splits console command arguments on whitespace without honouring quotes, so
	// any project path containing a space (e.g. "Unreal Projects") arrives as 3+ args
	// and the command is rejected with "Invalid arguments". Worse, GEngine->Exec still
	// returns true because the command was *handled*, so the failure was silent.
	// This API takes the path as a real parameter and returns a genuine success bool.
	if (!FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::File,
			*TraceFilePath,
			*ChannelString,
			nullptr,
			LogSoftUEBridgeEditor))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to start trace capture to '%s'. Check the editor log for the reason "
			     "(a trace connection may already be active, or the path may not be writable)."),
			*TraceFilePath));
	}

	GBridgeInsightsCaptureRequested = true;
	GBridgeInsightsTraceFile = TraceFilePath;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("started"));
	Result->SetStringField(TEXT("trace_file"), TraceFilePath);
	Result->SetBoolField(TEXT("is_capturing"), FTraceAuxiliary::IsConnected());
	Result->SetArrayField(TEXT("channels"), [&Channels]()
	{
		TArray<TSharedPtr<FJsonValue>> ChannelsJson;
		for (const FString& Channel : Channels)
		{
			ChannelsJson.Add(MakeShareable(new FJsonValueString(Channel)));
		}
		return ChannelsJson;
	}());

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsCaptureTool::StopCapture()
{
	if (!FTraceAuxiliary::IsConnected() && !GBridgeInsightsCaptureRequested)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("idle"));
		Result->SetStringField(TEXT("message"), TEXT("No active trace capture to stop"));
		return FBridgeToolResult::Json(Result);
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-capture: Stopping trace"));

	// Stop via the API for the same reason as StartCapture: GEngine->Exec reports
	// whether the command was handled, not whether tracing actually stopped.
	const bool bStopped = FTraceAuxiliary::Stop();

	GBridgeInsightsCaptureRequested = false;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	if (!bStopped && !FTraceAuxiliary::IsConnected())
	{
		Result->SetStringField(TEXT("status"), TEXT("idle"));
		Result->SetStringField(TEXT("message"), TEXT("Trace capture was already inactive"));
		return FBridgeToolResult::Json(Result);
	}

	if (FTraceAuxiliary::IsConnected())
	{
		return FBridgeToolResult::Error(TEXT("Failed to stop trace capture; the trace connection is still active."));
	}

	Result->SetStringField(TEXT("status"), TEXT("stopped"));
	Result->SetStringField(TEXT("message"), TEXT("Trace capture stopped successfully"));
	if (!GBridgeInsightsTraceFile.IsEmpty())
	{
		Result->SetStringField(TEXT("trace_file"), GBridgeInsightsTraceFile);

		// The file is only complete once tracing has stopped, so report whether it
		// actually landed rather than leaving the caller to discover an absent file.
		Result->SetBoolField(TEXT("trace_file_exists"), FPaths::FileExists(GBridgeInsightsTraceFile));
	}

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsCaptureTool::GetStatus()
{
	bool bIsCapturing = FTraceAuxiliary::IsConnected() || GBridgeInsightsCaptureRequested;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("is_capturing"), bIsCapturing);
	Result->SetStringField(TEXT("status"), bIsCapturing ? TEXT("active") : TEXT("idle"));
	if (!GBridgeInsightsTraceFile.IsEmpty())
	{
		Result->SetStringField(TEXT("trace_file"), GBridgeInsightsTraceFile);
	}

	return FBridgeToolResult::Json(Result);
}
