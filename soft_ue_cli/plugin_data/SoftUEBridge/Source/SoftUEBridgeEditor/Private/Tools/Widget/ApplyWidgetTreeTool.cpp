// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/ApplyWidgetTreeTool.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "SoftUEBridgeEditorModule.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"
#include "WidgetBlueprint.h"

namespace
{
	static bool TryGetJsonString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, FString& OutValue)
	{
		return Obj.IsValid() && Obj->TryGetStringField(Field, OutValue);
	}

	static bool TryGetJsonNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double& OutValue)
	{
		return Obj.IsValid() && Obj->TryGetNumberField(Field, OutValue);
	}

	static bool TryGetJsonBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, bool& OutValue)
	{
		return Obj.IsValid() && Obj->TryGetBoolField(Field, OutValue);
	}

	static bool TryReadVector2(
		const TSharedPtr<FJsonObject>& Obj,
		const TCHAR* Field,
		FVector2D& OutValue,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, Array))
		{
			return false;
		}
		if (!Array || Array->Num() != 2)
		{
			OutError = FString::Printf(TEXT("%s must be an array [X, Y]"), Field);
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		if (!(*Array)[0]->TryGetNumber(X) || !(*Array)[1]->TryGetNumber(Y))
		{
			OutError = FString::Printf(TEXT("%s must contain numeric values"), Field);
			return false;
		}

		OutValue = FVector2D(X, Y);
		return true;
	}

	static bool TryReadMarginFromValue(const TSharedPtr<FJsonValue>& Value, FMargin& OutMargin, FString& OutError)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		double Number = 0.0;
		if (Value->TryGetNumber(Number))
		{
			OutMargin = FMargin(Number);
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array))
		{
			if (!Array || (Array->Num() != 2 && Array->Num() != 4))
			{
				OutError = TEXT("margin array must be [horizontal, vertical] or [left, top, right, bottom]");
				return false;
			}

			double A = 0.0;
			double B = 0.0;
			if (!(*Array)[0]->TryGetNumber(A) || !(*Array)[1]->TryGetNumber(B))
			{
				OutError = TEXT("margin array values must be numeric");
				return false;
			}

			if (Array->Num() == 2)
			{
				OutMargin = FMargin(A, B);
				return true;
			}

			double C = 0.0;
			double D = 0.0;
			if (!(*Array)[2]->TryGetNumber(C) || !(*Array)[3]->TryGetNumber(D))
			{
				OutError = TEXT("margin array values must be numeric");
				return false;
			}

			OutMargin = FMargin(A, B, C, D);
			return true;
		}

		const TSharedPtr<FJsonObject>* MarginObj = nullptr;
		if (Value->TryGetObject(MarginObj) && MarginObj && MarginObj->IsValid())
		{
			double Left = 0.0;
			double Top = 0.0;
			double Right = 0.0;
			double Bottom = 0.0;
			(*MarginObj)->TryGetNumberField(TEXT("left"), Left);
			(*MarginObj)->TryGetNumberField(TEXT("top"), Top);
			(*MarginObj)->TryGetNumberField(TEXT("right"), Right);
			(*MarginObj)->TryGetNumberField(TEXT("bottom"), Bottom);
			OutMargin = FMargin(Left, Top, Right, Bottom);
			return true;
		}

		OutError = TEXT("margin must be a number, array, or object");
		return false;
	}

	static bool TryReadMargin(
		const TSharedPtr<FJsonObject>& Obj,
		const TCHAR* Field,
		FMargin& OutMargin,
		FString& OutError)
	{
		if (!Obj.IsValid() || !Obj->HasField(Field))
		{
			return false;
		}
		return TryReadMarginFromValue(Obj->TryGetField(Field), OutMargin, OutError);
	}

	static bool TryReadColorFromValue(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor, FString& OutError)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array))
		{
			if (!Array || (Array->Num() != 3 && Array->Num() != 4))
			{
				OutError = TEXT("color array must be [R, G, B] or [R, G, B, A]");
				return false;
			}

			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = 1.0;
			if (!(*Array)[0]->TryGetNumber(R) || !(*Array)[1]->TryGetNumber(G) || !(*Array)[2]->TryGetNumber(B))
			{
				OutError = TEXT("color array values must be numeric");
				return false;
			}
			if (Array->Num() == 4 && !(*Array)[3]->TryGetNumber(A))
			{
				OutError = TEXT("color alpha must be numeric");
				return false;
			}

			OutColor = FLinearColor(R, G, B, A);
			return true;
		}

		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (Value->TryGetObject(ColorObj) && ColorObj && ColorObj->IsValid())
		{
			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = 1.0;
			(*ColorObj)->TryGetNumberField(TEXT("r"), R);
			(*ColorObj)->TryGetNumberField(TEXT("g"), G);
			(*ColorObj)->TryGetNumberField(TEXT("b"), B);
			(*ColorObj)->TryGetNumberField(TEXT("a"), A);
			OutColor = FLinearColor(R, G, B, A);
			return true;
		}

		FString Hex;
		if (Value->TryGetString(Hex))
		{
			Hex.RemoveFromStart(TEXT("#"));
			if (Hex.Len() == 6 || Hex.Len() == 8)
			{
				const FColor Parsed = FColor::FromHex(Hex);
				OutColor = FLinearColor(Parsed);
				return true;
			}
		}

		OutError = TEXT("color must be an array, object, or #RRGGBB/#RRGGBBAA string");
		return false;
	}

	static bool TryReadColor(
		const TSharedPtr<FJsonObject>& Obj,
		const TCHAR* Field,
		FLinearColor& OutColor,
		FString& OutError)
	{
		if (!Obj.IsValid() || !Obj->HasField(Field))
		{
			return false;
		}
		return TryReadColorFromValue(Obj->TryGetField(Field), OutColor, OutError);
	}

	static ESlateVisibility ParseVisibility(const FString& Value)
	{
		if (Value.Equals(TEXT("collapsed"), ESearchCase::IgnoreCase))
		{
			return ESlateVisibility::Collapsed;
		}
		if (Value.Equals(TEXT("hidden"), ESearchCase::IgnoreCase))
		{
			return ESlateVisibility::Hidden;
		}
		if (Value.Equals(TEXT("hit_test_invisible"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("hittestinvisible"), ESearchCase::IgnoreCase))
		{
			return ESlateVisibility::HitTestInvisible;
		}
		if (Value.Equals(TEXT("self_hit_test_invisible"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("selfhittestinvisible"), ESearchCase::IgnoreCase))
		{
			return ESlateVisibility::SelfHitTestInvisible;
		}
		return ESlateVisibility::Visible;
	}

	static EHorizontalAlignment ParseHAlign(const FString& Value)
	{
		if (Value.Equals(TEXT("left"), ESearchCase::IgnoreCase))
		{
			return HAlign_Left;
		}
		if (Value.Equals(TEXT("center"), ESearchCase::IgnoreCase))
		{
			return HAlign_Center;
		}
		if (Value.Equals(TEXT("right"), ESearchCase::IgnoreCase))
		{
			return HAlign_Right;
		}
		return HAlign_Fill;
	}

	static EVerticalAlignment ParseVAlign(const FString& Value)
	{
		if (Value.Equals(TEXT("top"), ESearchCase::IgnoreCase))
		{
			return VAlign_Top;
		}
		if (Value.Equals(TEXT("center"), ESearchCase::IgnoreCase))
		{
			return VAlign_Center;
		}
		if (Value.Equals(TEXT("bottom"), ESearchCase::IgnoreCase))
		{
			return VAlign_Bottom;
		}
		return VAlign_Fill;
	}

	static ETextJustify::Type ParseJustification(const FString& Value)
	{
		if (Value.Equals(TEXT("center"), ESearchCase::IgnoreCase))
		{
			return ETextJustify::Center;
		}
		if (Value.Equals(TEXT("right"), ESearchCase::IgnoreCase))
		{
			return ETextJustify::Right;
		}
		return ETextJustify::Left;
	}

	static EStretch::Type ParseStretch(const FString& Value)
	{
		if (Value.Equals(TEXT("scale_to_fit"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("ScaleToFit"), ESearchCase::CaseSensitive))
		{
			return EStretch::ScaleToFit;
		}
		if (Value.Equals(TEXT("scale_to_fill"), ESearchCase::IgnoreCase))
		{
			return EStretch::ScaleToFill;
		}
		if (Value.Equals(TEXT("fill"), ESearchCase::IgnoreCase))
		{
			return EStretch::Fill;
		}
		if (Value.Equals(TEXT("user_specified"), ESearchCase::IgnoreCase))
		{
			return EStretch::UserSpecified;
		}
		return EStretch::None;
	}

	static FSlateChildSize ParseSlateChildSize(const TSharedPtr<FJsonObject>& Obj)
	{
		FSlateChildSize Size;
		FString SizeRule;
		if (Obj.IsValid() && Obj->TryGetStringField(TEXT("size_rule"), SizeRule))
		{
			Size.SizeRule = SizeRule.Equals(TEXT("fill"), ESearchCase::IgnoreCase)
				? ESlateSizeRule::Fill
				: ESlateSizeRule::Automatic;
		}

		double SizeValue = 1.0;
		if (Obj.IsValid() && Obj->TryGetNumberField(TEXT("size_value"), SizeValue))
		{
			Size.Value = SizeValue;
		}
		return Size;
	}

	static TSharedPtr<FJsonObject> MakeCreatedWidgetSummary(UWidget* Widget, UWidget* ParentWidget)
	{
		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetStringField(TEXT("name"), Widget ? Widget->GetName() : TEXT(""));
		Summary->SetStringField(TEXT("class"), Widget && Widget->GetClass() ? Widget->GetClass()->GetName() : TEXT(""));
		if (ParentWidget)
		{
			Summary->SetStringField(TEXT("parent"), ParentWidget->GetName());
		}
		return Summary;
	}

	static int32 DetachDesignerTree(UWidgetTree* WidgetTree)
	{
		if (!WidgetTree || !WidgetTree->RootWidget)
		{
			return 0;
		}

		TArray<UWidget*> ExistingWidgets;
		WidgetTree->ForEachWidget([&ExistingWidgets](UWidget* Widget)
		{
			if (Widget)
			{
				ExistingWidgets.Add(Widget);
			}
		});

		WidgetTree->RemoveWidget(WidgetTree->RootWidget);
		WidgetTree->RootWidget = nullptr;

		for (UWidget* Widget : ExistingWidgets)
		{
			Widget->Modify();
			Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}

		return ExistingWidgets.Num();
	}

	template<typename SlotType>
	static void ApplyPaddingAndAlignment(SlotType* Slot, const TSharedPtr<FJsonObject>& SlotSpec, FString& OutError)
	{
		if (!Slot || !SlotSpec.IsValid())
		{
			return;
		}

		FMargin Padding;
		if (TryReadMargin(SlotSpec, TEXT("padding"), Padding, OutError))
		{
			Slot->SetPadding(Padding);
		}

		FString HAlign;
		if (SlotSpec->TryGetStringField(TEXT("horizontal_alignment"), HAlign))
		{
			Slot->SetHorizontalAlignment(ParseHAlign(HAlign));
		}

		FString VAlign;
		if (SlotSpec->TryGetStringField(TEXT("vertical_alignment"), VAlign))
		{
			Slot->SetVerticalAlignment(ParseVAlign(VAlign));
		}
	}
}

FString UApplyWidgetTreeTool::GetToolDescription() const
{
	return TEXT("Build or replace a WidgetBlueprint Designer hierarchy from a declarative JSON spec. "
		"Supports common UMG widgets, nested panel trees, slot layout, styling primitives, optional compile/save, "
		"and inspect-widget-blueprint verification.");
}

TMap<FString, FBridgeSchemaProperty> UApplyWidgetTreeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("WidgetBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Spec;
	Spec.Type = TEXT("object");
	Spec.Description = TEXT("Designer tree spec object. Use {root:{class,name,children,slot,...}} or a root widget object directly.");
	Spec.bRequired = true;
	Schema.Add(TEXT("spec"), Spec);

	FBridgeSchemaProperty Replace;
	Replace.Type = TEXT("boolean");
	Replace.Description = TEXT("Replace the current designer root instead of appending to the existing root (default: true)");
	Replace.bRequired = false;
	Schema.Add(TEXT("replace"), Replace);

	FBridgeSchemaProperty Compile;
	Compile.Type = TEXT("boolean");
	Compile.Description = TEXT("Compile the WidgetBlueprint after applying the tree (default: false)");
	Compile.bRequired = false;
	Schema.Add(TEXT("compile"), Compile);

	FBridgeSchemaProperty Save;
	Save.Type = TEXT("boolean");
	Save.Description = TEXT("Save the WidgetBlueprint package after applying the tree (default: false)");
	Save.bRequired = false;
	Schema.Add(TEXT("save"), Save);

	FBridgeSchemaProperty Checkout;
	Checkout.Type = TEXT("boolean");
	Checkout.Description = TEXT("Attempt source-control checkout before modifying/saving (default: false)");
	Checkout.bRequired = false;
	Schema.Add(TEXT("checkout"), Checkout);

	return Schema;
}

TArray<FString> UApplyWidgetTreeTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("spec") };
}

