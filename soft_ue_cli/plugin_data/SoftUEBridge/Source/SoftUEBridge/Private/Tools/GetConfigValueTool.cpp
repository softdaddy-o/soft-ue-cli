// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/GetConfigValueTool.h"

#include "Misc/ConfigCacheIni.h"
#include "SoftUEBridgeModule.h"
#include "Tools/BridgeToolRegistry.h"

REGISTER_BRIDGE_TOOL(UGetConfigValueTool)

FString UGetConfigValueTool::GetToolDescription() const
{
	return TEXT("Get a configuration value from UE's runtime GConfig. Returns the effective value after all config layers are merged.");
}

TMap<FString, FBridgeSchemaProperty> UGetConfigValueTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	auto Prop = [](const FString& Type, const FString& Desc, bool bReq = false) {
		FBridgeSchemaProperty P;
		P.Type = Type;
		P.Description = Desc;
		P.bRequired = bReq;
		return P;
	};

	Schema.Add(TEXT("section"), Prop(TEXT("string"), TEXT("Config section (e.g., /Script/Engine.RendererSettings)"), true));
	Schema.Add(TEXT("key"), Prop(TEXT("string"), TEXT("Config key (e.g., r.DefaultFeature.AutoExposure)"), true));
	Schema.Add(TEXT("config_type"), Prop(TEXT("string"), TEXT("Config category: Engine, Game, Input, Editor, etc."), true));
	return Schema;
}

FBridgeToolResult UGetConfigValueTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString Section = GetStringArgOrDefault(Args, TEXT("section"));
	const FString Key = GetStringArgOrDefault(Args, TEXT("key"));
	const FString ConfigType = GetStringArgOrDefault(Args, TEXT("config_type"));

	if (Section.IsEmpty() || Key.IsEmpty() || ConfigType.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("section, key, and config_type are all required"));
	}

	const FString Filename = ConfigTypeToFilename(ConfigType);
	if (Filename.IsEmpty())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Unknown config_type: '%s'. Use Engine, Game, Input, Editor, etc."), *ConfigType));
	}

	FString Value;
	if (!GConfig || !GConfig->GetString(*Section, *Key, Value, Filename))
	{
		FConfigSection* Sec = GConfig ? GConfig->GetSectionPrivate(*Section, false, false, Filename) : nullptr;
		if (!Sec)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Section '%s' not found in %s config"), *Section, *ConfigType));
		}
		return FBridgeToolResult::Error(FString::Printf(TEXT("Key '%s' not found in section '%s'"), *Key, *Section));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetStringField(TEXT("config_type"), ConfigType);

	UE_LOG(LogSoftUEBridge, Log, TEXT("get-config-value: [%s]%s = %s"), *Section, *Key, *Value);
	return FBridgeToolResult::Json(Result);
}

FString UGetConfigValueTool::ConfigTypeToFilename(const FString& ConfigType)
{
	if (ConfigType.Equals(TEXT("Engine"), ESearchCase::IgnoreCase)) return GEngineIni;
	if (ConfigType.Equals(TEXT("Game"), ESearchCase::IgnoreCase)) return GGameIni;
	if (ConfigType.Equals(TEXT("Input"), ESearchCase::IgnoreCase)) return GInputIni;
	if (ConfigType.Equals(TEXT("Editor"), ESearchCase::IgnoreCase)) return GEditorIni;
	if (ConfigType.Equals(TEXT("EditorPerProjectUserSettings"), ESearchCase::IgnoreCase)) return GEditorPerProjectIni;
	if (ConfigType.Equals(TEXT("Scalability"), ESearchCase::IgnoreCase)) return GScalabilityIni;
	if (ConfigType.Equals(TEXT("GameUserSettings"), ESearchCase::IgnoreCase)) return GGameUserSettingsIni;
	if (ConfigType.Equals(TEXT("Hardware"), ESearchCase::IgnoreCase)) return GHardwareIni;
	return FString();
}
