// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/WireWidgetNavigationTool.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "Utils/BridgeAssetModifier.h"
#include "WidgetBlueprint.h"

FString UWireWidgetNavigationTool::GetToolDescription() const
{
	return TEXT("Validate named UMG button navigation bindings for a WidgetBlueprint and expose required widgets as variables for a reusable parent_binding_contract. Supports WidgetSwitcher and viewport-replace contracts, optional checkout/compile/save, and returns an inspectable binding summary.");
}

TMap<FString, FBridgeSchemaProperty> UWireWidgetNavigationTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("WidgetBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Bindings;
	Bindings.Type = TEXT("array");
	Bindings.Description = TEXT("Navigation bindings array. Each item requires button and supports mode=switcher|viewport-replace, switcher, target_index, target_widget, target_widget_class.");
	Bindings.bRequired = true;
	Schema.Add(TEXT("bindings"), Bindings);

	FBridgeSchemaProperty Compile;
	Compile.Type = TEXT("boolean");
	Compile.Description = TEXT("Compile after preparing navigation contract (default: false)");
	Schema.Add(TEXT("compile"), Compile);

	FBridgeSchemaProperty Save;
	Save.Type = TEXT("boolean");
	Save.Description = TEXT("Save after preparing navigation contract (default: false)");
	Schema.Add(TEXT("save"), Save);

	FBridgeSchemaProperty Checkout;
	Checkout.Type = TEXT("boolean");
	Checkout.Description = TEXT("Attempt source-control checkout before modifying/saving (default: false)");
	Schema.Add(TEXT("checkout"), Checkout);

	FBridgeSchemaProperty AllowPIE;
	AllowPIE.Type = TEXT("boolean");
	AllowPIE.Description = TEXT("Allow mutating the WidgetBlueprint while PIE is active. Default false fails fast with status=blocked_active_pie.");
	Schema.Add(TEXT("allow_pie"), AllowPIE);

	FBridgeSchemaProperty AllowBusy;
	AllowBusy.Type = TEXT("boolean");
	AllowBusy.Description = TEXT("Allow mutating the WidgetBlueprint while the editor is saving or garbage collecting. Default false fails fast with status=blocked_editor_busy.");
	Schema.Add(TEXT("allow_busy"), AllowBusy);

	return Schema;
}

TArray<FString> UWireWidgetNavigationTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("bindings") };
}