FBridgeToolResult UApplyWidgetTreeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	const TSharedPtr<FJsonObject>* SpecPtr = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(TEXT("spec"), SpecPtr) || !SpecPtr || !SpecPtr->IsValid())
	{
		return FBridgeToolResult::Error(TEXT("spec is required and must be a JSON object"));
	}

	TSharedPtr<FJsonObject> Spec = *SpecPtr;
	TSharedPtr<FJsonObject> RootSpec;
	const TSharedPtr<FJsonObject>* RootSpecPtr = nullptr;
	if (Spec->TryGetObjectField(TEXT("root"), RootSpecPtr) && RootSpecPtr && RootSpecPtr->IsValid())
	{
		RootSpec = *RootSpecPtr;
	}
	else
	{
		RootSpec = Spec;
	}

	if (!RootSpec.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("spec.root must be a JSON object"));
	}

	const bool bReplace = GetBoolArgOrDefault(Arguments, TEXT("replace"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);

	FString LoadError;
	UWidgetBlueprint* WidgetBP = FBridgeAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBP)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	UWidgetTree* WidgetTree = WidgetBP->WidgetTree;
	if (!WidgetTree)
	{
		return FBridgeToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
	}

	bool bCheckedOut = false;
	if (bCheckout)
	{
		UPackage* Package = WidgetBP->GetOutermost();
		if (Package)
		{
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(
				Package->GetName(),
				FPackageName::GetAssetPackageExtension());
			if (IFileManager::Get().FileExists(*PackageFileName) && IFileManager::Get().IsReadOnly(*PackageFileName))
			{
				FString CheckoutError;
				if (!FBridgeAssetModifier::CheckoutFile(PackageFileName, CheckoutError))
				{
					return FBridgeToolResult::Error(FString::Printf(TEXT("Checkout failed: %s"), *CheckoutError));
				}
				bCheckedOut = true;
			}
		}
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("apply-widget-tree: %s replace=%d"), *AssetPath, bReplace);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "ApplyWidgetTree", "Apply Widget Tree to {0}"),
			FText::FromString(AssetPath)));

	FBridgeAssetModifier::MarkModified(WidgetBP);
	FBridgeAssetModifier::MarkModified(WidgetTree);

	UWidget* ParentForRoot = nullptr;
	int32 RemovedWidgetCount = 0;
	if (bReplace)
	{
		RemovedWidgetCount = DetachDesignerTree(WidgetTree);
	}
	else
	{
		ParentForRoot = WidgetTree->RootWidget;
	}

	TArray<TSharedPtr<FJsonValue>> CreatedWidgets;
	FString ApplyError;
	UWidget* NewRoot = BuildWidgetFromSpec(WidgetBP, WidgetTree, RootSpec, ParentForRoot, CreatedWidgets, ApplyError);
	if (!NewRoot)
	{
		return FBridgeToolResult::Error(ApplyError.IsEmpty() ? TEXT("Failed to apply widget tree") : ApplyError);
	}

	if (bReplace || !ParentForRoot)
	{
		WidgetTree->RootWidget = NewRoot;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	FBridgeAssetModifier::MarkPackageDirty(WidgetBP);
	WidgetBP->PostEditChange();

	bool bCompiled = false;
	FString CompileError;
	if (bCompile)
	{
		FBridgeAssetModifier::RefreshBlueprintNodes(WidgetBP);
		bCompiled = FBridgeAssetModifier::CompileBlueprint(WidgetBP, CompileError);
		if (!bCompiled)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("compile failed: %s"), *CompileError));
		}
	}

	bool bSaved = false;
	FString SaveError;
	if (bSave)
	{
		bSaved = FBridgeAssetModifier::SaveAsset(WidgetBP, false, SaveError);
		if (!bSaved)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("save failed: %s"), *SaveError));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetBoolField(TEXT("replaced"), bReplace || !ParentForRoot);
	Result->SetStringField(TEXT("root_widget"), WidgetTree->RootWidget ? WidgetTree->RootWidget->GetName() : TEXT(""));
	Result->SetStringField(TEXT("applied_widget"), NewRoot->GetName());
	Result->SetNumberField(TEXT("widget_count"), CreatedWidgets.Num());
	Result->SetNumberField(TEXT("removed_widget_count"), RemovedWidgetCount);
	Result->SetArrayField(TEXT("created_widgets"), CreatedWidgets);
	Result->SetBoolField(TEXT("needs_compile"), !bCompile);
	Result->SetBoolField(TEXT("needs_save"), !bSave);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("saved"), bSaved);
	if (bCheckout)
	{
		Result->SetBoolField(TEXT("checked_out"), bCheckedOut);
	}

	return FBridgeToolResult::Json(Result);
}

