// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Widget/WidgetBlueprintTool.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
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
#include "Components/NamedSlot.h"
#include "Binding/DynamicPropertyPath.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "SoftUEBridgeEditorModule.h"

TMap<FString, FBridgeSchemaProperty> UWidgetBlueprintTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Widget Blueprint (e.g., /Game/UI/WBP_MainMenu)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty IncludeDefaults;
	IncludeDefaults.Type = TEXT("boolean");
	IncludeDefaults.Description = TEXT("Include widget property default values (default: false)");
	IncludeDefaults.bRequired = false;
	Schema.Add(TEXT("include_defaults"), IncludeDefaults);

	FBridgeSchemaProperty DepthLimit;
	DepthLimit.Type = TEXT("integer");
	DepthLimit.Description = TEXT("Maximum hierarchy depth to traverse (-1 for unlimited, default: -1)");
	DepthLimit.bRequired = false;
	Schema.Add(TEXT("depth_limit"), DepthLimit);

	FBridgeSchemaProperty IncludeBindings;
	IncludeBindings.Type = TEXT("boolean");
	IncludeBindings.Description = TEXT("Include property binding information (default: true)");
	IncludeBindings.bRequired = false;
	Schema.Add(TEXT("include_bindings"), IncludeBindings);

	return Schema;
}

