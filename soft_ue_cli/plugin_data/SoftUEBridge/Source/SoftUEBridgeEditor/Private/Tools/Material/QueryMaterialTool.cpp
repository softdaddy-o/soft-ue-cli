// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Material/QueryMaterialTool.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialFunction.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

FString UQueryMaterialTool::GetToolDescription() const
{
	return TEXT("Query Material, MaterialInstance, or MaterialFunction structure: expression graph and parameters. "
		"Use 'include' to select 'graph', 'parameters', or 'all' (default).");
}

TMap<FString, FBridgeSchemaProperty> UQueryMaterialTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Material, MaterialInstance, or MaterialFunction");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("string");
	Include.Description = TEXT("What to include: 'graph', 'parameters', or 'all' (default: 'all')");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	FBridgeSchemaProperty IncludePositions;
	IncludePositions.Type = TEXT("boolean");
	IncludePositions.Description = TEXT("Include expression X/Y positions (default: false)");
	IncludePositions.bRequired = false;
	Schema.Add(TEXT("include_positions"), IncludePositions);

	FBridgeSchemaProperty IncludeDefaults;
	IncludeDefaults.Type = TEXT("boolean");
	IncludeDefaults.Description = TEXT("Include default parameter values (default: true)");
	IncludeDefaults.bRequired = false;
	Schema.Add(TEXT("include_defaults"), IncludeDefaults);

	FBridgeSchemaProperty ParameterFilter;
	ParameterFilter.Type = TEXT("string");
	ParameterFilter.Description = TEXT("Filter parameters by name (wildcards supported)");
	ParameterFilter.bRequired = false;
	Schema.Add(TEXT("parameter_filter"), ParameterFilter);

	FBridgeSchemaProperty ParentChain;
	ParentChain.Type = TEXT("boolean");
	ParentChain.Description = TEXT("Include full parent material chain from leaf to root (default: false)");
	ParentChain.bRequired = false;
	Schema.Add(TEXT("parent_chain"), ParentChain);

	return Schema;
}

TArray<FString> UQueryMaterialTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryMaterialTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString Include = GetStringArgOrDefault(Arguments, TEXT("include"), TEXT("all")).ToLower();
	bool bIncludePositions = GetBoolArgOrDefault(Arguments, TEXT("include_positions"), false);
	bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), true);
	FString ParameterFilter = GetStringArgOrDefault(Arguments, TEXT("parameter_filter"), TEXT(""));
	bool bParentChain = GetBoolArgOrDefault(Arguments, TEXT("parent_chain"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-material: path='%s', include='%s'"), *AssetPath, *Include);

	bool bIncludeGraph = (Include == TEXT("all") || Include == TEXT("graph"));
	bool bIncludeParams = (Include == TEXT("all") || Include == TEXT("parameters"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Try MaterialInstance first
	UMaterialInstance* MatInstance = LoadObject<UMaterialInstance>(nullptr, *AssetPath);
	if (MatInstance)
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("MaterialInstance"));
		if (MatInstance->Parent)
		{
			Result->SetStringField(TEXT("parent_material"), MatInstance->Parent->GetPathName());
		}

		if (bIncludeGraph)
		{
			Result->SetStringField(TEXT("graph_note"), TEXT("MaterialInstances don't have expression graphs. Query the parent Material."));
		}

		if (bIncludeParams)
		{
			Result->SetObjectField(TEXT("parameters"), ExtractParameters(MatInstance, bIncludeDefaults, ParameterFilter));
		}

		if (bParentChain)
		{
			Result->SetArrayField(TEXT("parent_chain"), ExtractParentChain(MatInstance));
		}

		return FBridgeToolResult::Json(Result);
	}

	// Try base Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *AssetPath);
	if (!Material)
	{
		// Try MaterialFunction
		UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *AssetPath);
		if (MatFunc)
		{
			Result->SetStringField(TEXT("asset_type"), TEXT("MaterialFunction"));
			if (!MatFunc->Description.IsEmpty())
			{
				Result->SetStringField(TEXT("description"), MatFunc->Description);
			}

			if (bIncludeGraph)
			{
				TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);
				TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
				for (UMaterialExpression* Expression : MatFunc->GetExpressions())
				{
					if (!Expression) continue;
					TSharedPtr<FJsonObject> ExprJson = ExpressionToJson(Expression, bIncludePositions);
					if (ExprJson.IsValid())
					{
						ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
					}
				}
				GraphJson->SetArrayField(TEXT("expressions"), ExpressionsArray);
				GraphJson->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());
				Result->SetObjectField(TEXT("graph"), GraphJson);
			}

			if (bIncludeParams)
			{
				Result->SetStringField(TEXT("parameters_note"),
					TEXT("MaterialFunctions do not expose parameters directly. Parameter expressions are visible as nodes in the graph."));
			}

			return FBridgeToolResult::Json(Result);
		}

		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load Material or MaterialFunction: %s"), *AssetPath));
	}

	Result->SetStringField(TEXT("asset_type"), TEXT("Material"));

	if (bIncludeGraph)
	{
		Result->SetObjectField(TEXT("graph"), ExtractGraph(Material, bIncludePositions));
	}

	if (bIncludeParams)
	{
		Result->SetObjectField(TEXT("parameters"), ExtractParameters(Material, bIncludeDefaults, ParameterFilter));
	}

	if (bParentChain)
	{
		Result->SetArrayField(TEXT("parent_chain"), ExtractParentChain(Material));
	}

	return FBridgeToolResult::Json(Result);
}

