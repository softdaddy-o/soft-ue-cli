// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/CompileBlueprintTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

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

	// Compile
	FString CompileError;
	bool bSuccess = FBridgeAssetModifier::CompileBlueprint(Blueprint, CompileError);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSuccess);

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
		Result->SetStringField(TEXT("error"), CompileError);
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("compile-blueprint: %s failed: %s"), *AssetPath, *CompileError);
	}
	else
	{
		Result->SetBoolField(TEXT("needs_save"), true);
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-blueprint: %s compiled successfully"), *AssetPath);
	}

	return FBridgeToolResult::Json(Result);
}