FBridgeToolResult UWidgetBlueprintTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Get required parameter
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	// Get optional parameters
	bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), false);
	int32 DepthLimit = GetIntArgOrDefault(Arguments, TEXT("depth_limit"), -1);
	bool bIncludeBindings = GetBoolArgOrDefault(Arguments, TEXT("include_bindings"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("inspect-widget-blueprint: path='%s', depth_limit=%d"),
		*AssetPath, DepthLimit);

	// Load the Widget Blueprint
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		// Try loading as regular Blueprint first to give better error
		UBlueprint* RegularBP = LoadObject<UBlueprint>(nullptr, *AssetPath);
		if (RegularBP)
		{
			UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("inspect-widget-blueprint: '%s' is not a Widget Blueprint"), *AssetPath);
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Asset '%s' is a Blueprint but not a Widget Blueprint. Use analyze-blueprint instead."),
				*AssetPath));
		}
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("inspect-widget-blueprint: Failed to load '%s'"), *AssetPath);
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *AssetPath));
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("widget_class"), WidgetBP->GeneratedClass ?
		WidgetBP->GeneratedClass->GetName() : WidgetBP->GetName() + TEXT("_C"));

	// Parent class
	if (WidgetBP->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), WidgetBP->ParentClass->GetName());
		Result->SetStringField(TEXT("parent_class_path"), WidgetBP->ParentClass->GetPathName());
	}

	// Widget Tree
	UWidgetTree* WidgetTree = WidgetBP->WidgetTree;
	if (!WidgetTree)
	{
		Result->SetObjectField(TEXT("root_widget"), nullptr);
		Result->SetArrayField(TEXT("all_widgets"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetNumberField(TEXT("widget_count"), 0);
		return FBridgeToolResult::Json(Result);
	}

	// Root widget
	UWidget* RootWidget = WidgetTree->RootWidget;
	if (RootWidget)
	{
		int32 MaxDepth = DepthLimit < 0 ? INT32_MAX : DepthLimit;
		TSharedPtr<FJsonObject> RootNode = BuildWidgetNode(RootWidget, 0, MaxDepth, bIncludeDefaults);
		Result->SetObjectField(TEXT("root_widget"), RootNode);
	}
	else
	{
		Result->SetObjectField(TEXT("root_widget"), nullptr);
	}

	// Flat list of all widget names
	TArray<FString> AllWidgetNames;
	if (RootWidget)
	{
		CollectWidgetNames(RootWidget, AllWidgetNames);
	}

	TArray<TSharedPtr<FJsonValue>> NamesArray;
	for (const FString& Name : AllWidgetNames)
	{
		NamesArray.Add(MakeShareable(new FJsonValueString(Name)));
	}
	Result->SetArrayField(TEXT("all_widgets"), NamesArray);
	Result->SetNumberField(TEXT("widget_count"), AllWidgetNames.Num());

	// Named slots (for UserWidgets that define named slots)
	TArray<TSharedPtr<FJsonValue>> NamedSlotsArray;
	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);
	for (UWidget* Widget : AllWidgets)
	{
		if (UNamedSlot* NamedSlot = Cast<UNamedSlot>(Widget))
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
			SlotObj->SetStringField(TEXT("name"), NamedSlot->GetName());
			if (NamedSlot->GetContent())
			{
				SlotObj->SetStringField(TEXT("content"), NamedSlot->GetContent()->GetName());
			}
			NamedSlotsArray.Add(MakeShareable(new FJsonValueObject(SlotObj)));
		}
	}
	if (NamedSlotsArray.Num() > 0)
	{
		Result->SetArrayField(TEXT("named_slots"), NamedSlotsArray);
	}

	// Property bindings
	if (bIncludeBindings)
	{
		TArray<TSharedPtr<FJsonValue>> BindingsArray = ExtractBindings(WidgetBP);
		if (BindingsArray.Num() > 0)
		{
			Result->SetArrayField(TEXT("bindings"), BindingsArray);
		}
	}

	// Animations
	TArray<TSharedPtr<FJsonValue>> AnimationsArray = ExtractAnimations(WidgetBP);
	if (AnimationsArray.Num() > 0)
	{
		Result->SetArrayField(TEXT("animations"), AnimationsArray);
	}

	return FBridgeToolResult::Json(Result);
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::BuildWidgetNode(
	UWidget* Widget,
	int32 CurrentDepth,
	int32 MaxDepth,
	bool bIncludeDefaults)
{
	if (!Widget)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);

	// Basic info
	NodeObj->SetStringField(TEXT("name"), Widget->GetName());
	NodeObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());

	// Visibility
	NodeObj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));

	// Is variable (exposed to Blueprint)
	NodeObj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

	// Slot information (if widget is in a panel)
	if (UPanelSlot* Slot = Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotInfo = ExtractSlotInfo(Slot);
		if (SlotInfo.IsValid())
		{
			NodeObj->SetObjectField(TEXT("slot"), SlotInfo);
		}
	}

	// Widget-specific properties
	if (bIncludeDefaults)
	{
		TSharedPtr<FJsonObject> PropsObj = ExtractWidgetProperties(Widget, bIncludeDefaults);
		if (PropsObj.IsValid() && PropsObj->Values.Num() > 0)
		{
			NodeObj->SetObjectField(TEXT("properties"), PropsObj);
		}
	}

	// Children (if panel widget and within depth limit)
	if (CurrentDepth < MaxDepth)
	{
		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArray;
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); i++)
			{
				UWidget* Child = PanelWidget->GetChildAt(i);
				if (Child)
				{
					TSharedPtr<FJsonObject> ChildNode = BuildWidgetNode(
						Child, CurrentDepth + 1, MaxDepth, bIncludeDefaults);
					if (ChildNode.IsValid())
					{
						ChildrenArray.Add(MakeShareable(new FJsonValueObject(ChildNode)));
					}
				}
			}
			if (ChildrenArray.Num() > 0)
			{
				NodeObj->SetArrayField(TEXT("children"), ChildrenArray);
			}
		}
	}

	return NodeObj;
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::ExtractSlotInfo(UPanelSlot* Slot)
{
	if (!Slot)
	{
		return nullptr;
	}

	// Try specific slot types
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

		FMargin Padding = VBoxSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

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

		FMargin Padding = HBoxSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

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

		FMargin Padding = BorderSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

		return SlotObj;
	}

	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("ButtonSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(ButtonSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(ButtonSlot->GetVerticalAlignment()));

		FMargin Padding = ButtonSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

		return SlotObj;
	}

	if (USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(Slot))
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("type"), TEXT("SizeBoxSlot"));
		SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(SizeBoxSlot->GetHorizontalAlignment()));
		SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(SizeBoxSlot->GetVerticalAlignment()));

		FMargin Padding = SizeBoxSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

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

		FMargin Padding = ScrollBoxSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

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

		FMargin Padding = SwitcherSlot->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
		PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
		PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
		PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
		SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

		return SlotObj;
	}

	// Fallback for unknown slot types
	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), Slot->GetClass()->GetName());
	return SlotObj;
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::ExtractCanvasSlotInfo(UCanvasPanelSlot* CanvasSlot)
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

	TArray<TSharedPtr<FJsonValue>> MinArray;
	MinArray.Add(MakeShareable(new FJsonValueNumber(LayoutData.Anchors.Minimum.X)));
	MinArray.Add(MakeShareable(new FJsonValueNumber(LayoutData.Anchors.Minimum.Y)));
	AnchorsObj->SetArrayField(TEXT("min"), MinArray);

	TArray<TSharedPtr<FJsonValue>> MaxArray;
	MaxArray.Add(MakeShareable(new FJsonValueNumber(LayoutData.Anchors.Maximum.X)));
	MaxArray.Add(MakeShareable(new FJsonValueNumber(LayoutData.Anchors.Maximum.Y)));
	AnchorsObj->SetArrayField(TEXT("max"), MaxArray);

	SlotObj->SetObjectField(TEXT("anchors"), AnchorsObj);

	// Offsets
	TSharedPtr<FJsonObject> OffsetsObj = MakeShareable(new FJsonObject);
	OffsetsObj->SetNumberField(TEXT("left"), LayoutData.Offsets.Left);
	OffsetsObj->SetNumberField(TEXT("top"), LayoutData.Offsets.Top);
	OffsetsObj->SetNumberField(TEXT("right"), LayoutData.Offsets.Right);
	OffsetsObj->SetNumberField(TEXT("bottom"), LayoutData.Offsets.Bottom);
	SlotObj->SetObjectField(TEXT("offsets"), OffsetsObj);

	// Size (derived from offsets when not stretching)
	FVector2D Size = CanvasSlot->GetSize();
	TArray<TSharedPtr<FJsonValue>> SizeArray;
	SizeArray.Add(MakeShareable(new FJsonValueNumber(Size.X)));
	SizeArray.Add(MakeShareable(new FJsonValueNumber(Size.Y)));
	SlotObj->SetArrayField(TEXT("size"), SizeArray);

	// Position
	FVector2D Position = CanvasSlot->GetPosition();
	TArray<TSharedPtr<FJsonValue>> PosArray;
	PosArray.Add(MakeShareable(new FJsonValueNumber(Position.X)));
	PosArray.Add(MakeShareable(new FJsonValueNumber(Position.Y)));
	SlotObj->SetArrayField(TEXT("position"), PosArray);

	// Alignment
	FVector2D Alignment = CanvasSlot->GetAlignment();
	TArray<TSharedPtr<FJsonValue>> AlignArray;
	AlignArray.Add(MakeShareable(new FJsonValueNumber(Alignment.X)));
	AlignArray.Add(MakeShareable(new FJsonValueNumber(Alignment.Y)));
	SlotObj->SetArrayField(TEXT("alignment"), AlignArray);

	// Z-Order
	SlotObj->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());

	// Auto size
	SlotObj->SetBoolField(TEXT("auto_size"), CanvasSlot->GetAutoSize());

	return SlotObj;
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::ExtractOverlaySlotInfo(UOverlaySlot* OverlaySlot)
{
	if (!OverlaySlot)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
	SlotObj->SetStringField(TEXT("type"), TEXT("OverlaySlot"));
	SlotObj->SetStringField(TEXT("horizontal_alignment"), HAlignToString(OverlaySlot->GetHorizontalAlignment()));
	SlotObj->SetStringField(TEXT("vertical_alignment"), VAlignToString(OverlaySlot->GetVerticalAlignment()));

	FMargin Padding = OverlaySlot->GetPadding();
	TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
	PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
	PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
	PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
	PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
	SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

	return SlotObj;
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::ExtractGridSlotInfo(UGridSlot* GridSlot)
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

	FMargin Padding = GridSlot->GetPadding();
	TSharedPtr<FJsonObject> PaddingObj = MakeShareable(new FJsonObject);
	PaddingObj->SetNumberField(TEXT("left"), Padding.Left);
	PaddingObj->SetNumberField(TEXT("top"), Padding.Top);
	PaddingObj->SetNumberField(TEXT("right"), Padding.Right);
	PaddingObj->SetNumberField(TEXT("bottom"), Padding.Bottom);
	SlotObj->SetObjectField(TEXT("padding"), PaddingObj);

	return SlotObj;
}

