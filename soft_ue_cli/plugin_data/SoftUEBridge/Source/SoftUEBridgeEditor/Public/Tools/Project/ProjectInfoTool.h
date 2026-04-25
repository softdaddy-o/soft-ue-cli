// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "ProjectInfoTool.generated.h"

/**
 * Tool for retrieving project and plugin information.
 * Returns project name, path, plugin version, and optionally project settings.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UProjectInfoTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-project-info"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Get input mappings (actions and axes) */
	TSharedPtr<FJsonObject> GetInputMappings() const;

	/** Get collision settings */
	TSharedPtr<FJsonObject> GetCollisionSettings() const;

	/** Get gameplay tags */
	TSharedPtr<FJsonObject> GetGameplayTags() const;

	/** Get default maps and game modes */
	TSharedPtr<FJsonObject> GetDefaultMapsAndModes() const;
};
