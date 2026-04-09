// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/GetConfigValueTool.h"

#include "Misc/ConfigCacheIni.h"
#include "SoftUEBridgeModule.h"
#include "Tools/BridgeToolRegistry.h"

REGISTER_BRIDGE_TOOL(UGetConfigValueTool)

namespace
{
bool ParseConfigEntry(const FString& Entry, FString& OutKey, FString& OutValue)
{
	FString Working = Entry.TrimStartAndEnd();
	if (Working.IsEmpty())
	{
		return false;
	}

	if (Working.StartsWith(TEXT("+")) || Working.StartsWith(TEXT("-")) || Working.StartsWith(TEXT(".")) || Working.StartsWith(TEXT("!")))
	{
		Working = Working.Mid(1);
	}

	if (!Working.Split(TEXT("="), &OutKey, &OutValue))
	{
		OutKey = Working.TrimStartAndEnd();
		OutValue.Reset();
		return !OutKey.IsEmpty();
	}

	OutKey = OutKey.TrimStartAndEnd();
	OutValue = OutValue.TrimStartAndEnd();
	return !OutKey.IsEmpty();
}
}

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

	if (!GConfig)
	{
		return FBridgeToolResult::Error(TEXT("GConfig is not available"));
	}

	FString Value;
	bool bHasSection = false;
	if (!TryGetConfigValue(Section, Key, Filename, Value, &bHasSection))
	{
		if (!bHasSection)
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

bool UGetConfigValueTool::TryGetConfigValue(const FString& Section, const FString& Key, const FString& Filename, FString& OutValue, bool* bOutHasSection)
{
	if (bOutHasSection)
	{
		*bOutHasSection = false;
	}

	if (!GConfig)
	{
		return false;
	}

	if (GConfig->GetString(*Section, *Key, OutValue, Filename))
	{
		if (bOutHasSection)
		{
			*bOutHasSection = true;
		}
		return true;
	}

	TArray<FString> SectionEntries;
	if (!GConfig->GetSection(*Section, SectionEntries, Filename))
	{
		return false;
	}

	if (bOutHasSection)
	{
		*bOutHasSection = true;
	}

	bool bFoundKey = false;
	for (const FString& Entry : SectionEntries)
	{
		FString EntryKey;
		FString EntryValue;
		if (!ParseConfigEntry(Entry, EntryKey, EntryValue))
		{
			continue;
		}
		if (!EntryKey.Equals(Key, ESearchCase::CaseSensitive))
		{
			continue;
		}
		OutValue = EntryValue;
		bFoundKey = true;
	}

	return bFoundKey;
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
