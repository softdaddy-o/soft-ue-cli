// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/AddWidgetTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "ScopedTransaction.h"

namespace
{
	UClass* ResolveBuiltInWidgetClass(const FString& WidgetClass)
	{
		if (WidgetClass.Equals(TEXT("Button"), ESearchCase::IgnoreCase))
		{
			return UButton::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("TextBlock"), ESearchCase::IgnoreCase))
		{
			return UTextBlock::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("Image"), ESearchCase::IgnoreCase))
		{
			return UImage::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("ProgressBar"), ESearchCase::IgnoreCase))
		{
			return UProgressBar::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("CanvasPanel"), ESearchCase::IgnoreCase))
		{
			return UCanvasPanel::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("VerticalBox"), ESearchCase::IgnoreCase))
		{
			return UVerticalBox::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("HorizontalBox"), ESearchCase::IgnoreCase))
		{
			return UHorizontalBox::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("Border"), ESearchCase::IgnoreCase))
		{
			return UBorder::StaticClass();
		}
		if (WidgetClass.Equals(TEXT("Overlay"), ESearchCase::IgnoreCase))
		{
			return UOverlay::StaticClass();
		}

		return nullptr;
	}

	FString BuildGeneratedWidgetClassPath(const FString& WidgetClass)
	{
		FString PackagePath;
		FString ObjectName;
		if (WidgetClass.Split(TEXT("."), &PackagePath, &ObjectName))
		{
			if (ObjectName.EndsWith(TEXT("_C")))
			{
				return WidgetClass;
			}
			return PackagePath + TEXT(".") + ObjectName + TEXT("_C");
		}

		int32 LastSlashIndex = INDEX_NONE;
		if (!WidgetClass.FindLastChar(TEXT('/'), LastSlashIndex))
		{
			return WidgetClass;
		}

		const FString AssetName = WidgetClass.Mid(LastSlashIndex + 1);
		return WidgetClass + TEXT(".") + AssetName + TEXT("_C");
	}

	UClass* ResolveWidgetClass(
		const FString& WidgetClass,
		FString& OutResolvedWidgetClassPath,
		bool& bOutChildUserWidgetClass)
	{
		OutResolvedWidgetClassPath.Reset();
		bOutChildUserWidgetClass = false;

		UClass* WClass = ResolveBuiltInWidgetClass(WidgetClass);
		if (!WClass && WidgetClass.StartsWith(TEXT("/")))
		{
			WClass = LoadClass<UWidget>(nullptr, *WidgetClass);
			if (!WClass)
			{
				const FString GeneratedClassPath = BuildGeneratedWidgetClassPath(WidgetClass);
				WClass = LoadClass<UWidget>(nullptr, *GeneratedClassPath);
			}

			if (!WClass)
			{
				FString LoadError;
				if (UWidgetBlueprint* LoadedWidgetBP = FBridgeAssetModifier::LoadAssetByPath<UWidgetBlueprint>(WidgetClass, LoadError))
				{
					WClass = LoadedWidgetBP->GeneratedClass;
				}
				else if (UWidgetBlueprint* DirectWidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetClass))
				{
					WClass = DirectWidgetBP->GeneratedClass;
				}
			}
		}

		if (!WClass)
		{
			WClass = FindFirstObject<UClass>(*WidgetClass, EFindFirstObjectOptions::ExactClass);
			if (!WClass)
			{
				WClass = FindFirstObject<UClass>(*(TEXT("U") + WidgetClass), EFindFirstObjectOptions::ExactClass);
			}
		}

		if (!WClass || !WClass->IsChildOf(UWidget::StaticClass()))
		{
			return nullptr;
		}

		OutResolvedWidgetClassPath = WClass->GetPathName();
		bOutChildUserWidgetClass = WClass->IsChildOf(UUserWidget::StaticClass());
		return WClass;
	}
}

FString UAddWidgetTool::GetToolDescription() const
{
	return TEXT("Add a widget to a WidgetBlueprint tree.");
}

