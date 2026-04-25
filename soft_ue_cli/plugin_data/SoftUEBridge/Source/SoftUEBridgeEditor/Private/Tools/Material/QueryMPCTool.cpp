// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Material/QueryMPCTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Engine/World.h"

FString UQueryMPCTool::GetToolDescription() const
{
	return TEXT("Read or write Material Parameter Collection (MPC) values. "
		"Read returns both default (asset) and runtime (world) values. "
		"Write sets runtime values in the current world.");
}

TMap<FString, FBridgeSchemaProperty> UQueryMPCTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("MPC asset path (e.g. /Game/Materials/MPC_GlobalParams)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'read' (default) or 'write'");
	Action.bRequired = false;
	Schema.Add(TEXT("action"), Action);

	FBridgeSchemaProperty ParamName;
	ParamName.Type = TEXT("string");
	ParamName.Description = TEXT("Parameter name (required for write)");
	ParamName.bRequired = false;
	Schema.Add(TEXT("parameter_name"), ParamName);

	FBridgeSchemaProperty Value;
	Value.Type = TEXT("number");
	Value.Description = TEXT("Value for write: scalar number, or JSON array [r,g,b,a] for vector");
	Value.bRequired = false;
	Schema.Add(TEXT("value"), Value);

	FBridgeSchemaProperty World;
	World.Type = TEXT("string");
	World.Description = TEXT("World context: 'editor', 'pie', 'game'. Omit for first available.");
	World.bRequired = false;
	Schema.Add(TEXT("world"), World);

	return Schema;
}

TArray<FString> UQueryMPCTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryMPCTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString Action = GetStringArgOrDefault(Arguments, TEXT("action"), TEXT("read")).ToLower();
	FString ParamName = GetStringArgOrDefault(Arguments, TEXT("parameter_name"));
	FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"));

	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	// Load the MPC asset
	UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *AssetPath);
	if (!MPC)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load MPC: %s"), *AssetPath));
	}

	UWorld* World = FindWorldByType(WorldType);

	if (Action == TEXT("write"))
	{
		if (ParamName.IsEmpty())
		{
			return FBridgeToolResult::Error(TEXT("parameter_name is required for write action"));
		}
		if (!World)
		{
			return FBridgeToolResult::Error(TEXT("No world available for write. Specify 'world' parameter."));
		}

		// Check if it's a vector value (array)
		const TArray<TSharedPtr<FJsonValue>>* ValueArray;
		if (Arguments->TryGetArrayField(TEXT("value"), ValueArray) && ValueArray->Num() >= 3)
		{
			FLinearColor Color;
			Color.R = (*ValueArray)[0]->AsNumber();
			Color.G = (*ValueArray)[1]->AsNumber();
			Color.B = (*ValueArray)[2]->AsNumber();
			Color.A = ValueArray->Num() >= 4 ? (*ValueArray)[3]->AsNumber() : 1.0f;

			UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, FName(*ParamName), Color);

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("parameter"), ParamName);
			Result->SetStringField(TEXT("type"), TEXT("vector"));

			TSharedPtr<FJsonObject> ValJson = MakeShareable(new FJsonObject);
			ValJson->SetNumberField(TEXT("r"), Color.R);
			ValJson->SetNumberField(TEXT("g"), Color.G);
			ValJson->SetNumberField(TEXT("b"), Color.B);
			ValJson->SetNumberField(TEXT("a"), Color.A);
			Result->SetObjectField(TEXT("value"), ValJson);

			return FBridgeToolResult::Json(Result);
		}

		// Scalar value
		double ScalarValue = 0.0;
		if (!Arguments->TryGetNumberField(TEXT("value"), ScalarValue))
		{
			return FBridgeToolResult::Error(TEXT("value is required for write action (number for scalar, [r,g,b,a] for vector)"));
		}

		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, FName(*ParamName), static_cast<float>(ScalarValue));

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("parameter"), ParamName);
		Result->SetStringField(TEXT("type"), TEXT("scalar"));
		Result->SetNumberField(TEXT("value"), ScalarValue);

		return FBridgeToolResult::Json(Result);
	}

	// Read mode
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("name"), MPC->GetName());

	// Scalar parameters
	TArray<TSharedPtr<FJsonValue>> ScalarArray;
	for (const FCollectionScalarParameter& Param : MPC->ScalarParameters)
	{
		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Param.ParameterName.ToString());
		ParamJson->SetNumberField(TEXT("default_value"), Param.DefaultValue);

		// Try to get runtime value from world
		if (World)
		{
			float RuntimeValue = UKismetMaterialLibrary::GetScalarParameterValue(World, MPC, FName(Param.ParameterName));
			ParamJson->SetNumberField(TEXT("runtime_value"), RuntimeValue);
		}

		ScalarArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
	Result->SetArrayField(TEXT("scalar_parameters"), ScalarArray);

	// Vector parameters
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	for (const FCollectionVectorParameter& Param : MPC->VectorParameters)
	{
		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Param.ParameterName.ToString());

		auto ColorToJson = [](const FLinearColor& C) {
			TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
			J->SetNumberField(TEXT("r"), C.R);
			J->SetNumberField(TEXT("g"), C.G);
			J->SetNumberField(TEXT("b"), C.B);
			J->SetNumberField(TEXT("a"), C.A);
			return J;
		};

		ParamJson->SetObjectField(TEXT("default_value"), ColorToJson(Param.DefaultValue));

		if (World)
		{
			FLinearColor RuntimeValue = UKismetMaterialLibrary::GetVectorParameterValue(World, MPC, FName(Param.ParameterName));
			ParamJson->SetObjectField(TEXT("runtime_value"), ColorToJson(RuntimeValue));
		}

		VectorArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
	Result->SetArrayField(TEXT("vector_parameters"), VectorArray);

	Result->SetNumberField(TEXT("scalar_count"), ScalarArray.Num());
	Result->SetNumberField(TEXT("vector_count"), VectorArray.Num());

	return FBridgeToolResult::Json(Result);
}
