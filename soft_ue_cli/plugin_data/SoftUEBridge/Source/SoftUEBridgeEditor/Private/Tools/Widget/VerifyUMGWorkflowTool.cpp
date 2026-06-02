// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/VerifyUMGWorkflowTool.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SoftUEBridgeEditorModule.h"
#include "Tools/Widget/WidgetPreviewRegistry.h"
#include "UObject/UObjectIterator.h"
#include "Utils/BridgeAssetModifier.h"
#include "WidgetBlueprint.h"

FString UVerifyUMGWorkflowTool::GetToolDescription() const
{
	return TEXT("Validate an end-to-end UMG workflow in PIE. Optionally CreateWidget/adds a WidgetBlueprint to the viewport, checks expected widget names/text, broadcasts named Button OnClicked events, validates WidgetSwitcher active state or visible widgets, and can return a capture_after recommendation.");
}

TMap<FString, FBridgeSchemaProperty> UVerifyUMGWorkflowTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty WidgetClass;
	WidgetClass.Type = TEXT("string");
	WidgetClass.Description = TEXT("Widget class path or WidgetBlueprint asset path to CreateWidget and add to viewport");
	Schema.Add(TEXT("widget_class"), WidgetClass);

	FBridgeSchemaProperty RootWidget;
	RootWidget.Type = TEXT("string");
	RootWidget.Description = TEXT("Existing runtime root widget name to validate instead of all viewport roots");
	Schema.Add(TEXT("root_widget"), RootWidget);

	FBridgeSchemaProperty ExpectedWidgets;
	ExpectedWidgets.Type = TEXT("array");
	ExpectedWidgets.Description = TEXT("Expected widget names");
	Schema.Add(TEXT("expected_widgets"), ExpectedWidgets);

	FBridgeSchemaProperty ExpectedText;
	ExpectedText.Type = TEXT("array");
	ExpectedText.Description = TEXT("Expected TextBlock strings");
	Schema.Add(TEXT("expected_text"), ExpectedText);

	FBridgeSchemaProperty ClickSequence;
	ClickSequence.Type = TEXT("array");
	ClickSequence.Description = TEXT("Array of named button click validation steps");
	Schema.Add(TEXT("click_sequence"), ClickSequence);

	FBridgeSchemaProperty PieIndex;
	PieIndex.Type = TEXT("integer");
	PieIndex.Description = TEXT("PIE instance index, 0-based (default: 0)");
	Schema.Add(TEXT("pie_index"), PieIndex);

	FBridgeSchemaProperty ViewportZOrder;
	ViewportZOrder.Type = TEXT("integer");
	ViewportZOrder.Description = TEXT("Viewport Z order for created preview widget (default: 0)");
	Schema.Add(TEXT("viewport_z_order"), ViewportZOrder);

	FBridgeSchemaProperty CaptureAfter;
	CaptureAfter.Type = TEXT("boolean");
	CaptureAfter.Description = TEXT("Return a capture-viewport recommendation after successful verification");
	Schema.Add(TEXT("capture_after"), CaptureAfter);

	FBridgeSchemaProperty RemovePreview;
	RemovePreview.Type = TEXT("boolean");
	RemovePreview.Description = TEXT("Remove the created preview widget before returning (default: false)");
	Schema.Add(TEXT("remove_preview"), RemovePreview);

	FBridgeSchemaProperty PreviewLifecycle;
	PreviewLifecycle.Type = TEXT("string");
	PreviewLifecycle.Description = TEXT("Tool-owned preview lifecycle policy before verification: replace (default), keep, or remove; remove skips new preview creation");
	PreviewLifecycle.Enum = {TEXT("replace"), TEXT("keep"), TEXT("remove")};
	Schema.Add(TEXT("preview_lifecycle"), PreviewLifecycle);

	return Schema;
}