TSharedPtr<FJsonObject> UWidgetBlueprintTool::ExtractWidgetProperties(UWidget* Widget, bool bIncludeDefaults)
{
	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);

	// Render transform
	FWidgetTransform RenderTransform = Widget->GetRenderTransform();
	if (bIncludeDefaults || !RenderTransform.IsIdentity())
	{
		TSharedPtr<FJsonObject> TransformObj = MakeShareable(new FJsonObject);

		TArray<TSharedPtr<FJsonValue>> TranslationArray;
		TranslationArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Translation.X)));
		TranslationArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Translation.Y)));
		TransformObj->SetArrayField(TEXT("translation"), TranslationArray);

		TransformObj->SetNumberField(TEXT("angle"), RenderTransform.Angle);

		TArray<TSharedPtr<FJsonValue>> ScaleArray;
		ScaleArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Scale.X)));
		ScaleArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Scale.Y)));
		TransformObj->SetArrayField(TEXT("scale"), ScaleArray);

		TArray<TSharedPtr<FJsonValue>> ShearArray;
		ShearArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Shear.X)));
		ShearArray.Add(MakeShareable(new FJsonValueNumber(RenderTransform.Shear.Y)));
		TransformObj->SetArrayField(TEXT("shear"), ShearArray);

		PropsObj->SetObjectField(TEXT("render_transform"), TransformObj);
	}

	// Render opacity
	float RenderOpacity = Widget->GetRenderOpacity();
	if (bIncludeDefaults || RenderOpacity != 1.0f)
	{
		PropsObj->SetNumberField(TEXT("render_opacity"), RenderOpacity);
	}

	// Tool tip text
	FText ToolTipText = Widget->GetToolTipText();
	if (!ToolTipText.IsEmpty())
	{
		PropsObj->SetStringField(TEXT("tool_tip"), ToolTipText.ToString());
	}

	// Is enabled
	bool bIsEnabled = Widget->GetIsEnabled();
	if (bIncludeDefaults || !bIsEnabled)
	{
		PropsObj->SetBoolField(TEXT("is_enabled"), bIsEnabled);
	}

	return PropsObj;
}