// === Graph extraction ===

TSharedPtr<FJsonObject> UQueryMaterialTool::ExtractGraph(UMaterial* Material, bool bIncludePositions) const
{
	if (!Material)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);

	// Get all expressions
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;

		TSharedPtr<FJsonObject> ExprJson = ExpressionToJson(Expression, bIncludePositions);
		if (ExprJson.IsValid())
		{
			ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
		}
	}

	GraphJson->SetArrayField(TEXT("expressions"), ExpressionsArray);
	GraphJson->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());

	return GraphJson;
}

TSharedPtr<FJsonObject> UQueryMaterialTool::ExpressionToJson(UMaterialExpression* Expression, bool bIncludePositions) const
{
	if (!Expression)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ExprJson = MakeShareable(new FJsonObject);

	ExprJson->SetStringField(TEXT("name"), Expression->GetName());
	ExprJson->SetStringField(TEXT("guid"), Expression->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ExprJson->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
	ExprJson->SetStringField(TEXT("description"), Expression->GetDescription());

	if (bIncludePositions)
	{
		ExprJson->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
		ExprJson->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
	}

	// Inputs — use GetInputsView() for bounded iteration.
	// Some expression subclasses never return nullptr from GetInput() on
	// out-of-range indices, which can spin forever and OOM the editor.
	TArray<TSharedPtr<FJsonValue>> InputsArray;
	TArrayView<FExpressionInput*> Inputs = Expression->GetInputsView();
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		FExpressionInput* Input = Inputs[i];
		if (!Input) continue;

		TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);
		InputJson->SetStringField(TEXT("name"), Expression->GetInputName(i).ToString());
		InputJson->SetBoolField(TEXT("connected"), Input->Expression != nullptr);

		if (Input->Expression)
		{
			InputJson->SetStringField(TEXT("connected_to"), Input->Expression->GetName());
			InputJson->SetNumberField(TEXT("output_index"), Input->OutputIndex);
		}

		InputsArray.Add(MakeShareable(new FJsonValueObject(InputJson)));
	}
	ExprJson->SetArrayField(TEXT("inputs"), InputsArray);

	return ExprJson;
}

// === Parameter extraction ===

TSharedPtr<FJsonObject> UQueryMaterialTool::ExtractParameters(UMaterialInterface* Material,
	bool bIncludeDefaults, const FString& ParameterFilter) const
{
	if (!Material)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ParamsJson = MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> ScalarArray;
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	TArray<TSharedPtr<FJsonValue>> TextureArray;
	TArray<TSharedPtr<FJsonValue>> SwitchArray;

	ExtractScalarParameters(Material, ScalarArray);
	ExtractVectorParameters(Material, VectorArray);
	ExtractTextureParameters(Material, TextureArray);
	ExtractStaticSwitchParameters(Material, SwitchArray);

	ParamsJson->SetArrayField(TEXT("scalar"), ScalarArray);
	ParamsJson->SetArrayField(TEXT("vector"), VectorArray);
	ParamsJson->SetArrayField(TEXT("texture"), TextureArray);
	ParamsJson->SetArrayField(TEXT("static_switch"), SwitchArray);

	ParamsJson->SetNumberField(TEXT("total_count"),
		ScalarArray.Num() + VectorArray.Num() + TextureArray.Num() + SwitchArray.Num());

	return ParamsJson;
}

void UQueryMaterialTool::ExtractScalarParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllScalarParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		float Value = 0.0f;
		Material->GetScalarParameterValue(Info, Value);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetNumberField(TEXT("value"), Value);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractVectorParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllVectorParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		FLinearColor Value;
		Material->GetVectorParameterValue(Info, Value);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());

		TSharedPtr<FJsonObject> ColorJson = MakeShareable(new FJsonObject);
		ColorJson->SetNumberField(TEXT("r"), Value.R);
		ColorJson->SetNumberField(TEXT("g"), Value.G);
		ColorJson->SetNumberField(TEXT("b"), Value.B);
		ColorJson->SetNumberField(TEXT("a"), Value.A);
		ParamJson->SetObjectField(TEXT("value"), ColorJson);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractTextureParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		UTexture* Texture = nullptr;
		Material->GetTextureParameterValue(Info, Texture);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetStringField(TEXT("value"), Texture ? Texture->GetPathName() : TEXT("None"));

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractStaticSwitchParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		bool bValue = false;
		FGuid OutGuid;
		Material->GetStaticSwitchParameterValue(Info, bValue, OutGuid);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetBoolField(TEXT("value"), bValue);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

TArray<TSharedPtr<FJsonValue>> UQueryMaterialTool::ExtractParentChain(UMaterialInterface* Material) const
{
	TArray<TSharedPtr<FJsonValue>> Chain;
	UMaterialInterface* Current = Material;
	while (Current)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), Current->GetName());
		Entry->SetStringField(TEXT("path"), Current->GetPathName());
		Entry->SetStringField(TEXT("class"), Current->GetClass()->GetName());
		Chain.Add(MakeShareable(new FJsonValueObject(Entry)));
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(Current))
		{
			Current = MI->Parent;
		}
		else
		{
			break;
		}
	}
	return Chain;
}
