// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Project/ProjectInfoTool.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "GameFramework/InputSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Engine/CollisionProfile.h"
#include "GameplayTagsSettings.h"
#include "GameMapsSettings.h"
#include "SoftUEBridgeEditorModule.h"
#include "Subsystem/SoftUEBridgeSubsystem.h"

FString UProjectInfoTool::GetToolDescription() const
{
	return TEXT("Get project and plugin information including project name, path, and plugin version. "
		"Optionally include project settings (input mappings, collision, gameplay tags, maps).");
}

TMap<FString, FBridgeSchemaProperty> UProjectInfoTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Section;
	Section.Type = TEXT("string");
	Section.Description = TEXT("Include settings section: 'input', 'collision', 'tags', 'maps', or 'all'. Omit for basic info only.");
	Section.bRequired = false;
	Schema.Add(TEXT("section"), Section);

	return Schema;
}

TArray<FString> UProjectInfoTool::GetRequiredParams() const
{
	// No required parameters
	return TArray<FString>();
}

FBridgeToolResult UProjectInfoTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString Section = GetStringArgOrDefault(Arguments, TEXT("section"), TEXT(""));
	Section = Section.ToLower();

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("get-project-info: section='%s'"), *Section);

	// Validate section if provided
	if (!Section.IsEmpty() &&
		Section != TEXT("all") &&
		Section != TEXT("input") &&
		Section != TEXT("collision") &&
		Section != TEXT("tags") &&
		Section != TEXT("maps"))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid section '%s'. Must be 'input', 'collision', 'tags', 'maps', or 'all'"), *Section));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Project information (always included)
	FString ProjectName = FApp::GetProjectName();
	FString ProjectPath = FPaths::GetProjectFilePath();
	FString ProjectDir = FPaths::ProjectDir();

	Result->SetStringField(TEXT("project_name"), ProjectName);
	Result->SetStringField(TEXT("project_path"), ProjectPath);
	Result->SetStringField(TEXT("project_directory"), ProjectDir);

	// Engine information
	Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

	// Plugin information
	TSharedPtr<FJsonObject> PluginInfo = MakeShareable(new FJsonObject);
	PluginInfo->SetStringField(TEXT("name"), TEXT("SoftUEBridge"));
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SoftUEBridge"));
	if (Plugin.IsValid())
	{
		PluginInfo->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
	}
	Result->SetObjectField(TEXT("plugin"), PluginInfo);

	// Bridge Server information
	TSharedPtr<FJsonObject> ServerInfo = MakeShareable(new FJsonObject);
	USoftUEBridgeSubsystem* BridgeSubsystem = GEngine ? GEngine->GetEngineSubsystem<USoftUEBridgeSubsystem>() : nullptr;
	if (BridgeSubsystem)
	{
		ServerInfo->SetBoolField(TEXT("running"), BridgeSubsystem->IsServerRunning());
		if (BridgeSubsystem->IsServerRunning())
		{
			ServerInfo->SetNumberField(TEXT("port"), BridgeSubsystem->GetServerPort());
		}
	}
	Result->SetObjectField(TEXT("bridge_server"), ServerInfo);

	// Add settings sections if requested
	if (!Section.IsEmpty())
	{
		TSharedPtr<FJsonObject> SettingsJson = MakeShareable(new FJsonObject);

		if (Section == TEXT("all") || Section == TEXT("input"))
		{
			SettingsJson->SetObjectField(TEXT("input"), GetInputMappings());
		}

		if (Section == TEXT("all") || Section == TEXT("collision"))
		{
			SettingsJson->SetObjectField(TEXT("collision"), GetCollisionSettings());
		}

		if (Section == TEXT("all") || Section == TEXT("tags"))
		{
			SettingsJson->SetObjectField(TEXT("tags"), GetGameplayTags());
		}

		if (Section == TEXT("all") || Section == TEXT("maps"))
		{
			SettingsJson->SetObjectField(TEXT("maps"), GetDefaultMapsAndModes());
		}

		Result->SetObjectField(TEXT("settings"), SettingsJson);
	}

	return FBridgeToolResult::Json(Result);
}

TSharedPtr<FJsonObject> UProjectInfoTool::GetInputMappings() const
{
	TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);

	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	if (!InputSettings)
	{
		return InputJson;
	}

	// Action mappings
	TArray<TSharedPtr<FJsonValue>> ActionMappingsArray;
	for (const FInputActionKeyMapping& ActionMapping : InputSettings->GetActionMappings())
	{
		TSharedPtr<FJsonObject> MappingJson = MakeShareable(new FJsonObject);
		MappingJson->SetStringField(TEXT("action_name"), ActionMapping.ActionName.ToString());
		MappingJson->SetStringField(TEXT("key"), ActionMapping.Key.ToString());
		MappingJson->SetBoolField(TEXT("shift"), ActionMapping.bShift);
		MappingJson->SetBoolField(TEXT("ctrl"), ActionMapping.bCtrl);
		MappingJson->SetBoolField(TEXT("alt"), ActionMapping.bAlt);
		MappingJson->SetBoolField(TEXT("cmd"), ActionMapping.bCmd);

		ActionMappingsArray.Add(MakeShareable(new FJsonValueObject(MappingJson)));
	}
	InputJson->SetArrayField(TEXT("action_mappings"), ActionMappingsArray);

	// Axis mappings
	TArray<TSharedPtr<FJsonValue>> AxisMappingsArray;
	for (const FInputAxisKeyMapping& AxisMapping : InputSettings->GetAxisMappings())
	{
		TSharedPtr<FJsonObject> MappingJson = MakeShareable(new FJsonObject);
		MappingJson->SetStringField(TEXT("axis_name"), AxisMapping.AxisName.ToString());
		MappingJson->SetStringField(TEXT("key"), AxisMapping.Key.ToString());
		MappingJson->SetNumberField(TEXT("scale"), AxisMapping.Scale);

		AxisMappingsArray.Add(MakeShareable(new FJsonValueObject(MappingJson)));
	}
	InputJson->SetArrayField(TEXT("axis_mappings"), AxisMappingsArray);

	return InputJson;
}