UWidget* UApplyWidgetTreeTool::BuildWidgetFromSpec(
	UWidgetBlueprint* WidgetBP,
	UWidgetTree* WidgetTree,
	const TSharedPtr<FJsonObject>& NodeSpec,
	UWidget* ParentWidget,
	TArray<TSharedPtr<FJsonValue>>& OutCreatedWidgets,
	FString& OutError)
{
	if (!WidgetBP || !WidgetTree || !NodeSpec.IsValid())
	{
		OutError = TEXT("Invalid widget tree build context");
		return nullptr;
	}

	FString WidgetClassName;
	if (!NodeSpec->TryGetStringField(TEXT("class"), WidgetClassName))
	{
		NodeSpec->TryGetStringField(TEXT("type"), WidgetClassName);
	}
	if (WidgetClassName.IsEmpty())
	{
		OutError = TEXT("Each widget spec requires a class field");
		return nullptr;
	}

	FString WidgetName;
	NodeSpec->TryGetStringField(TEXT("name"), WidgetName);
	if (WidgetName.IsEmpty())
	{
		WidgetName = WidgetClassName;
		WidgetName.ReplaceInline(TEXT("/"), TEXT("_"));
		WidgetName.ReplaceInline(TEXT("."), TEXT("_"));
	}

	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName, OutError);
	if (!WidgetClass)
	{
		return nullptr;
	}

	UWidget* Widget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
	if (!Widget)
	{
		OutError = FString::Printf(TEXT("Failed to construct widget %s (%s)"), *WidgetName, *WidgetClassName);
		return nullptr;
	}

	Widget->Modify();
	if (!ApplyWidgetProperties(Widget, NodeSpec, OutError))
	{
		return nullptr;
	}

	if (ParentWidget)
	{
		if (!AttachToParent(ParentWidget, Widget, OutError))
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* SlotSpec = nullptr;
		if (NodeSpec->TryGetObjectField(TEXT("slot"), SlotSpec) && SlotSpec && SlotSpec->IsValid() && Widget->Slot)
		{
			if (!ApplySlotProperties(Widget->Slot, *SlotSpec, OutError))
			{
				return nullptr;
			}
		}
	}

	OutCreatedWidgets.Add(MakeShareable(new FJsonValueObject(MakeCreatedWidgetSummary(Widget, ParentWidget))));

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (NodeSpec->TryGetArrayField(TEXT("children"), Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			const TSharedPtr<FJsonObject>* ChildSpec = nullptr;
			if (!ChildValue.IsValid() || !ChildValue->TryGetObject(ChildSpec) || !ChildSpec || !ChildSpec->IsValid())
			{
				OutError = FString::Printf(TEXT("children for %s must contain objects"), *WidgetName);
				return nullptr;
			}

			if (!BuildWidgetFromSpec(WidgetBP, WidgetTree, *ChildSpec, Widget, OutCreatedWidgets, OutError))
			{
				return nullptr;
			}
		}
	}

	return Widget;
}