TArray<TSharedPtr<FJsonValue>> UWidgetBlueprintTool::ExtractBindings(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> BindingsArray;

	if (!WidgetBP)
	{
		return BindingsArray;
	}

	// Get the generated class which contains binding information
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBP->GeneratedClass);
	if (!GeneratedClass)
	{
		return BindingsArray;
	}

	// Extract bindings from the generated class
	for (const FDelegateRuntimeBinding& Binding : GeneratedClass->Bindings)
	{
		TSharedPtr<FJsonObject> BindingObj = MakeShareable(new FJsonObject);
		BindingObj->SetStringField(TEXT("widget"), Binding.ObjectName);
		BindingObj->SetStringField(TEXT("property"), Binding.PropertyName.ToString());

		// Determine binding type
		if (Binding.FunctionName != NAME_None)
		{
			BindingObj->SetStringField(TEXT("binding_type"), TEXT("Function"));
			BindingObj->SetStringField(TEXT("function_name"), Binding.FunctionName.ToString());
		}
		else if (Binding.SourcePath.IsValid())
		{
			BindingObj->SetStringField(TEXT("binding_type"), TEXT("Property"));
			BindingObj->SetStringField(TEXT("source_path"), Binding.SourcePath.ToString());
		}
		else
		{
			BindingObj->SetStringField(TEXT("binding_type"), TEXT("Unknown"));
		}

		// Binding kind
		switch (Binding.Kind)
		{
		case EBindingKind::Function:
			BindingObj->SetStringField(TEXT("kind"), TEXT("Function"));
			break;
		case EBindingKind::Property:
			BindingObj->SetStringField(TEXT("kind"), TEXT("Property"));
			break;
		default:
			BindingObj->SetStringField(TEXT("kind"), TEXT("Unknown"));
			break;
		}

		BindingsArray.Add(MakeShareable(new FJsonValueObject(BindingObj)));
	}

	return BindingsArray;
}