FBridgeToolResult UVerifyUMGWorkflowTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (!GEditor || !GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running. Start one with 'pie-session --action start'."));
	}

	const int32 PieIndex = GetIntArgOrDefault(Arguments, TEXT("pie_index"), 0);
	int32 TotalPIEWorlds = 0;
	UWorld* PIEWorld = GetPIEWorldByIndex(PieIndex, TotalPIEWorlds);
	if (!PIEWorld)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("PIE instance %d not found. %d instance(s) running."),
			PieIndex, TotalPIEWorlds));
	}

	const FString WidgetClassPath = GetStringArgOrDefault(Arguments, TEXT("widget_class"));
	const int32 ViewportZOrder = GetIntArgOrDefault(Arguments, TEXT("viewport_z_order"), 0);
	const bool bRemovePreview = GetBoolArgOrDefault(Arguments, TEXT("remove_preview"), false);
	const bool bCaptureAfter = GetBoolArgOrDefault(Arguments, TEXT("capture_after"), false);
	const FString PreviewLifecycle = GetStringArgOrDefault(Arguments, TEXT("preview_lifecycle"), TEXT("replace")).ToLower();
	if (PreviewLifecycle != TEXT("replace") && PreviewLifecycle != TEXT("keep") && PreviewLifecycle != TEXT("remove"))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid preview_lifecycle '%s'. Valid values: replace, keep, remove"),
			*PreviewLifecycle));
	}

	TArray<FString> RemovedPreviewHandles;
	int32 RemovedExistingPreviews = 0;
	if (PreviewLifecycle == TEXT("replace") || PreviewLifecycle == TEXT("remove"))
	{
		RemovedExistingPreviews = FWidgetPreviewRegistry::RemovePreviewsForWorld(PIEWorld, &RemovedPreviewHandles);
	}

	UUserWidget* CreatedWidget = nullptr;
	FString PreviewHandle;
	if (!WidgetClassPath.IsEmpty() && PreviewLifecycle != TEXT("remove"))
	{
		FString ResolveError;
		UClass* WidgetClass = ResolveWidgetClass(WidgetClassPath, ResolveError);
		if (!WidgetClass)
		{
			return FBridgeToolResult::Error(ResolveError);
		}

		CreatedWidget = CreateWidget<UUserWidget>(PIEWorld, WidgetClass);
		if (!CreatedWidget)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("CreateWidget failed for %s"), *WidgetClassPath));
		}
		CreatedWidget->AddToViewport(ViewportZOrder);
		PreviewHandle = FWidgetPreviewRegistry::MakeHandle(WidgetClassPath, PIEWorld);
		FWidgetPreviewRegistry::RegisterPreview(PIEWorld, CreatedWidget, PreviewHandle);
	}

	TArray<UUserWidget*> RootWidgets = CollectPIEWidgets(PIEWorld);
	if (CreatedWidget && !RootWidgets.Contains(CreatedWidget))
	{
		RootWidgets.Add(CreatedWidget);
	}

	auto BuildRootLookupDiagnostics = [&]() -> FString
	{
		TArray<FString> RootNames;
		for (UUserWidget* Root : RootWidgets)
		{
			if (Root)
			{
				RootNames.Add(Root->GetName());
			}
		}

		TArray<FWidgetPreviewSummary> PreviewSummaries;
		FWidgetPreviewRegistry::ListPreviewsForWorld(PIEWorld, PreviewSummaries);
		TArray<FString> PreviewNames;
		for (const FWidgetPreviewSummary& Summary : PreviewSummaries)
		{
			PreviewNames.Add(FString::Printf(
				TEXT("%s handle=%s class=%s"),
				*Summary.WidgetName,
				*Summary.Handle,
				*Summary.WidgetClass));
		}

		return FString::Printf(
			TEXT(" Current root widgets: [%s]. current tool previews: [%s]."),
			*FString::Join(RootNames, TEXT(", ")),
			*FString::Join(PreviewNames, TEXT(", ")));
	};

	auto Fail = [&](const FString& Message) -> FBridgeToolResult
	{
		if (CreatedWidget && bRemovePreview)
		{
			// Registry cleanup calls RemoveFromParent for every tool-owned preview in this PIE world.
			FWidgetPreviewRegistry::RemovePreviewsForWorld(PIEWorld);
		}
		return FBridgeToolResult::Error(Message);
	};

	FString RootWidgetName = GetStringArgOrDefault(Arguments, TEXT("root_widget"));
	UWidget* SearchRoot = nullptr;
	if (!RootWidgetName.IsEmpty())
	{
		for (UUserWidget* Root : RootWidgets)
		{
			SearchRoot = FindWidgetByName(Root, RootWidgetName);
			if (SearchRoot)
			{
				break;
			}
		}
		if (!SearchRoot)
		{
			return Fail(FString::Printf(
				TEXT("Runtime root widget not found: %s.%s"),
				*RootWidgetName,
				*BuildRootLookupDiagnostics()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Checks;
	auto AddCheck = [&Checks](const FString& Kind, const FString& Target, bool bPass, const FString& Message)
	{
		TSharedPtr<FJsonObject> Check = MakeShareable(new FJsonObject);
		Check->SetStringField(TEXT("kind"), Kind);
		Check->SetStringField(TEXT("target"), Target);
		Check->SetBoolField(TEXT("pass"), bPass);
		if (!Message.IsEmpty())
		{
			Check->SetStringField(TEXT("message"), Message);
		}
		Checks.Add(MakeShareable(new FJsonValueObject(Check)));
	};

	auto FindInRoots = [&](const FString& Name) -> UWidget*
	{
		if (SearchRoot)
		{
			return FindWidgetByName(SearchRoot, Name);
		}
		for (UUserWidget* Root : RootWidgets)
		{
			if (UWidget* Found = FindWidgetByName(Root, Name))
			{
				return Found;
			}
		}
		return nullptr;
	};

	const TArray<TSharedPtr<FJsonValue>>* ExpectedWidgets = nullptr;
	if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("expected_widgets"), ExpectedWidgets) && ExpectedWidgets)
	{
		for (const TSharedPtr<FJsonValue>& Item : *ExpectedWidgets)
		{
			const FString Name = Item.IsValid() ? Item->AsString() : TEXT("");
			const bool bFound = !Name.IsEmpty() && FindInRoots(Name) != nullptr;
			AddCheck(TEXT("expected_widget"), Name, bFound, bFound ? TEXT("") : TEXT("widget not found"));
			if (!bFound)
			{
				return Fail(FString::Printf(TEXT("Expected widget not found: %s"), *Name));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ExpectedText = nullptr;
	if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("expected_text"), ExpectedText) && ExpectedText)
	{
		for (const TSharedPtr<FJsonValue>& Item : *ExpectedText)
		{
			const FString Text = Item.IsValid() ? Item->AsString() : TEXT("");
			UTextBlock* FoundText = nullptr;
			if (SearchRoot)
			{
				FoundText = FindTextBlockByText(SearchRoot, Text);
			}
			else
			{
				for (UUserWidget* Root : RootWidgets)
				{
					FoundText = FindTextBlockByText(Root, Text);
					if (FoundText)
					{
						break;
					}
				}
			}
			AddCheck(TEXT("expected_text"), Text, FoundText != nullptr, FoundText ? TEXT("") : TEXT("text not found"));
			if (!FoundText)
			{
				return Fail(FString::Printf(TEXT("Expected text not found: %s"), *Text));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ClickSequence = nullptr;
	if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("click_sequence"), ClickSequence) && ClickSequence)
	{
		for (int32 Index = 0; Index < ClickSequence->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* StepObjPtr = nullptr;
			if (!(*ClickSequence)[Index].IsValid() || !(*ClickSequence)[Index]->TryGetObject(StepObjPtr) || !StepObjPtr || !StepObjPtr->IsValid())
			{
				return Fail(FString::Printf(TEXT("click_sequence[%d] must be an object"), Index));
			}

			const TSharedPtr<FJsonObject>& StepObj = *StepObjPtr;
			FString ButtonName;
			if (!StepObj->TryGetStringField(TEXT("button"), ButtonName) || ButtonName.IsEmpty())
			{
				return Fail(FString::Printf(TEXT("click_sequence[%d].button is required"), Index));
			}

			UButton* Button = Cast<UButton>(FindInRoots(ButtonName));
			if (!Button)
			{
				return Fail(FString::Printf(TEXT("Click target not found or not a Button: %s"), *ButtonName));
			}
			if (!Button->GetIsEnabled() || !IsRuntimeVisible(Button))
			{
				return Fail(FString::Printf(TEXT("Click target is disabled or hidden: %s"), *ButtonName));
			}

			Button->OnClicked.Broadcast();
			AddCheck(TEXT("click"), ButtonName, true, TEXT("OnClicked.Broadcast invoked"));

			FString ExpectedVisible;
			if (StepObj->TryGetStringField(TEXT("expect_visible_widget"), ExpectedVisible) && !ExpectedVisible.IsEmpty())
			{
				UWidget* Expected = FindInRoots(ExpectedVisible);
				const bool bVisible = Expected && IsRuntimeVisible(Expected);
				AddCheck(TEXT("expect_visible_widget"), ExpectedVisible, bVisible, bVisible ? TEXT("") : TEXT("widget not visible"));
				if (!bVisible)
				{
					return Fail(FString::Printf(TEXT("Expected widget is not visible after click: %s"), *ExpectedVisible));
				}
			}

			FString SwitcherName;
			if (StepObj->TryGetStringField(TEXT("switcher"), SwitcherName) && !SwitcherName.IsEmpty())
			{
				UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(FindInRoots(SwitcherName));
				if (!Switcher)
				{
					return Fail(FString::Printf(TEXT("Expected WidgetSwitcher not found: %s"), *SwitcherName));
				}

				double ExpectedIndex = 0.0;
				if (StepObj->TryGetNumberField(TEXT("expect_active_index"), ExpectedIndex))
				{
					const int32 ActiveIndex = Switcher->GetActiveWidgetIndex();
					const bool bMatch = ActiveIndex == static_cast<int32>(ExpectedIndex);
					AddCheck(TEXT("expect_active_index"), SwitcherName, bMatch,
						FString::Printf(TEXT("active_index=%d expected=%d"), ActiveIndex, static_cast<int32>(ExpectedIndex)));
					if (!bMatch)
					{
						return Fail(FString::Printf(TEXT("WidgetSwitcher active index mismatch for %s"), *SwitcherName));
					}
				}

				FString ExpectedActiveWidget;
				if (StepObj->TryGetStringField(TEXT("expect_active_widget"), ExpectedActiveWidget) && !ExpectedActiveWidget.IsEmpty())
				{
					UWidget* ActiveWidget = Switcher->GetActiveWidget();
					const bool bMatch = ActiveWidget && ActiveWidget->GetName().Equals(ExpectedActiveWidget, ESearchCase::IgnoreCase);
					AddCheck(TEXT("expect_active_widget"), SwitcherName, bMatch,
						ActiveWidget ? ActiveWidget->GetName() : TEXT("no active widget"));
					if (!bMatch)
					{
						return Fail(FString::Printf(TEXT("WidgetSwitcher active widget mismatch for %s"), *SwitcherName));
					}
				}

				FString ExpectedActiveClass;
				if (StepObj->TryGetStringField(TEXT("expect_active_class"), ExpectedActiveClass) && !ExpectedActiveClass.IsEmpty())
				{
					UWidget* ActiveWidget = Switcher->GetActiveWidget();
					const FString ActiveClass = ActiveWidget && ActiveWidget->GetClass() ? ActiveWidget->GetClass()->GetName() : TEXT("");
					const bool bMatch = ActiveClass.Contains(ExpectedActiveClass, ESearchCase::IgnoreCase);
					AddCheck(TEXT("expect_active_class"), SwitcherName, bMatch, ActiveClass);
					if (!bMatch)
					{
						return Fail(FString::Printf(TEXT("WidgetSwitcher active class mismatch for %s"), *SwitcherName));
					}
				}
			}
		}
	}

	if (CreatedWidget && bRemovePreview)
	{
		RemovedExistingPreviews += FWidgetPreviewRegistry::RemovePreviewsForWorld(PIEWorld, &RemovedPreviewHandles);
	}

	TArray<TSharedPtr<FJsonValue>> RootSummaries;
	for (UUserWidget* Root : RootWidgets)
	{
		if (!Root)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetStringField(TEXT("name"), Root->GetName());
		Summary->SetStringField(TEXT("class"), Root->GetClass()->GetName());
		RootSummaries.Add(MakeShareable(new FJsonValueObject(Summary)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("pie_index"), PieIndex);
	Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	Result->SetBoolField(TEXT("created_preview_widget"), CreatedWidget != nullptr);
	Result->SetBoolField(TEXT("removed_preview"), CreatedWidget != nullptr && bRemovePreview);
	Result->SetStringField(TEXT("preview_lifecycle"), PreviewLifecycle);
	Result->SetNumberField(TEXT("removed_existing_previews"), RemovedExistingPreviews);
	Result->SetNumberField(TEXT("tool_preview_count"), FWidgetPreviewRegistry::CountPreviewsForWorld(PIEWorld));
	if (!PreviewHandle.IsEmpty())
	{
		Result->SetStringField(TEXT("preview_handle"), PreviewHandle);
	}
	if (RemovedPreviewHandles.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RemovedHandleValues;
		for (const FString& RemovedHandle : RemovedPreviewHandles)
		{
			RemovedHandleValues.Add(MakeShareable(new FJsonValueString(RemovedHandle)));
		}
		Result->SetArrayField(TEXT("removed_preview_handles"), RemovedHandleValues);
	}
	Result->SetArrayField(TEXT("root_widgets"), RootSummaries);
	Result->SetArrayField(TEXT("checks"), Checks);
	if (bCaptureAfter)
	{
		Result->SetBoolField(TEXT("capture_after"), true);
		Result->SetStringField(TEXT("capture_command"), TEXT("soft-ue-cli capture-viewport --source game --output file"));
	}

	return FBridgeToolResult::Json(Result);
}

UWorld* UVerifyUMGWorkflowTool::GetPIEWorldByIndex(int32 Index, int32& OutTotalCount) const
{
	OutTotalCount = 0;
	UWorld* FoundWorld = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.World())
		{
			if (OutTotalCount == Index)
			{
				FoundWorld = WorldContext.World();
			}
			OutTotalCount++;
		}
	}
	return FoundWorld;
}

UClass* UVerifyUMGWorkflowTool::ResolveWidgetClass(const FString& WidgetClassPath, FString& OutError) const
{
	if (UClass* LoadedClass = LoadClass<UUserWidget>(nullptr, *WidgetClassPath))
	{
		return LoadedClass;
	}

	FString LoadError;
	if (UWidgetBlueprint* WidgetBP = FBridgeAssetModifier::LoadAssetByPath<UWidgetBlueprint>(WidgetClassPath, LoadError))
	{
		if (WidgetBP->GeneratedClass && WidgetBP->GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
		{
			return WidgetBP->GeneratedClass;
		}
	}

	if (UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetClassPath))
	{
		if (WidgetBP->GeneratedClass && WidgetBP->GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
		{
			return WidgetBP->GeneratedClass;
		}
	}

	OutError = FString::Printf(TEXT("Widget class or WidgetBlueprint not found: %s"), *WidgetClassPath);
	return nullptr;
}

TArray<UUserWidget*> UVerifyUMGWorkflowTool::CollectPIEWidgets(UWorld* PIEWorld) const
{
	TArray<UUserWidget*> Result;
	ForEachObjectOfClass(UUserWidget::StaticClass(), [&](UObject* Obj)
	{
		UUserWidget* Widget = Cast<UUserWidget>(Obj);
		if (Widget
			&& Widget->GetWorld() == PIEWorld
			&& !Widget->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& Widget->IsInViewport()
			&& !Widget->GetParent())
		{
			Result.Add(Widget);
		}
	});
	FWidgetPreviewRegistry::CollectPreviewWidgetsForWorld(PIEWorld, Result);
	return Result;
}

UWidget* UVerifyUMGWorkflowTool::FindWidgetByName(UWidget* Root, const FString& Name) const
{
	if (!Root || Name.IsEmpty())
	{
		return nullptr;
	}

	if (Root->GetName().Equals(Name, ESearchCase::IgnoreCase))
	{
		return Root;
	}

	if (UUserWidget* UserWidget = Cast<UUserWidget>(Root))
	{
		if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
		{
			if (UWidget* Found = FindWidgetByName(UserWidget->WidgetTree->RootWidget, Name))
			{
				return Found;
			}
		}
	}

	if (UContentWidget* ContentWidget = Cast<UContentWidget>(Root))
	{
		if (UWidget* Found = FindWidgetByName(ContentWidget->GetContent(), Name))
		{
			return Found;
		}
	}

	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Root))
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			if (UWidget* Found = FindWidgetByName(PanelWidget->GetChildAt(i), Name))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UTextBlock* UVerifyUMGWorkflowTool::FindTextBlockByText(UWidget* Root, const FString& Text) const
{
	if (!Root)
	{
		return nullptr;
	}

	if (UTextBlock* TextBlock = Cast<UTextBlock>(Root))
	{
		if (TextBlock->GetText().ToString().Equals(Text, ESearchCase::CaseSensitive))
		{
			return TextBlock;
		}
	}

	if (UUserWidget* UserWidget = Cast<UUserWidget>(Root))
	{
		if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
		{
			if (UTextBlock* Found = FindTextBlockByText(UserWidget->WidgetTree->RootWidget, Text))
			{
				return Found;
			}
		}
	}

	if (UContentWidget* ContentWidget = Cast<UContentWidget>(Root))
	{
		if (UTextBlock* Found = FindTextBlockByText(ContentWidget->GetContent(), Text))
		{
			return Found;
		}
	}

	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Root))
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			if (UTextBlock* Found = FindTextBlockByText(PanelWidget->GetChildAt(i), Text))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

bool UVerifyUMGWorkflowTool::IsRuntimeVisible(UWidget* Widget) const
{
	if (!Widget)
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	return Visibility == ESlateVisibility::Visible
		|| Visibility == ESlateVisibility::HitTestInvisible
		|| Visibility == ESlateVisibility::SelfHitTestInvisible;
}
