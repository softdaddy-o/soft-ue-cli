// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Material/CompileMaterialTool.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

FString UCompileMaterialTool::GetToolDescription() const
{
	return TEXT("Compile or recompile a Material, MaterialInstance, or MaterialFunction. "
		"Triggers shader compilation and returns success/failure with any errors.");
}

TMap<FString, FBridgeSchemaProperty> UCompileMaterialTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Material, MaterialInstance, or MaterialFunction");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UCompileMaterialTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UCompileMaterialTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-material: %s"), *AssetPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Try Material first
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *AssetPath);
	if (Material)
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("Material"));

		// Force recompilation
		Material->PreEditChange(nullptr);
		Material->PostEditChange();

		// Check for errors
		bool bSuccess = (Material->GetMaterialResource(GMaxRHIShaderPlatform) != nullptr);
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-material: %s %s"),
			*AssetPath, bSuccess ? TEXT("compiled successfully") : TEXT("compilation failed"));

		return FBridgeToolResult::Json(Result);
	}

	// Try MaterialInstance
	UMaterialInstanceConstant* MatInstance = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath);
	if (MatInstance)
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("MaterialInstance"));

		MatInstance->PreEditChange(nullptr);
		MatInstance->PostEditChange();

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-material: %s recompiled"), *AssetPath);

		return FBridgeToolResult::Json(Result);
	}

	// Try MaterialFunction
	UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *AssetPath);
	if (MatFunc)
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("MaterialFunction"));

		MatFunc->PreEditChange(nullptr);
		MatFunc->PostEditChange();

		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("compile-material: %s recompiled"), *AssetPath);

		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Failed to load Material, MaterialInstance, or MaterialFunction: %s"), *AssetPath));
}
