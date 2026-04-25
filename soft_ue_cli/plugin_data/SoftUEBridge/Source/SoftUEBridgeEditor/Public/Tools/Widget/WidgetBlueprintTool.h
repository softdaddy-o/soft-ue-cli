// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "WidgetBlueprintTool.generated.h"

class UWidget;
class UPanelSlot;
class UWidgetTree;
class UWidgetBlueprint;
class UCanvasPanelSlot;
class UOverlaySlot;
class UGridSlot;
class UWidgetAnimation;

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

	/** Extract slot information for a widget */
	TSharedPtr<FJsonObject> ExtractSlotInfo(UPanelSlot* Slot);

	/** Extract CanvasPanelSlot-specific properties */
	TSharedPtr<FJsonObject> ExtractCanvasSlotInfo(UCanvasPanelSlot* CanvasSlot);

	/** Extract OverlaySlot properties */
	TSharedPtr<FJsonObject> ExtractOverlaySlotInfo(UOverlaySlot* OverlaySlot);

	/** Extract GridSlot properties */
	TSharedPtr<FJsonObject> ExtractGridSlotInfo(UGridSlot* GridSlot);

	/** Extract common widget properties */
	TSharedPtr<FJsonObject> ExtractWidgetProperties(UWidget* Widget, bool bIncludeDefaults);

	/** Extract property bindings from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractBindings(UWidgetBlueprint* WidgetBP);

	/** Extract animations from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractAnimations(UWidgetBlueprint* WidgetBP);

	/** Get visibility as string */
	static FString VisibilityToString(ESlateVisibility Visibility);

	/** Get horizontal alignment as string */
	static FString HAlignToString(EHorizontalAlignment Align);

	/** Get vertical alignment as string */
	static FString VAlignToString(EVerticalAlignment Align);

	/** Collect all widget names for flat listing */
	void CollectWidgetNames(UWidget* Widget, TArray<FString>& OutNames);
};
