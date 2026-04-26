// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/SetConfigValueTool.h"

#include "Misc/ConfigCacheIni.h"
#include "SoftUEBridgeModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "Tools/GetConfigValueTool.h"

REGISTER_BRIDGE_TOOL(USetConfigValueTool)

FString USetConfigValueTool::GetToolDescription() const
{
	return TEXT("Set a configuration value in UE's runtime GConfig and flush to disk.");
}

TMap<FString, FBridgeSchemaProperty> USetConfigValueTool::GetInputSchema() const
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
	Schema.Add(TEXT("value"), Prop(TEXT("string"), TEXT("Value to set"), true));
	Schema.Add(TEXT("config_type"), Prop(TEXT("string"), TEXT("Config category: Engine, Game, Input, Editor, etc."), true));
	return Schema;
}

FBridgeToolResult USetConfigValueTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString Section = GetStringArgOrDefault(Args, TEXT("section"));
	const FString Key = GetStringArgOrDefault(Args, TEXT("key"));
	const FString Value = GetStringArgOrDefault(Args, TEXT("value"));
	const FString ConfigType = GetStringArgOrDefault(Args, TEXT("config_type"));

	if (Section.IsEmpty() || Key.IsEmpty() || ConfigType.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("section, key, value, and config_type are all required"));
	}

	const FString Filename = UGetConfigValueTool::ConfigTypeToFilename(ConfigType);
	if (Filename.IsEmpty())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Unknown config_type: '%s'"), *ConfigType));
	}

	if (!GConfig)
	{
		return FBridgeToolResult::Error(TEXT("GConfig is not available"));
	}

	GConfig->SetString(*Section, *Key, *Value, Filename);
	GConfig->Flush(false, Filename);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("value"), Value);

	UE_LOG(LogSoftUEBridge, Log, TEXT("set-config-value: [%s]%s = %s"), *Section, *Key, *Value);
	return FBridgeToolResult::Json(Result);
}