UClass* UApplyWidgetTreeTool::ResolveWidgetClass(const FString& WidgetClass, FString& OutError) const
{
	if (WidgetClass.Equals(TEXT("CanvasPanel"), ESearchCase::IgnoreCase))
	{
		return UCanvasPanel::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("Overlay"), ESearchCase::IgnoreCase))
	{
		return UOverlay::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("Border"), ESearchCase::IgnoreCase))
	{
		return UBorder::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("SizeBox"), ESearchCase::IgnoreCase))
	{
		return USizeBox::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("ScaleBox"), ESearchCase::IgnoreCase))
	{
		return UScaleBox::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("Image"), ESearchCase::IgnoreCase))
	{
		return UImage::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("TextBlock"), ESearchCase::IgnoreCase))
	{
		return UTextBlock::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("Button"), ESearchCase::IgnoreCase))
	{
		return UButton::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("HorizontalBox"), ESearchCase::IgnoreCase))
	{
		return UHorizontalBox::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("VerticalBox"), ESearchCase::IgnoreCase))
	{
		return UVerticalBox::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("UniformGridPanel"), ESearchCase::IgnoreCase))
	{
		return UUniformGridPanel::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("GridPanel"), ESearchCase::IgnoreCase))
	{
		return UGridPanel::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("ScrollBox"), ESearchCase::IgnoreCase))
	{
		return UScrollBox::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("Spacer"), ESearchCase::IgnoreCase))
	{
		return USpacer::StaticClass();
	}
	if (WidgetClass.Equals(TEXT("WidgetSwitcher"), ESearchCase::IgnoreCase))
	{
		return UWidgetSwitcher::StaticClass();
	}

	if (WidgetClass.StartsWith(TEXT("/")))
	{
		if (UClass* LoadedClass = LoadClass<UWidget>(nullptr, *WidgetClass))
		{
			return LoadedClass;
		}
		if (UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetClass))
		{
			if (WidgetBP->GeneratedClass && WidgetBP->GeneratedClass->IsChildOf<UWidget>())
			{
				return WidgetBP->GeneratedClass;
			}
		}
	}

	UClass* ResolvedClass = FBridgePropertySerializer::ResolveClassOfType<UWidget>(WidgetClass, OutError);
	if (ResolvedClass)
	{
		return ResolvedClass;
	}

	OutError = FString::Printf(TEXT("Widget class not found or not a UWidget subclass: %s"), *WidgetClass);
	return nullptr;
}

