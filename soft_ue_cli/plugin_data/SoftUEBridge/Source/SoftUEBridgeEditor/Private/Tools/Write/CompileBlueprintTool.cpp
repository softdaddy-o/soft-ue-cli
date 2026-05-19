// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/CompileBlueprintTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"

namespace
{
	static FString SeverityToString(EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error:
			return TEXT("error");
		case EMessageSeverity::Warning:
			return TEXT("warning");
		case EMessageSeverity::PerformanceWarning:
			return TEXT("performance_warning");
		case EMessageSeverity::Info:
			return TEXT("info");
		default:
			return TEXT("unknown");
		}
	}

	static TArray<TSharedPtr<FJsonValue>> BuildCompilerDiagnostics(const FCompilerResultsLog& ResultsLog)
	{
		TArray<TSharedPtr<FJsonValue>> Diagnostics;
		for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
		{
			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("severity"), SeverityToString(Message->GetSeverity()));
			Diagnostic->SetStringField(TEXT("message"), Message->ToText().ToString());
			Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
		}
		return Diagnostics;
	}
}

FString UCompileBlueprintTool::GetToolDescription() const
{
	return TEXT("Compile a Blueprint or AnimBlueprint and return the compilation result (success, warnings, errors). Use after graph modifications to validate changes.");
}

TMap<FString, FBridgeSchemaProperty> UCompileBlueprintTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint or AnimBlueprint");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UCompileBlueprintTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UCompileBlueprintTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-blueprint: %s"), *AssetPath);

	// Load the asset as Blueprint
	FString LoadError;
	UBlueprint* Blueprint = FBridgeAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Refresh nodes before compiling to ensure pins are up to date
	FBridgeAssetModifier::RefreshBlueprintNodes(Blueprint);

	// Compile with an operation-local results log so callers can associate
	// diagnostics with this compile request without scraping stale output logs.
	FCompilerResultsLog ResultsLog;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);
	const bool bSuccess = Blueprint->Status != BS_Error && ResultsLog.NumErrors == 0;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetNumberField(TEXT("error_count"), ResultsLog.NumErrors);
	Result->SetNumberField(TEXT("warning_count"), ResultsLog.NumWarnings);
	Result->SetArrayField(TEXT("diagnostics"), BuildCompilerDiagnostics(ResultsLog));

	// Report status
	if (Blueprint->Status == BS_UpToDate)
	{
		Result->SetStringField(TEXT("status"), TEXT("up_to_date"));
	}
	else if (Blueprint->Status == BS_UpToDateWithWarnings)
	{
		Result->SetStringField(TEXT("status"), TEXT("warnings"));
	}
	else if (Blueprint->Status == BS_Error)
	{
		Result->SetStringField(TEXT("status"), TEXT("error"));
	}
	else
	{
		Result->SetStringField(TEXT("status"), TEXT("unknown"));
	}

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Blueprint compilation failed with errors"));
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("compile-blueprint: %s failed with %d errors"),
			*AssetPath,
			ResultsLog.NumErrors);
	}
	else
	{
		Result->SetBoolField(TEXT("needs_save"), true);
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-blueprint: %s compiled successfully"), *AssetPath);
	}

	return FBridgeToolResult::Json(Result);
}
