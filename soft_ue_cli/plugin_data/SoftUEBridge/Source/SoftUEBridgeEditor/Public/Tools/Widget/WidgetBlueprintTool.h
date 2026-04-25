// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "WidgetBlueprintTool.generated.h"

class UWidget;
class UWidgetBlueprint;

/**
 * Tool for inspecting Widget Blueprint-specific data including
 * widget hierarchy, slot information, and visibility settings.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UWidgetBlueprintTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("inspect-widget-blueprint"); }
	virtual FString GetToolDescription() const override
	{
		return TEXT("Inspect Widget Blueprint-specific data including widget hierarchy from WidgetTree, "
			"slot information (anchors, offsets, sizes), visibility settings, named slots, property bindings, "
			"and animations. Works only with Widget Blueprints (UserWidget subclasses).");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path") }; }

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Build widget hierarchy recursively */
	TSharedPtr<FJsonObject> BuildWidgetNode(
		UWidget* Widget,
		int32 CurrentDepth,
		int32 MaxDepth,
		bool bIncludeDefaults);

	/** Extract common widget properties */
	TSharedPtr<FJsonObject> ExtractWidgetProperties(UWidget* Widget, bool bIncludeDefaults);

	/** Extract property bindings from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractBindings(UWidgetBlueprint* WidgetBP);

	/** Extract animations from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractAnimations(UWidgetBlueprint* WidgetBP);

	/** Collect all widget names for flat listing */
	void CollectWidgetNames(UWidget* Widget, TArray<FString>& OutNames);
};
