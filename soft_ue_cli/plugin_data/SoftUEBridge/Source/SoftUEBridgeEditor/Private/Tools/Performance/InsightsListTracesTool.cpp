// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Performance/InsightsListTracesTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

FString UInsightsListTracesTool::GetToolDescription() const
{
	return TEXT("List available Unreal Insights trace files (.utrace) in the project's Saved/Profiling directory. Returns file paths, sizes, and creation times.");
}

TMap<FString, FBridgeSchemaProperty> UInsightsListTracesTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Directory;
	Directory.Type = TEXT("string");
	Directory.Description = TEXT("Directory to search for trace files. Default: Project/Saved/Profiling");
	Directory.bRequired = false;
	Schema.Add(TEXT("directory"), Directory);

	return Schema;
}

FBridgeToolResult UInsightsListTracesTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Get directory to search
	FString TraceDir = GetStringArgOrDefault(Arguments, TEXT("directory"));
	if (TraceDir.IsEmpty())
	{
		TraceDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"));
	}

	// Normalize path
	FPaths::NormalizeDirectoryName(TraceDir);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-list-traces: Searching in %s"), *TraceDir);

	// Check if directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TraceDir))
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("directory"), TraceDir);
		Result->SetNumberField(TEXT("count"), 0);
		Result->SetArrayField(TEXT("traces"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetStringField(TEXT("message"), TEXT("Directory does not exist"));

		return FBridgeToolResult::Json(Result);
	}

	// Find all .utrace files
	TArray<FString> TraceFiles;
	IFileManager& FileManager = IFileManager::Get();
	FString SearchPattern = FPaths::Combine(TraceDir, TEXT("*.utrace"));
	FileManager.FindFiles(TraceFiles, *SearchPattern, true, false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-list-traces: Found %d trace files"), TraceFiles.Num());

	// Build result
	TArray<TSharedPtr<FJsonValue>> TracesArray;
	for (const FString& FileName : TraceFiles)
	{
		FString FullPath = FPaths::Combine(TraceDir, FileName);

		// Get file info
		FFileStatData StatData = FileManager.GetStatData(*FullPath);

		TSharedPtr<FJsonObject> TraceObj = MakeShareable(new FJsonObject);
		TraceObj->SetStringField(TEXT("file_path"), FullPath);
		TraceObj->SetStringField(TEXT("file_name"), FileName);
		TraceObj->SetNumberField(TEXT("size_bytes"), StatData.FileSize);
		TraceObj->SetNumberField(TEXT("size_mb"), static_cast<double>(StatData.FileSize) / (1024.0 * 1024.0));
		TraceObj->SetStringField(TEXT("created"), StatData.CreationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		TraceObj->SetStringField(TEXT("modified"), StatData.ModificationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));

		TracesArray.Add(MakeShareable(new FJsonValueObject(TraceObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("directory"), TraceDir);
	Result->SetNumberField(TEXT("count"), TraceFiles.Num());
	Result->SetArrayField(TEXT("traces"), TracesArray);

	return FBridgeToolResult::Json(Result);
}
