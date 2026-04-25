// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Performance/InsightsAnalyzeTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FString UInsightsAnalyzeTool::GetToolDescription() const
{
	return TEXT("Analyze Unreal Insights trace files. Currently supports 'basic_info' analysis type (file metadata). Full trace analysis requires TraceAnalysis module integration (future enhancement).");
}

TMap<FString, FBridgeSchemaProperty> UInsightsAnalyzeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty TraceFile;
	TraceFile.Type = TEXT("string");
	TraceFile.Description = TEXT("Path to trace file (.utrace) to analyze");
	TraceFile.bRequired = true;
	Schema.Add(TEXT("trace_file"), TraceFile);

	FBridgeSchemaProperty AnalysisType;
	AnalysisType.Type = TEXT("string");
	AnalysisType.Description = TEXT("Type of analysis: 'basic_info' (default). Future: 'frame_stats', 'top_functions', 'memory_summary'");
	AnalysisType.bRequired = false;
	AnalysisType.Enum = {TEXT("basic_info")};
	Schema.Add(TEXT("analysis_type"), AnalysisType);

	return Schema;
}

FBridgeToolResult UInsightsAnalyzeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString TraceFile = GetStringArgOrDefault(Arguments, TEXT("trace_file"));
	if (TraceFile.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Missing required argument: trace_file"));
	}

	FString AnalysisType = GetStringArgOrDefault(Arguments, TEXT("analysis_type"), TEXT("basic_info"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-analyze: Analyzing %s with type %s"), *TraceFile, *AnalysisType);

	// Check if file exists
	if (!FPaths::FileExists(TraceFile))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Trace file not found: %s"), *TraceFile));
	}

	if (AnalysisType == TEXT("basic_info"))
	{
		return AnalyzeBasicInfo(TraceFile);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Analysis type '%s' not yet implemented. Currently only 'basic_info' is supported. "
			     "Full trace analysis (frame_stats, top_functions, etc.) requires TraceAnalysis module integration."),
			*AnalysisType));
	}
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeBasicInfo(const FString& TraceFile)
{
	IFileManager& FileManager = IFileManager::Get();

	// Get file info
	FFileStatData StatData = FileManager.GetStatData(*TraceFile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("trace_file"), TraceFile);
	Result->SetStringField(TEXT("file_name"), FPaths::GetCleanFilename(TraceFile));
	Result->SetNumberField(TEXT("size_bytes"), StatData.FileSize);
	Result->SetNumberField(TEXT("size_mb"), static_cast<double>(StatData.FileSize) / (1024.0 * 1024.0));
	Result->SetStringField(TEXT("created"), StatData.CreationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Result->SetStringField(TEXT("modified"), StatData.ModificationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Result->SetStringField(TEXT("analysis_type"), TEXT("basic_info"));
	Result->SetStringField(TEXT("note"), TEXT("Full trace analysis (frame stats, CPU profiling, etc.) requires TraceAnalysis module integration. Use Unreal Insights standalone application for detailed analysis."));

	return FBridgeToolResult::Json(Result);
}