FBridgeToolResult UWireWidgetNavigationTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("bindings"), Bindings) || !Bindings)
	{
		return FBridgeToolResult::Error(TEXT("bindings is required and must be an array"));
	}

	const bool bAllowPIE = GetBoolArgOrDefault(Arguments, TEXT("allow_pie"), false);
	if (!bAllowPIE && GEditor && GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("wire-widget-navigation blocked_active_pie editor_state=pie_active: PIE is active; stop PIE before mutating WidgetBlueprint assets, or pass allow_pie=true / --allow-pie to override."));
	}
	const bool bAllowBusy = GetBoolArgOrDefault(Arguments, TEXT("allow_busy"), false);
	if (!bAllowBusy && (GIsSavingPackage || IsGarbageCollecting()))
	{
		return FBridgeToolResult::Error(TEXT("wire-widget-navigation blocked_editor_busy editor_state=busy: the editor is saving or garbage collecting; retry after the editor is idle, or pass allow_busy=true / --allow-busy to override."));
	}

	FString LoadError;
	UWidgetBlueprint* WidgetBP = FBridgeAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBP)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!WidgetBP->WidgetTree)
	{
		return FBridgeToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
	}

	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);

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

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("SoftUEBridge", "WireWidgetNavigation", "Wire Widget Navigation: {0}"),
			FText::FromString(AssetPath)));

	TArray<TSharedPtr<FJsonValue>> BindingSummaries;
	TArray<TSharedPtr<FJsonValue>> ChangedWidgets;
	TSet<FString> RequiredWidgetNames;

	for (int32 Index = 0; Index < Bindings->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* BindingObjPtr = nullptr;
		if (!(*Bindings)[Index].IsValid() || !(*Bindings)[Index]->TryGetObject(BindingObjPtr) || !BindingObjPtr || !BindingObjPtr->IsValid())
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("bindings[%d] must be an object"), Index));
		}

		const TSharedPtr<FJsonObject>& BindingObj = *BindingObjPtr;
		FString ButtonName;
		if (!BindingObj->TryGetStringField(TEXT("button"), ButtonName) || ButtonName.IsEmpty())
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("bindings[%d].button is required"), Index));
		}

		UButton* Button = Cast<UButton>(FindWidgetByName(WidgetBP->WidgetTree, ButtonName));
		if (!Button)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Navigation button not found or not a Button: %s"), *ButtonName));
		}
		EnsureVariable(Button, ChangedWidgets);
		RequiredWidgetNames.Add(ButtonName);

		FString Mode;
		BindingObj->TryGetStringField(TEXT("mode"), Mode);
		if (Mode.IsEmpty())
		{
			Mode = TEXT("parent-contract");
		}

		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetStringField(TEXT("button"), ButtonName);
		Summary->SetStringField(TEXT("mode"), Mode);

		FString SwitcherName;
		if (BindingObj->TryGetStringField(TEXT("switcher"), SwitcherName) && !SwitcherName.IsEmpty())
		{
			UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(FindWidgetByName(WidgetBP->WidgetTree, SwitcherName));
			if (!Switcher)
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Navigation switcher not found or not a WidgetSwitcher: %s"), *SwitcherName));
			}
			EnsureVariable(Switcher, ChangedWidgets);
			RequiredWidgetNames.Add(SwitcherName);
			Summary->SetStringField(TEXT("switcher"), SwitcherName);
		}

		double TargetIndex = 0.0;
		if (BindingObj->TryGetNumberField(TEXT("target_index"), TargetIndex))
		{
			Summary->SetNumberField(TEXT("target_index"), static_cast<int32>(TargetIndex));
		}

		FString TargetWidget;
		if (BindingObj->TryGetStringField(TEXT("target_widget"), TargetWidget) && !TargetWidget.IsEmpty())
		{
			UWidget* Target = FindWidgetByName(WidgetBP->WidgetTree, TargetWidget);
			if (!Target)
			{
				return FBridgeToolResult::Error(FString::Printf(TEXT("Navigation target_widget not found: %s"), *TargetWidget));
			}
			EnsureVariable(Target, ChangedWidgets);
			RequiredWidgetNames.Add(TargetWidget);
			Summary->SetStringField(TEXT("target_widget"), TargetWidget);
		}

		FString TargetWidgetClass;
		if (BindingObj->TryGetStringField(TEXT("target_widget_class"), TargetWidgetClass) && !TargetWidgetClass.IsEmpty())
		{
			if (!LoadClass<UUserWidget>(nullptr, *TargetWidgetClass))
			{
				FString TargetLoadError;
				UWidgetBlueprint* TargetBP = FBridgeAssetModifier::LoadAssetByPath<UWidgetBlueprint>(TargetWidgetClass, TargetLoadError);
				if (!TargetBP)
				{
					TargetBP = LoadObject<UWidgetBlueprint>(nullptr, *TargetWidgetClass);
				}
				if (!TargetBP || !TargetBP->GeneratedClass || !TargetBP->GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
				{
					return FBridgeToolResult::Error(FString::Printf(TEXT("target_widget_class is not a loadable UserWidget class or WidgetBlueprint: %s"), *TargetWidgetClass));
				}
			}
			Summary->SetStringField(TEXT("target_widget_class"), TargetWidgetClass);
		}

		BindingSummaries.Add(MakeShareable(new FJsonValueObject(Summary)));
	}

	if (ChangedWidgets.Num() > 0)
	{
		FBridgeAssetModifier::MarkModified(WidgetBP);
		FBridgeAssetModifier::MarkModified(WidgetBP->WidgetTree);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		FBridgeAssetModifier::MarkPackageDirty(WidgetBP);
		WidgetBP->PostEditChange();
	}

	bool bCompiled = false;
	if (bCompile)
	{
		FString CompileError;
		FBridgeAssetModifier::RefreshBlueprintNodes(WidgetBP);
		bCompiled = FBridgeAssetModifier::CompileBlueprint(WidgetBP, CompileError);
		if (!bCompiled)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("compile failed: %s"), *CompileError));
		}
	}

	bool bSaved = false;
	if (bSave)
	{
		FString SaveError;
		bSaved = FBridgeAssetModifier::SaveAsset(WidgetBP, false, SaveError);
		if (!bSaved)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("save failed: %s"), *SaveError));
		}
	}

	TArray<TSharedPtr<FJsonValue>> RequiredWidgets;
	for (const FString& Name : RequiredWidgetNames)
	{
		RequiredWidgets.Add(MakeShareable(new FJsonValueString(Name)));
	}

	TSharedPtr<FJsonObject> Contract = MakeShareable(new FJsonObject);
	Contract->SetStringField(TEXT("pattern"), TEXT("parent-class named widget binding"));
	Contract->SetStringField(TEXT("asset_path"), AssetPath);
	Contract->SetArrayField(TEXT("required_widgets"), RequiredWidgets);
	Contract->SetArrayField(TEXT("bindings"), BindingSummaries);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetNumberField(TEXT("binding_count"), BindingSummaries.Num());
	Result->SetArrayField(TEXT("bindings"), BindingSummaries);
	Result->SetArrayField(TEXT("changed_widgets"), ChangedWidgets);
	Result->SetObjectField(TEXT("parent_binding_contract"), Contract);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetBoolField(TEXT("allow_pie"), bAllowPIE);
	Result->SetBoolField(TEXT("allow_busy"), bAllowBusy);
	Result->SetStringField(TEXT("editor_state"), (GEditor && GEditor->IsPlaySessionInProgress()) ? TEXT("pie_active_allowed") : TEXT("editor"));
	if (bCheckout)
	{
		Result->SetBoolField(TEXT("checked_out"), bCheckedOut);
	}

	return FBridgeToolResult::Json(Result);
}

UWidget* UWireWidgetNavigationTool::FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name) const
{
	if (!WidgetTree || Name.IsEmpty())
	{
		return nullptr;
	}

	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (Widget && Widget->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Widget;
		}
	}
	return nullptr;
}

bool UWireWidgetNavigationTool::EnsureVariable(UWidget* Widget, TArray<TSharedPtr<FJsonValue>>& OutChangedWidgets) const
{
	if (!Widget || Widget->bIsVariable)
	{
		return false;
	}

	Widget->Modify();
	Widget->bIsVariable = true;

	TSharedPtr<FJsonObject> Changed = MakeShareable(new FJsonObject);
	Changed->SetStringField(TEXT("name"), Widget->GetName());
	Changed->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
	Changed->SetBoolField(TEXT("bIsVariable"), true);
	OutChangedWidgets.Add(MakeShareable(new FJsonValueObject(Changed)));
	return true;
}
