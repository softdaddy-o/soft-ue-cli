// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/OpenAssetTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "LevelEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/DataTable.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/OutputDevice.h"

namespace
{
	/**
	 * Replaces GError for the duration of a scope and restores it on destruction.
	 * When loading a new level, UEditorEngine::CheckForWorldGCLeaks fires a Fatal
	 * log through GError if any world-owning plugin (e.g. Niagara) hasn't yet
	 * released its reference.  The level switch itself completes successfully —
	 * the "leak" is a timing artefact — so suppressing the fatal allows the new
	 * world to load, after which the plugin updates its reference normally on the
	 * next tick.
	 */
	class FSuppressMapLoadFatalDevice : public FOutputDeviceError
	{
	public:
		TArray<FString> SuppressedMessages;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Fatal)
			{
				UE_LOG(LogSoftUEBridgeEditor, Warning,
					TEXT("open-asset: suppressed fatal during map load: %s"), V);
				SuppressedMessages.Add(FString(V));
			}
		}

		virtual void HandleError() override {}  // no-op: suppressing the error

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnPanicThread() const override { return true; }
	};

	struct FGErrorGuard
	{
		FOutputDeviceError* Saved;
		explicit FGErrorGuard(FOutputDeviceError* Replacement) : Saved(GError) { GError = Replacement; }
		~FGErrorGuard() { GError = Saved; }
	};
}

TMap<FString, FBridgeSchemaProperty> UOpenAssetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to open (e.g., /Game/Blueprints/BP_Player). Mutually exclusive with window_name.");
	AssetPath.bRequired = false;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty WindowName;
	WindowName.Type = TEXT("string");
	WindowName.Description = TEXT("Editor window/tab name to open (e.g., OutputLog, ContentBrowser). Mutually exclusive with asset_path.");
	WindowName.bRequired = false;
	Schema.Add(TEXT("window_name"), WindowName);

	FBridgeSchemaProperty BringToFront;
	BringToFront.Type = TEXT("boolean");
	BringToFront.Description = TEXT("Whether to bring the editor window to front (default: true)");
	BringToFront.bRequired = false;
	Schema.Add(TEXT("bring_to_front"), BringToFront);

	return Schema;
}

TArray<FString> UOpenAssetTool::GetRequiredParams() const
{
	// Neither parameter is strictly required, but at least one must be provided
	// We'll validate this in Execute()
	return {};
}

FBridgeToolResult UOpenAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	FString WindowName = GetStringArgOrDefault(Arguments, TEXT("window_name"), TEXT(""));
	bool bBringToFront = GetBoolArgOrDefault(Arguments, TEXT("bring_to_front"), true);

	// Validate that exactly one mode is specified
	if (AssetPath.IsEmpty() && WindowName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Either 'asset_path' or 'window_name' must be provided"));
	}

	if (!AssetPath.IsEmpty() && !WindowName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Cannot specify both 'asset_path' and 'window_name'. Use one or the other."));
	}

	// Route to appropriate mode
	if (!AssetPath.IsEmpty())
	{
		return ExecuteAssetMode(AssetPath, bBringToFront);
	}
	else
	{
		return ExecuteWindowMode(WindowName, bBringToFront);
	}
}

