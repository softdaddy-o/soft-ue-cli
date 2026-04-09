// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/ValidateConfigKeyTool.h"

#include "Misc/ConfigCacheIni.h"
#include "Tools/BridgeToolRegistry.h"
#include "Tools/GetConfigValueTool.h"

REGISTER_BRIDGE_TOOL(UValidateConfigKeyTool)

FString UValidateConfigKeyTool::GetToolDescription() const
{
	return TEXT("Check whether a config section/key is known to the running engine. Returns validity, current value, and inferred type.");
}

TMap<FString, FBridgeSchemaProperty> UValidateConfigKeyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	auto Prop = [](const FString& Type, const FString& Desc, bool bReq = false) {
		FBridgeSchemaProperty P;
		P.Type = Type;
		P.Description = Desc;
		P.bRequired = bReq;
		return P;
	};

	Schema.Add(TEXT("section"), Prop(TEXT("string"), TEXT("Config section"), true));
	Schema.Add(TEXT("key"), Prop(TEXT("string"), TEXT("Config key"), true));
	Schema.Add(TEXT("config_type"), Prop(TEXT("string"), TEXT("Config category: Engine, Game, etc."), true));
	return Schema;
}

FBridgeToolResult UValidateConfigKeyTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString Section = GetStringArgOrDefault(Args, TEXT("section"));
	const FString Key = GetStringArgOrDefault(Args, TEXT("key"));
	const FString ConfigType = GetStringArgOrDefault(Args, TEXT("config_type"));

	const FString Filename = UGetConfigValueTool::ConfigTypeToFilename(ConfigType);
	if (Filename.IsEmpty())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Unknown config_type: '%s'"), *ConfigType));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);

	if (!GConfig)
	{
		Result->SetBoolField(TEXT("valid"), false);
		Result->SetStringField(TEXT("reason"), TEXT("GConfig is not available"));
		return FBridgeToolResult::Json(Result);
	}

	FString Value;
	bool bHasSection = false;
	if (!UGetConfigValueTool::TryGetConfigValue(Section, Key, Filename, Value, &bHasSection))
	{
		Result->SetBoolField(TEXT("valid"), false);
		Result->SetStringField(TEXT("reason"), bHasSection ? TEXT("key not found in section") : TEXT("section not found"));
		return FBridgeToolResult::Json(Result);
	}

	Result->SetBoolField(TEXT("valid"), true);
	Result->SetStringField(TEXT("current_value"), Value);

	if (Value.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("False"), ESearchCase::IgnoreCase))
	{
		Result->SetStringField(TEXT("type"), TEXT("bool"));
	}
	else if (Value.IsNumeric())
	{
		Result->SetStringField(TEXT("type"), Value.Contains(TEXT(".")) ? TEXT("float") : TEXT("int"));
	}
	else
	{
		Result->SetStringField(TEXT("type"), TEXT("string"));
	}

	return FBridgeToolResult::Json(Result);
}