bool UApplyWidgetTreeTool::AttachToParent(UWidget* ParentWidget, UWidget* ChildWidget, FString& OutError) const
{
	if (!ParentWidget || !ChildWidget)
	{
		OutError = TEXT("Cannot attach null widget");
		return false;
	}

	if (UContentWidget* ContentParent = Cast<UContentWidget>(ParentWidget))
	{
		if (ContentParent->GetContent())
		{
			OutError = FString::Printf(TEXT("Parent widget already contains a child: %s"), *ParentWidget->GetName());
			return false;
		}
		ContentParent->SetContent(ChildWidget);
		return true;
	}

	if (UPanelWidget* PanelParent = Cast<UPanelWidget>(ParentWidget))
	{
		PanelParent->AddChild(ChildWidget);
		return true;
	}

	OutError = FString::Printf(TEXT("Parent widget does not support child attachment: %s"), *ParentWidget->GetName());
	return false;
}

bool UApplyWidgetTreeTool::ApplyWidgetProperties(
	UWidget* Widget,
	const TSharedPtr<FJsonObject>& NodeSpec,
	FString& OutError) const
{
	if (!Widget || !NodeSpec.IsValid())
	{
		return false;
	}

	FString Visibility;
	if (TryGetJsonString(NodeSpec, TEXT("visibility"), Visibility))
	{
		Widget->SetVisibility(ParseVisibility(Visibility));
	}

	double RenderOpacity = 0.0;
	if (TryGetJsonNumber(NodeSpec, TEXT("render_opacity"), RenderOpacity))
	{
		Widget->SetRenderOpacity(RenderOpacity);
	}

	bool bIsEnabled = true;
	if (TryGetJsonBool(NodeSpec, TEXT("is_enabled"), bIsEnabled))
	{
		Widget->SetIsEnabled(bIsEnabled);
	}

	bool bIsVariable = false;
	if (TryGetJsonBool(NodeSpec, TEXT("is_variable"), bIsVariable))
	{
		Widget->bIsVariable = bIsVariable;
	}

	FString ToolTip;
	if (TryGetJsonString(NodeSpec, TEXT("tool_tip"), ToolTip))
	{
		Widget->SetToolTipText(FText::FromString(ToolTip));
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
	{
		FString Text;
		if (TryGetJsonString(NodeSpec, TEXT("text"), Text))
		{
			TextBlock->SetText(FText::FromString(Text));
		}

		FLinearColor Color;
		if (TryReadColor(NodeSpec, TEXT("color"), Color, OutError) ||
			TryReadColor(NodeSpec, TEXT("color_and_opacity"), Color, OutError))
		{
			TextBlock->SetColorAndOpacity(FSlateColor(Color));
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		double FontSize = 0.0;
		if (TryGetJsonNumber(NodeSpec, TEXT("font_size"), FontSize))
		{
			FSlateFontInfo Font = TextBlock->GetFont();
			Font.Size = FontSize;
			TextBlock->SetFont(Font);
		}

		FString Justification;
		if (TryGetJsonString(NodeSpec, TEXT("justification"), Justification))
		{
			TextBlock->SetJustification(ParseJustification(Justification));
		}

		double WrapAt = 0.0;
		if (TryGetJsonNumber(NodeSpec, TEXT("wrap_at"), WrapAt))
		{
			TextBlock->SetWrapTextAt(WrapAt);
		}

		bool bAutoWrapText = false;
		if (TryGetJsonBool(NodeSpec, TEXT("auto_wrap_text"), bAutoWrapText))
		{
			TextBlock->SetAutoWrapText(bAutoWrapText);
		}
	}

	if (UImage* Image = Cast<UImage>(Widget))
	{
		FLinearColor Tint;
		if (TryReadColor(NodeSpec, TEXT("tint"), Tint, OutError) ||
			TryReadColor(NodeSpec, TEXT("color"), Tint, OutError) ||
			TryReadColor(NodeSpec, TEXT("color_and_opacity"), Tint, OutError))
		{
			Image->SetColorAndOpacity(Tint);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FString TexturePath;
		if (TryGetJsonString(NodeSpec, TEXT("texture"), TexturePath))
		{
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
			if (!Texture)
			{
				OutError = FString::Printf(TEXT("Failed to load texture: %s"), *TexturePath);
				return false;
			}
			Image->SetBrushFromTexture(Texture);
		}

		FString MaterialPath;
		if (TryGetJsonString(NodeSpec, TEXT("material"), MaterialPath))
		{
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material)
			{
				OutError = FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath);
				return false;
			}
			Image->SetBrushFromMaterial(Material);
		}
	}

	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		FLinearColor BrushColor;
		if (TryReadColor(NodeSpec, TEXT("brush_color"), BrushColor, OutError))
		{
			Border->SetBrushColor(BrushColor);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FLinearColor ContentColor;
		if (TryReadColor(NodeSpec, TEXT("content_color"), ContentColor, OutError) ||
			TryReadColor(NodeSpec, TEXT("content_color_and_opacity"), ContentColor, OutError))
		{
			Border->SetContentColorAndOpacity(ContentColor);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FMargin Padding;
		if (TryReadMargin(NodeSpec, TEXT("padding"), Padding, OutError))
		{
			Border->SetPadding(Padding);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}
	}

	if (UButton* Button = Cast<UButton>(Widget))
	{
		FLinearColor BackgroundColor;
		if (TryReadColor(NodeSpec, TEXT("background_color"), BackgroundColor, OutError))
		{
			Button->SetBackgroundColor(BackgroundColor);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FLinearColor ContentColor;
		if (TryReadColor(NodeSpec, TEXT("content_color"), ContentColor, OutError) ||
			TryReadColor(NodeSpec, TEXT("color_and_opacity"), ContentColor, OutError))
		{
			Button->SetColorAndOpacity(ContentColor);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}
	}

	if (USpacer* Spacer = Cast<USpacer>(Widget))
	{
		FVector2D DesiredSize;
		if (TryReadVector2(NodeSpec, TEXT("desired_size"), DesiredSize, OutError) ||
			TryReadVector2(NodeSpec, TEXT("size"), DesiredSize, OutError))
		{
			Spacer->SetSize(DesiredSize);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}
	}

	if (USizeBox* SizeBox = Cast<USizeBox>(Widget))
	{
		double WidthOverride = 0.0;
		if (TryGetJsonNumber(NodeSpec, TEXT("width_override"), WidthOverride))
		{
			SizeBox->SetWidthOverride(WidthOverride);
		}
		double HeightOverride = 0.0;
		if (TryGetJsonNumber(NodeSpec, TEXT("height_override"), HeightOverride))
		{
			SizeBox->SetHeightOverride(HeightOverride);
		}
	}

	if (UScaleBox* ScaleBox = Cast<UScaleBox>(Widget))
	{
		FString Stretch;
		if (TryGetJsonString(NodeSpec, TEXT("stretch"), Stretch))
		{
			ScaleBox->SetStretch(ParseStretch(Stretch));
		}
	}

	if (UWidgetSwitcher* WidgetSwitcher = Cast<UWidgetSwitcher>(Widget))
	{
		double ActiveWidgetIndex = 0.0;
		if (TryGetJsonNumber(NodeSpec, TEXT("active_widget_index"), ActiveWidgetIndex))
		{
			WidgetSwitcher->SetActiveWidgetIndex(static_cast<int32>(ActiveWidgetIndex));
		}
	}

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (NodeSpec->TryGetObjectField(TEXT("properties"), Properties) && Properties && Properties->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString FindError;
			if (!FBridgeAssetModifier::FindPropertyByPath(Widget, Pair.Key, Property, Container, FindError))
			{
				OutError = FString::Printf(TEXT("%s.%s: %s"), *Widget->GetName(), *Pair.Key, *FindError);
				return false;
			}
			if (!FBridgeAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, OutError))
			{
				OutError = FString::Printf(TEXT("%s.%s: %s"), *Widget->GetName(), *Pair.Key, *OutError);
				return false;
			}
		}
	}

	return true;
}

bool UApplyWidgetTreeTool::ApplySlotProperties(
	UPanelSlot* Slot,
	const TSharedPtr<FJsonObject>& SlotSpec,
	FString& OutError) const
{
	if (!Slot || !SlotSpec.IsValid())
	{
		return true;
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		FVector2D Position;
		if (TryReadVector2(SlotSpec, TEXT("position"), Position, OutError))
		{
			CanvasSlot->SetPosition(Position);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FVector2D Size;
		if (TryReadVector2(SlotSpec, TEXT("size"), Size, OutError))
		{
			CanvasSlot->SetSize(Size);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FVector2D Alignment;
		if (TryReadVector2(SlotSpec, TEXT("alignment"), Alignment, OutError))
		{
			CanvasSlot->SetAlignment(Alignment);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		FMargin Offsets;
		if (TryReadMargin(SlotSpec, TEXT("offsets"), Offsets, OutError))
		{
			CanvasSlot->SetOffsets(Offsets);
		}
		if (!OutError.IsEmpty())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* AnchorsObj = nullptr;
		if (SlotSpec->TryGetObjectField(TEXT("anchors"), AnchorsObj) && AnchorsObj && AnchorsObj->IsValid())
		{
			FVector2D Min;
			FVector2D Max;
			if (!TryReadVector2(*AnchorsObj, TEXT("min"), Min, OutError))
			{
				return false;
			}
			if (!TryReadVector2(*AnchorsObj, TEXT("max"), Max, OutError))
			{
				return false;
			}
			CanvasSlot->SetAnchors(FAnchors(Min.X, Min.Y, Max.X, Max.Y));
		}

		double ZOrder = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("z_order"), ZOrder))
		{
			CanvasSlot->SetZOrder(static_cast<int32>(ZOrder));
		}

		bool bAutoSize = false;
		if (TryGetJsonBool(SlotSpec, TEXT("auto_size"), bAutoSize))
		{
			CanvasSlot->SetAutoSize(bAutoSize);
		}
		return true;
	}

	if (UVerticalBoxSlot* VerticalBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		ApplyPaddingAndAlignment(VerticalBoxSlot, SlotSpec, OutError);
		VerticalBoxSlot->SetSize(ParseSlateChildSize(SlotSpec));
		return OutError.IsEmpty();
	}
	if (UHorizontalBoxSlot* HorizontalBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		ApplyPaddingAndAlignment(HorizontalBoxSlot, SlotSpec, OutError);
		HorizontalBoxSlot->SetSize(ParseSlateChildSize(SlotSpec));
		return OutError.IsEmpty();
	}
	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		ApplyPaddingAndAlignment(OverlaySlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}
	if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
	{
		ApplyPaddingAndAlignment(BorderSlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
	{
		ApplyPaddingAndAlignment(ButtonSlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}
	if (USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(Slot))
	{
		ApplyPaddingAndAlignment(SizeBoxSlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}
	if (UScaleBoxSlot* ScaleBoxSlot = Cast<UScaleBoxSlot>(Slot))
	{
		FString HAlign;
		if (SlotSpec->TryGetStringField(TEXT("horizontal_alignment"), HAlign))
		{
			ScaleBoxSlot->SetHorizontalAlignment(ParseHAlign(HAlign));
		}
		FString VAlign;
		if (SlotSpec->TryGetStringField(TEXT("vertical_alignment"), VAlign))
		{
			ScaleBoxSlot->SetVerticalAlignment(ParseVAlign(VAlign));
		}
		return true;
	}
	if (UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(Slot))
	{
		FMargin Padding;
		if (TryReadMargin(SlotSpec, TEXT("padding"), Padding, OutError))
		{
			ScrollBoxSlot->SetPadding(Padding);
		}
		FString HAlign;
		if (SlotSpec->TryGetStringField(TEXT("horizontal_alignment"), HAlign))
		{
			ScrollBoxSlot->SetHorizontalAlignment(ParseHAlign(HAlign));
		}
		return OutError.IsEmpty();
	}
	if (UWidgetSwitcherSlot* SwitcherSlot = Cast<UWidgetSwitcherSlot>(Slot))
	{
		ApplyPaddingAndAlignment(SwitcherSlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}
	if (UUniformGridSlot* UniformGridSlot = Cast<UUniformGridSlot>(Slot))
	{
		double Row = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("row"), Row))
		{
			UniformGridSlot->SetRow(static_cast<int32>(Row));
		}
		double Column = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("column"), Column))
		{
			UniformGridSlot->SetColumn(static_cast<int32>(Column));
		}
		FString HAlign;
		if (SlotSpec->TryGetStringField(TEXT("horizontal_alignment"), HAlign))
		{
			UniformGridSlot->SetHorizontalAlignment(ParseHAlign(HAlign));
		}
		FString VAlign;
		if (SlotSpec->TryGetStringField(TEXT("vertical_alignment"), VAlign))
		{
			UniformGridSlot->SetVerticalAlignment(ParseVAlign(VAlign));
		}
		return true;
	}
	if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
	{
		double Row = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("row"), Row))
		{
			GridSlot->SetRow(static_cast<int32>(Row));
		}
		double Column = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("column"), Column))
		{
			GridSlot->SetColumn(static_cast<int32>(Column));
		}
		double RowSpan = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("row_span"), RowSpan))
		{
			GridSlot->SetRowSpan(static_cast<int32>(RowSpan));
		}
		double ColumnSpan = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("column_span"), ColumnSpan))
		{
			GridSlot->SetColumnSpan(static_cast<int32>(ColumnSpan));
		}
		double Layer = 0.0;
		if (TryGetJsonNumber(SlotSpec, TEXT("layer"), Layer))
		{
			GridSlot->SetLayer(static_cast<int32>(Layer));
		}
		ApplyPaddingAndAlignment(GridSlot, SlotSpec, OutError);
		return OutError.IsEmpty();
	}

	return true;
}