FBridgeToolResult UOpenAssetTool::ExecuteAssetMode(const FString& AssetPath, bool bBringToFront)
{
	// Load the asset
	FString LoadError;
	UObject* Asset = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);

	if (!Asset)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to load asset '%s': %s"), *AssetPath, *LoadError));
	}

	// Get Asset Editor Subsystem
	UAssetEditorSubsystem* AssetEditorSubsystem =
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

	if (!AssetEditorSubsystem)
	{
		return FBridgeToolResult::Error(TEXT("Asset Editor Subsystem not available"));
	}

	// Check if already open
	bool bWasAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;

	// World assets are safer through the level-loading path than through the
	// generic asset-editor path. Before switching maps, give GC a couple of
	// passes to release subsystems still hanging on to the outgoing world.
	FSuppressMapLoadFatalDevice SuppressDevice;
	const bool bIsWorld = Asset->IsA<UWorld>();
	TOptional<FGErrorGuard> ErrorGuard;
	if (bIsWorld)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		ErrorGuard.Emplace(&SuppressDevice);
	}

	bool bSuccess = false;
	if (bIsWorld)
	{
		ULevelEditorSubsystem* LevelEditorSubsystem =
			GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (!LevelEditorSubsystem)
		{
			ErrorGuard.Reset();
			return FBridgeToolResult::Error(TEXT("Level Editor Subsystem not available"));
		}

		bSuccess = LevelEditorSubsystem->LoadLevel(AssetPath);
	}
	else
	{
		// Open the asset editor.
		// For World assets (levels) the load path calls CheckForWorldGCLeaks, which
		// raises a Fatal through GError if a plugin holds a lingering reference to
		// the outgoing world. The switch itself can still complete successfully, so
		// the fatal is suppressed for the duration of the map load.
		bSuccess = AssetEditorSubsystem->OpenEditorForAsset(Asset);
	}

	ErrorGuard.Reset(); // restore GError before any further work

	if (!bSuccess)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to open editor for asset '%s'. Asset type may not have an editor."),
			*AssetPath));
	}

	// Optionally bring window to front
	if (bBringToFront && FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ActiveWindow.IsValid())
		{
			ActiveWindow->BringToFront(true);
		}
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("mode"), TEXT("asset"));
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("editor_type"), bIsWorld ? TEXT("Level Editor") : GetEditorTypeName(Asset));
	Result->SetBoolField(TEXT("was_already_open"), bWasAlreadyOpen);
	if (SuppressDevice.SuppressedMessages.Num() > 0)
	{
		Result->SetStringField(TEXT("suppressed_fatal"),
			FString::Join(SuppressDevice.SuppressedMessages, TEXT("; ")));
	}
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("%s %s in %s"),
		bIsWorld ? TEXT("Loaded") : (bWasAlreadyOpen ? TEXT("Focused") : TEXT("Opened")),
		*Asset->GetName(),
		*(bIsWorld ? FString(TEXT("Level Editor")) : GetEditorTypeName(Asset))));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("open-asset (asset mode): %s '%s'"),
		bIsWorld ? TEXT("Loaded") : (bWasAlreadyOpen ? TEXT("Focused") : TEXT("Opened")), *AssetPath);

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UOpenAssetTool::ExecuteWindowMode(const FString& WindowName, bool bBringToFront)
{
	// Check if tab was already open BEFORE invoking
	TSharedPtr<SDockTab> ExistingTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*WindowName));
	bool bWasAlreadyOpen = ExistingTab.IsValid();

	// Try to invoke the tab using FGlobalTabmanager
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(*WindowName));

	if (!Tab.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to open window '%s'. Window may not exist or is not registered."),
			*WindowName));
	}

	// Optionally bring window to front
	if (bBringToFront)
	{
		TSharedPtr<SWindow> Window = Tab->GetParentWindow();
		if (Window.IsValid())
		{
			Window->BringToFront(true);
		}
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("mode"), TEXT("window"));
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("window_name"), WindowName);
	Result->SetStringField(TEXT("tab_label"), Tab->GetTabLabel().ToString());
	Result->SetBoolField(TEXT("was_already_open"), bWasAlreadyOpen);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("%s window '%s'"),
		bWasAlreadyOpen ? TEXT("Focused") : TEXT("Opened"),
		*Tab->GetTabLabel().ToString()));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("open-asset (window mode): %s '%s'"),
		bWasAlreadyOpen ? TEXT("Focused") : TEXT("Opened"), *WindowName);

	return FBridgeToolResult::Json(Result);
}

FString UOpenAssetTool::GetEditorTypeName(UObject* Asset) const
{
	if (!Asset)
	{
		return TEXT("Unknown");
	}

	if (Asset->IsA<UBlueprint>())
	{
		return TEXT("Blueprint Editor");
	}
	if (Asset->IsA<UMaterial>() || Asset->IsA<UMaterialInstance>())
	{
		return TEXT("Material Editor");
	}
	if (Asset->IsA<UDataTable>())
	{
		return TEXT("DataTable Editor");
	}
	if (Asset->IsA<UAnimSequence>())
	{
		return TEXT("Animation Editor");
	}
	if (Asset->IsA<UStaticMesh>())
	{
		return TEXT("Static Mesh Editor");
	}
	if (Asset->IsA<USkeletalMesh>())
	{
		return TEXT("Skeletal Mesh Editor");
	}
	if (Asset->IsA<UTexture2D>())
	{
		return TEXT("Texture Editor");
	}

	return FString::Printf(TEXT("%s Editor"), *Asset->GetClass()->GetName());
}
