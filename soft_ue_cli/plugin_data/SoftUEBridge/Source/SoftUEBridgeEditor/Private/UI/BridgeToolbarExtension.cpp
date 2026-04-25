// Copyright soft-ue-expert. All Rights Reserved.

#include "UI/BridgeToolbarExtension.h"
#include "Subsystem/SoftUEBridgeSubsystem.h"
#include "SoftUEBridgeEditorModule.h"
#include "ToolMenus.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/SlateTypes.h"
#include "Styling/AppStyle.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "SoftUEBridge"

bool FBridgeToolbarExtension::bIsInitialized = false;

void FBridgeToolbarExtension::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FBridgeToolbarExtension::RegisterStatusBarExtension));

	bIsInitialized = true;
}

void FBridgeToolbarExtension::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus)
	{
		ToolMenus->RemoveSection(TEXT("LevelEditor.StatusBar.ToolBar"), TEXT("SoftUEBridge"));
	}

	bIsInitialized = false;
}

void FBridgeToolbarExtension::RegisterStatusBarExtension()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("Bridge status bar: UToolMenus not available"));
		return;
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Bridge status bar: Registering extension..."));

	UToolMenu* StatusBar = ToolMenus->ExtendMenu(TEXT("LevelEditor.StatusBar.ToolBar"));
	if (StatusBar)
	{
		FToolMenuSection& Section = StatusBar->FindOrAddSection("SoftUEBridge");

		Section.AddEntry(FToolMenuEntry::InitWidget(
			"BridgeStatus",
			CreateStatusWidget(),
			FText::GetEmpty(),
			true  // bNoIndent
		));

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Bridge status bar: Registered on LevelEditor.StatusBar.ToolBar"));
	}
	else
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("Bridge status bar: LevelEditor.StatusBar.ToolBar menu not found"));
	}
}

TSharedRef<SWidget> FBridgeToolbarExtension::CreateStatusWidget()
{
	return SNew(SButton)
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
		.OnClicked_Static(&FBridgeToolbarExtension::OnStatusButtonClicked)
		.ToolTipText_Static(&FBridgeToolbarExtension::GetStatusTooltip)
		.ContentPadding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(10.0f)
				.HeightOverride(10.0f)
				[
					SNew(SImage)
					.Image_Static(&FBridgeToolbarExtension::GetStatusBrush)
					.ColorAndOpacity_Static(&FBridgeToolbarExtension::GetStatusColor)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BridgeLabel", "Bridge"))
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
			]
		];
}

const FSlateBrush* FBridgeToolbarExtension::GetStatusBrush()
{
	return FAppStyle::Get().GetBrush("Icons.FilledCircle");
}

FSlateColor FBridgeToolbarExtension::GetStatusColor()
{
	USoftUEBridgeSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FSlateColor(FLinearColor::Gray);
	}

	return Subsystem->IsServerRunning()
		? FSlateColor(FLinearColor::Green)
		: FSlateColor(FLinearColor::Red);
}

FText FBridgeToolbarExtension::GetStatusTooltip()
{
	USoftUEBridgeSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return LOCTEXT("BridgeNotAvailableTooltip", "SoftUEBridge: Not Available");
	}

	// Plugin version never changes at runtime — cache it
	static FString CachedVersion;
	if (CachedVersion.IsEmpty())
	{
		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SoftUEBridge")))
		{
			CachedVersion = Plugin->GetDescriptor().VersionName;
		}
		if (CachedVersion.IsEmpty())
		{
			CachedVersion = TEXT("Unknown");
		}
	}

	FString Status = Subsystem->IsServerRunning()
		? FString::Printf(TEXT("Running on port %d"), Subsystem->GetServerPort())
		: TEXT("Stopped");

	return FText::Format(
		LOCTEXT("BridgeTooltipFormat", "SoftUEBridge v{0}\nStatus: {1}\n\nClick to restart server"),
		FText::FromString(CachedVersion),
		FText::FromString(Status));
}

FReply FBridgeToolbarExtension::OnStatusButtonClicked()
{
	USoftUEBridgeSubsystem* Subsystem = GetSubsystem();
	if (Subsystem)
	{
		Subsystem->RestartServer();
	}

	return FReply::Handled();
}

USoftUEBridgeSubsystem* FBridgeToolbarExtension::GetSubsystem()
{
	if (GEngine)
	{
		return GEngine->GetEngineSubsystem<USoftUEBridgeSubsystem>();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
