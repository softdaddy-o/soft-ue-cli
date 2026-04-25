// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/WidgetToolUtils.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/GridSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/WrapBoxSlot.h"

TSharedPtr<FJsonObject> WidgetToolUtils::MarginToJson(const FMargin& Margin)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetNumberField(TEXT("left"), Margin.Left);
	Obj->SetNumberField(TEXT("top"), Margin.Top);
	Obj->SetNumberField(TEXT("right"), Margin.Right);
	Obj->SetNumberField(TEXT("bottom"), Margin.Bottom);
	return Obj;
}

TSharedPtr<FJsonObject> WidgetToolUtils::ExtractSlotInfo(UPanelSlot* Slot)
{
	if (!Slot)
	{
		return nullptr;
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		return ExtractCanvasSlotInfo(CanvasSlot);
	}

	if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("VerticalBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(VBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(VBoxSlot->GetVerticalAlignment()));

		FSlateChildSize Size = VBoxSlot->GetSize();
		SlotObj->SetStringField(TEXT("size_rule"),
			Size.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotObj->SetNumberField(TEXT("size_value"), Size.Value);

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(VBoxSlot->GetPadding()));

		return SlotObj;
	}

	if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("HorizontalBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(HBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(HBoxSlot->GetVerticalAlignment()));

		FSlateChildSize Size = HBoxSlot->GetSize();
		SlotObj->SetStringField(TEXT("size_rule"),
			Size.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
		SlotObj->SetNumberField(TEXT("size_value"), Size.Value);

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(HBoxSlot->GetPadding()));

		return SlotObj;
	}

	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		return ExtractOverlaySlotInfo(OverlaySlot);
	}

	if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
	{
		return ExtractGridSlotInfo(GridSlot);
	}

	if (UUniformGridSlot* UniformGridSlot = Cast<UUniformGridSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("UniformGridSlot"));
		SlotObj->SetNumberField(TEXT("row"), UniformGridSlot->GetRow());
		SlotObj->SetNumberField(TEXT("column"), UniformGridSlot->GetColumn());
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(UniformGridSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(UniformGridSlot->GetVerticalAlignment()));
		return SlotObj;
	}

	if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("BorderSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(BorderSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(BorderSlot->GetVerticalAlignment()));

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(BorderSlot->GetPadding()));

		return SlotObj;
	}

	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("ButtonSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(ButtonSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(ButtonSlot->GetVerticalAlignment()));

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(ButtonSlot->GetPadding()));

		return SlotObj;
	}

	if (USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("SizeBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(SizeBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(SizeBoxSlot->GetVerticalAlignment()));

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(SizeBoxSlot->GetPadding()));

		return SlotObj;
	}

	if (UScaleBoxSlot* ScaleBoxSlot = Cast<UScaleBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("ScaleBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(ScaleBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(ScaleBoxSlot->GetVerticalAlignment()));

		// Note: UScaleBoxSlot doesn't expose GetPadding() publicly in UE 5.6-5.7

		return SlotObj;
	}

	if (UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("ScrollBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(ScrollBoxSlot->GetHorizontalAlignment()));

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(ScrollBoxSlot->GetPadding()));

		return SlotObj;
	}

	if (UWrapBoxSlot* WrapBoxSlot = Cast<UWrapBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("WrapBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(WrapBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(WrapBoxSlot->GetVerticalAlignment()));

		return SlotObj;
	}

	if (UWidgetSwitcherSlot* SwitcherSlot = Cast<UWidgetSwitcherSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("WidgetSwitcherSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(SwitcherSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(SwitcherSlot->GetVerticalAlignment()));

		SlotObj->SetObjectField(TEXT("padding"), MarginToJson(SwitcherSlot->GetPadding()));

		return SlotObj;
	}

	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), Slot->GetClass()->GetName());
	return SlotObj;
}