TMap<FString, FBridgeSchemaProperty> UAddWidgetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("WidgetBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty WidgetClass;
	WidgetClass.Type = TEXT("string");
	WidgetClass.Description = TEXT("Widget class: 'Button', 'TextBlock', 'Image', 'ProgressBar', 'CanvasPanel', 'VerticalBox', 'HorizontalBox', 'Border', 'Overlay'");
	WidgetClass.bRequired = true;
	Schema.Add(TEXT("widget_class"), WidgetClass);

	FBridgeSchemaProperty WidgetName;
	WidgetName.Type = TEXT("string");
	WidgetName.Description = TEXT("Name for the new widget");
	WidgetName.bRequired = true;
	Schema.Add(TEXT("widget_name"), WidgetName);

	FBridgeSchemaProperty ParentWidget;
	ParentWidget.Type = TEXT("string");
	ParentWidget.Description = TEXT("Parent widget name. If empty, adds to root.");
	ParentWidget.bRequired = false;
	Schema.Add(TEXT("parent_widget"), ParentWidget);

	return Schema;
}

TArray<FString> UAddWidgetTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("widget_class"), TEXT("widget_name") };
}

FBridgeToolResult UAddWidgetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString WidgetClass = GetStringArgOrDefault(Arguments, TEXT("widget_class"));
	FString WidgetName = GetStringArgOrDefault(Arguments, TEXT("widget_name"));
	FString ParentWidget = GetStringArgOrDefault(Arguments, TEXT("parent_widget"));

	if (AssetPath.IsEmpty() || WidgetClass.IsEmpty() || WidgetName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, widget_class, and widget_name are required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-widget: %s (%s) to %s"), *WidgetName, *WidgetClass, *AssetPath);

	// Load the WidgetBlueprint
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

	FString ResolvedWidgetClassPath;
	bool bChildUserWidgetClass = false;
	UClass* WClass = ResolveWidgetClass(WidgetClass, ResolvedWidgetClassPath, bChildUserWidgetClass);
	if (!WClass)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Widget class not found: %s"), *WidgetClass));
	}

	// Find parent widget
	UWidget* ParentWidgetObject = nullptr;
	bool bSetAsRootWidget = false;

	if (!ParentWidget.IsEmpty())
	{
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName().Equals(ParentWidget, ESearchCase::IgnoreCase))
			{
				ParentWidgetObject = Widget;
			}
		});

		if (!ParentWidgetObject)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Parent widget not found: %s"), *ParentWidget));
		}

		if (!Cast<UPanelWidget>(ParentWidgetObject) && !Cast<UContentWidget>(ParentWidgetObject))
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Parent widget does not support child attachment: %s"),
				*ParentWidget));
		}

		if (UContentWidget* ContentParent = Cast<UContentWidget>(ParentWidgetObject))
		{
			if (ContentParent->GetContent())
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("Parent widget already contains a child: %s"),
					*ParentWidget));
			}
		}
	}
	else
	{
		ParentWidgetObject = Cast<UPanelWidget>(WidgetTree->RootWidget);
		bSetAsRootWidget = (ParentWidgetObject == nullptr);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddWidget", "Add {0} to {1}"),
			FText::FromString(WidgetName), FText::FromString(AssetPath)));

	FBridgeAssetModifier::MarkModified(WidgetBP);

	// Create the widget
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WClass, FName(*WidgetName));

	if (!NewWidget)
	{
		return FBridgeToolResult::Error(TEXT("Failed to create widget"));
	}

	// Add to parent or set as root
	if (UPanelWidget* PanelParent = Cast<UPanelWidget>(ParentWidgetObject))
	{
		PanelParent->AddChild(NewWidget);
	}
	else if (UContentWidget* ContentParent = Cast<UContentWidget>(ParentWidgetObject))
	{
		ContentParent->SetContent(NewWidget);
	}
	else if (bSetAsRootWidget)
	{
		WidgetTree->RootWidget = NewWidget;
	}
	else
	{
		return FBridgeToolResult::Error(TEXT("Failed to resolve widget attachment target"));
	}

	FBridgeAssetModifier::MarkPackageDirty(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("widget_name"), NewWidget->GetName());
	Result->SetStringField(TEXT("widget_class"), WClass->GetName());
	Result->SetStringField(TEXT("resolved_widget_class_path"), ResolvedWidgetClassPath);
	Result->SetBoolField(TEXT("child_user_widget_class"), bChildUserWidgetClass);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-widget: Added %s"), *NewWidget->GetName());

	return FBridgeToolResult::Json(Result);
}