TSharedPtr<FJsonObject> UProjectInfoTool::GetCollisionSettings() const
{
	TSharedPtr<FJsonObject> CollisionJson = MakeShareable(new FJsonObject);

	const UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
	if (!CollisionProfile)
	{
		return CollisionJson;
	}

	// Collision profiles
	TArray<TSharedPtr<FJsonValue>> ProfilesArray;

	// Use const_cast to access GetProfileTemplates (it's not const but should be)
	UCollisionProfile* MutableProfile = const_cast<UCollisionProfile*>(CollisionProfile);
	for (int32 i = 0; i < MutableProfile->GetNumOfProfiles(); i++)
	{
		const FCollisionResponseTemplate* ProfileTemplate = MutableProfile->GetProfileByIndex(i);
		if (ProfileTemplate)
		{
			TSharedPtr<FJsonObject> ProfileJson = MakeShareable(new FJsonObject);
			ProfileJson->SetStringField(TEXT("name"), ProfileTemplate->Name.ToString());
			ProfileJson->SetStringField(TEXT("object_type"), UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(ProfileTemplate->ObjectType).ToString());
			ProfileJson->SetBoolField(TEXT("can_modify"), ProfileTemplate->bCanModify);

			ProfilesArray.Add(MakeShareable(new FJsonValueObject(ProfileJson)));
		}
	}
	CollisionJson->SetArrayField(TEXT("profiles"), ProfilesArray);

	// Collision channels
	const UPhysicsSettings* PhysicsSettings = GetDefault<UPhysicsSettings>();
	if (PhysicsSettings)
	{
		// Add standard channels
		TArray<TSharedPtr<FJsonValue>> ChannelsArray;

		TArray<FString> StandardChannels = {
			TEXT("WorldStatic"),
			TEXT("WorldDynamic"),
			TEXT("Pawn"),
			TEXT("Visibility"),
			TEXT("Camera"),
			TEXT("PhysicsBody"),
			TEXT("Vehicle"),
			TEXT("Destructible")
		};

		for (const FString& Channel : StandardChannels)
		{
			TSharedPtr<FJsonObject> ChannelJson = MakeShareable(new FJsonObject);
			ChannelJson->SetStringField(TEXT("name"), Channel);
			ChannelJson->SetStringField(TEXT("type"), TEXT("standard"));
			ChannelsArray.Add(MakeShareable(new FJsonValueObject(ChannelJson)));
		}

		CollisionJson->SetArrayField(TEXT("channels"), ChannelsArray);
	}

	return CollisionJson;
}

TSharedPtr<FJsonObject> UProjectInfoTool::GetGameplayTags() const
{
	TSharedPtr<FJsonObject> TagsJson = MakeShareable(new FJsonObject);

	const UGameplayTagsSettings* TagsSettings = GetDefault<UGameplayTagsSettings>();
	if (!TagsSettings)
	{
		return TagsJson;
	}

	// Tag sources
	TArray<TSharedPtr<FJsonValue>> SourcesArray;
	for (const FSoftObjectPath& SourcePath : TagsSettings->GameplayTagTableList)
	{
		TSharedPtr<FJsonObject> SourceJson = MakeShareable(new FJsonObject);
		SourceJson->SetStringField(TEXT("source_path"), SourcePath.ToString());

		SourcesArray.Add(MakeShareable(new FJsonValueObject(SourceJson)));
	}
	TagsJson->SetArrayField(TEXT("tag_sources"), SourcesArray);

	// Common tags
	TArray<TSharedPtr<FJsonValue>> CommonTagsArray;
	for (const FName& CommonTag : TagsSettings->CommonlyReplicatedTags)
	{
		CommonTagsArray.Add(MakeShareable(new FJsonValueString(CommonTag.ToString())));
	}
	if (CommonTagsArray.Num() > 0)
	{
		TagsJson->SetArrayField(TEXT("commonly_replicated_tags"), CommonTagsArray);
	}

	// Tag settings
	TagsJson->SetBoolField(TEXT("import_tags_from_config"), TagsSettings->ImportTagsFromConfig);
	TagsJson->SetBoolField(TEXT("warn_on_invalid_tags"), TagsSettings->WarnOnInvalidTags);
	TagsJson->SetBoolField(TEXT("fast_replication"), TagsSettings->FastReplication);

	return TagsJson;
}

TSharedPtr<FJsonObject> UProjectInfoTool::GetDefaultMapsAndModes() const
{
	TSharedPtr<FJsonObject> MapsJson = MakeShareable(new FJsonObject);

	const UGameMapsSettings* MapsSettings = GetDefault<UGameMapsSettings>();
	if (!MapsSettings)
	{
		return MapsJson;
	}

	// Default maps
	MapsJson->SetStringField(TEXT("editor_startup_map"), MapsSettings->EditorStartupMap.GetLongPackageName());
	MapsJson->SetStringField(TEXT("game_default_map"), MapsSettings->GetGameDefaultMap());

	return MapsJson;
}
