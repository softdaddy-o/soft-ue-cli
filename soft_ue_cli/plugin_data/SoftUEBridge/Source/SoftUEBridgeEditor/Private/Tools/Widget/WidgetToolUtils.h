// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Types/SlateEnums.h"
#include "Layout/Margin.h"

class UPanelSlot;
class UCanvasPanelSlot;
class UOverlaySlot;
class UGridSlot;

/**
 * Shared utilities for widget inspection tools.
 * Used by both WidgetBlueprintTool and InspectRuntimeWidgetsTool.
 */
namespace WidgetToolUtils
{
	/** Extract slot information for a widget */
	TSharedPtr<FJsonObject> ExtractSlotInfo(UPanelSlot* Slot);

	/** Extract CanvasPanelSlot-specific properties */
	TSharedPtr<FJsonObject> ExtractCanvasSlotInfo(UCanvasPanelSlot* CanvasSlot);

	/** Extract OverlaySlot properties */
	TSharedPtr<FJsonObject> ExtractOverlaySlotInfo(UOverlaySlot* OverlaySlot);

	/** Extract GridSlot properties */
	TSharedPtr<FJsonObject> ExtractGridSlotInfo(UGridSlot* GridSlot);

	/** Get visibility as string */
	FString VisibilityToString(ESlateVisibility Visibility);

	/** Get horizontal alignment as string */
	FString HAlignToString(EHorizontalAlignment Align);

	/** Get vertical alignment as string */
	FString VAlignToString(EVerticalAlignment Align);

	/** Build a JSON object from an FMargin (padding) */
	TSharedPtr<FJsonObject> MarginToJson(const FMargin& Margin);

	/** Build a JSON array [X, Y] from an FVector2D */
	TArray<TSharedPtr<FJsonValue>> Vector2dToJsonArray(const FVector2D& Vec);
}