TArray<TSharedPtr<FJsonValue>> UWidgetBlueprintTool::ExtractAnimations(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> AnimationsArray;

	if (!WidgetBP)
	{
		return AnimationsArray;
	}

	// UWidgetBlueprint has Animations property
	for (UWidgetAnimation* Animation : WidgetBP->Animations)
	{
		if (!Animation)
		{
			continue;
		}

		TSharedPtr<FJsonObject> AnimObj = MakeShareable(new FJsonObject);
		AnimObj->SetStringField(TEXT("name"), Animation->GetName());
		AnimObj->SetStringField(TEXT("display_name"), Animation->GetDisplayName().ToString());

		// Get movie scene for duration info
		const UMovieScene* MovieScene = Animation->GetMovieScene();
		if (MovieScene)
		{
			// Duration in frames and seconds
			FFrameRate TickResolution = MovieScene->GetTickResolution();
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();

			TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
			FFrameNumber StartFrame = PlaybackRange.GetLowerBoundValue();
			FFrameNumber EndFrame = PlaybackRange.GetUpperBoundValue();

			double StartSeconds = TickResolution.AsSeconds(StartFrame);
			double EndSeconds = TickResolution.AsSeconds(EndFrame);
			double Duration = EndSeconds - StartSeconds;

			AnimObj->SetNumberField(TEXT("start_time"), StartSeconds);
			AnimObj->SetNumberField(TEXT("end_time"), EndSeconds);
			AnimObj->SetNumberField(TEXT("duration"), Duration);
			AnimObj->SetNumberField(TEXT("display_rate_fps"), DisplayRate.AsDecimal());

			// Track count - count bindings' tracks
			int32 TotalTracks = 0;
			for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
			{
				TotalTracks += Binding.GetTracks().Num();
			}
			AnimObj->SetNumberField(TEXT("track_count"), TotalTracks);

			// Get bound object names (which widgets are animated)
			TArray<TSharedPtr<FJsonValue>> BoundObjectsArray;
			for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
			{
				TSharedPtr<FJsonObject> BindingObj = MakeShareable(new FJsonObject);
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
				// GetName() deprecated in 5.7, use GUID as identifier
				BindingObj->SetStringField(TEXT("guid"), Binding.GetObjectGuid().ToString());
#else
				BindingObj->SetStringField(TEXT("name"), Binding.GetName());
#endif
				BindingObj->SetNumberField(TEXT("track_count"), Binding.GetTracks().Num());

				// Get track types for this binding
				TArray<TSharedPtr<FJsonValue>> TracksArray;
				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (Track)
					{
						TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject);
						TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
						TrackObj->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());
						TracksArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
					}
				}
				if (TracksArray.Num() > 0)
				{
					BindingObj->SetArrayField(TEXT("tracks"), TracksArray);
				}

				BoundObjectsArray.Add(MakeShareable(new FJsonValueObject(BindingObj)));
			}
			if (BoundObjectsArray.Num() > 0)
			{
				AnimObj->SetArrayField(TEXT("bound_objects"), BoundObjectsArray);
			}
		}

		AnimationsArray.Add(MakeShareable(new FJsonValueObject(AnimObj)));
	}

	return AnimationsArray;
}

void UWidgetBlueprintTool::CollectWidgetNames(UWidget* Widget, TArray<FString>& OutNames)
{
	if (!Widget)
	{
		return;
	}

	OutNames.Add(Widget->GetName());

	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); i++)
		{
			CollectWidgetNames(PanelWidget->GetChildAt(i), OutNames);
		}
	}
}

FString UWidgetBlueprintTool::VisibilityToString(ESlateVisibility Visibility)
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

FString UWidgetBlueprintTool::HAlignToString(EHorizontalAlignment Align)
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

FString UWidgetBlueprintTool::VAlignToString(EVerticalAlignment Align)
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