TSharedPtr<FJsonObject> WidgetToolUtils::ExtractCanvasSlotInfo(UCanvasPanelSlot* CanvasSlot)
{
	if (!CanvasSlot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), TEXT("CanvasPanelSlot"));

	// Anchors
	FAnchorData LayoutData = CanvasSlot->GetLayout();
	TSharedPtr<FJsonObject> AnchorsObj = MakeShareable(new FJsonObject);
	AnchorsObj->SetArrayField(TEXT("min"), Vector2dToJsonArray(FVector2D(LayoutData.Anchors.Minimum)));
	AnchorsObj->SetArrayField(TEXT("max"), Vector2dToJsonArray(FVector2D(LayoutData.Anchors.Maximum)));
	SlotObj->SetObjectField(TEXT("anchors"), AnchorsObj);

	// Offsets
	SlotObj->SetObjectField(TEXT("offsets"), MarginToJson(LayoutData.Offsets));

	// Size, Position, Alignment
	SlotObj->SetArrayField(TEXT("size"), Vector2dToJsonArray(CanvasSlot->GetSize()));
	SlotObj->SetArrayField(TEXT("position"), Vector2dToJsonArray(CanvasSlot->GetPosition()));
	SlotObj->SetArrayField(TEXT("alignment"), Vector2dToJsonArray(CanvasSlot->GetAlignment()));

	// Z-Order
	SlotObj->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());

	// Auto size
	SlotObj->SetBoolField(TEXT("auto_size"), CanvasSlot->GetAutoSize());

	return SlotObj;
}

TSharedPtr<FJsonObject> WidgetToolUtils::ExtractOverlaySlotInfo(UOverlaySlot* OverlaySlot)
{
	if (!OverlaySlot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), TEXT("OverlaySlot"));
	SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(OverlaySlot->GetHorizontalAlignment()));
	SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(OverlaySlot->GetVerticalAlignment()));

	SlotObj->SetObjectField(TEXT("padding"), MarginToJson(OverlaySlot->GetPadding()));

	return SlotObj;
}

TSharedPtr<FJsonObject> WidgetToolUtils::ExtractGridSlotInfo(UGridSlot* GridSlot)
{
	if (!GridSlot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), TEXT("GridSlot"));
	SlotObj->SetNumberField(TEXT("row"), GridSlot->GetRow());
	SlotObj->SetNumberField(TEXT("column"), GridSlot->GetColumn());
	SlotObj->SetNumberField(TEXT("row_span"), GridSlot->GetRowSpan());
	SlotObj->SetNumberField(TEXT("column_span"), GridSlot->GetColumnSpan());
	SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(GridSlot->GetHorizontalAlignment()));
	SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(GridSlot->GetVerticalAlignment()));

	SlotObj->SetObjectField(TEXT("padding"), MarginToJson(GridSlot->GetPadding()));

	return SlotObj;
}

FString WidgetToolUtils::VisibilityToString(ESlateVisibility Visibility)
{
	switch (Visibility)
	{
	case ESlateVisibility::Visible:
		return TEXT("Visible");
	case ESlateVisibility::Collapsed:
		return TEXT("Collapsed");
	case ESlateVisibility::Hidden:
		return TEXT("Hidden");
	case ESlateVisibility::HitTestInvisible:
		return TEXT("HitTestInvisible");
	case ESlateVisibility::SelfHitTestInvisible:
		return TEXT("SelfHitTestInvisible");
	default:
		return TEXT("Unknown");
	}
}

FString WidgetToolUtils::HAlignToString(EHorizontalAlignment Align)
{
	switch (Align)
	{
	case HAlign_Fill:
		return TEXT("Fill");
	case HAlign_Left:
		return TEXT("Left");
	case HAlign_Center:
		return TEXT("Center");
	case HAlign_Right:
		return TEXT("Right");
	default:
		return TEXT("Unknown");
	}
}

FString WidgetToolUtils::VAlignToString(EVerticalAlignment Align)
{
	switch (Align)
	{
	case VAlign_Fill:
		return TEXT("Fill");
	case VAlign_Top:
		return TEXT("Top");
	case VAlign_Center:
		return TEXT("Center");
	case VAlign_Bottom:
		return TEXT("Bottom");
	default:
		return TEXT("Unknown");
	}
}

TArray<TSharedPtr<FJsonValue>> WidgetToolUtils::Vector2dToJsonArray(const FVector2D& Vec)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Add(MakeShareable(new FJsonValueNumber(Vec.X)));
	Arr.Add(MakeShareable(new FJsonValueNumber(Vec.Y)));
	return Arr;
}
