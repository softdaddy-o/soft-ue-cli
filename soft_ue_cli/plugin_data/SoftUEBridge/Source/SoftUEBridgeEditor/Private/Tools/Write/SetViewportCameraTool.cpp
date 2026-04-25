// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/SetViewportCameraTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

FString USetViewportCameraTool::GetToolDescription() const
{
	return TEXT("Set the editor viewport camera position, rotation, and view mode. "
		"Supports perspective and orthographic views (Top, Front, Right, etc.). "
		"Use 'preset' for quick top-down/front/side views, or set location/rotation manually.");
}

TMap<FString, FBridgeSchemaProperty> USetViewportCameraTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Location;
	Location.Type = TEXT("array");
	Location.Description = TEXT("Camera position [x, y, z]. Ignored for orthographic presets.");
	Location.bRequired = false;
	Schema.Add(TEXT("location"), Location);

	FBridgeSchemaProperty Rotation;
	Rotation.Type = TEXT("array");
	Rotation.Description = TEXT("Camera rotation [pitch, yaw, roll] in degrees. Ignored for orthographic presets.");
	Rotation.bRequired = false;
	Schema.Add(TEXT("rotation"), Rotation);

	FBridgeSchemaProperty Preset;
	Preset.Type = TEXT("string");
	Preset.Description = TEXT("Camera preset: 'top', 'bottom', 'front', 'back', 'left', 'right', or 'perspective'. "
		"Orthographic presets switch to ortho view. 'perspective' switches back.");
	Preset.bRequired = false;
	Schema.Add(TEXT("preset"), Preset);

	FBridgeSchemaProperty OrthoWidth;
	OrthoWidth.Type = TEXT("number");
	OrthoWidth.Description = TEXT("Orthographic zoom factor. Larger = more zoomed out. "
		"Typical values: 1000 (tight), 10000 (medium), 50000 (wide), 200000 (very wide). "
		"UE default is ~175000. Only applies to orthographic views.");
	OrthoWidth.bRequired = false;
	Schema.Add(TEXT("ortho_width"), OrthoWidth);

	return Schema;
}

TArray<FString> USetViewportCameraTool::GetRequiredParams() const
{
	return {};
}

FBridgeToolResult USetViewportCameraTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Get the first level viewport
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<ILevelEditor> LevelEditorInstance = LevelEditor.GetFirstLevelEditor();
	if (!LevelEditorInstance.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No level editor found"));
	}

	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorInstance->GetActiveViewportInterface();
	if (!ActiveViewport.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No active viewport found"));
	}

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();

	const FString Preset = GetStringArgOrDefault(Arguments, TEXT("preset"));
	const float OrthoWidth = GetFloatArgOrDefault(Arguments, TEXT("ortho_width"), 0.0f);

	// Handle presets
	if (!Preset.IsEmpty())
	{
		if (Preset.Equals(TEXT("top"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoXY);
		}
		else if (Preset.Equals(TEXT("bottom"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoNegativeXY);
		}
		else if (Preset.Equals(TEXT("front"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoXZ);
		}
		else if (Preset.Equals(TEXT("back"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoNegativeXZ);
		}
		else if (Preset.Equals(TEXT("left"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoYZ);
		}
		else if (Preset.Equals(TEXT("right"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_OrthoNegativeYZ);
		}
		else if (Preset.Equals(TEXT("perspective"), ESearchCase::IgnoreCase))
		{
			ViewportClient.SetViewportType(LVT_Perspective);
		}
		else
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Unknown preset '%s'. Use: top, bottom, front, back, left, right, perspective"), *Preset));
		}
	}

	// Apply ortho zoom if provided (must be after SetViewportType)
	// OrthoZoom in UE is a zoom factor — larger values zoom out.
	// Typical range: 1 (very zoomed in) to 500000+ (very zoomed out).
	// Default is ~175000. Use ortho_width as a direct zoom value.
	if (OrthoWidth > 0.0f)
	{
		ViewportClient.SetOrthoZoom(OrthoWidth);
	}

	// Apply custom location if provided
	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (Arguments->TryGetArrayField(TEXT("location"), LocArr) && LocArr->Num() >= 3)
	{
		FVector Location;
		Location.X = (*LocArr)[0]->AsNumber();
		Location.Y = (*LocArr)[1]->AsNumber();
		Location.Z = (*LocArr)[2]->AsNumber();
		ViewportClient.SetViewLocation(Location);
	}

	// Apply custom rotation if provided
	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	if (Arguments->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() >= 3)
	{
		FRotator Rotation;
		Rotation.Pitch = (*RotArr)[0]->AsNumber();
		Rotation.Yaw   = (*RotArr)[1]->AsNumber();
		Rotation.Roll  = (*RotArr)[2]->AsNumber();
		ViewportClient.SetViewRotation(Rotation);
	}

	// Force viewport redraw
	ViewportClient.Invalidate();

	// Build result
	FVector FinalLoc = ViewportClient.GetViewLocation();
	FRotator FinalRot = ViewportClient.GetViewRotation();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> LocJson;
	LocJson.Add(MakeShared<FJsonValueNumber>(FinalLoc.X));
	LocJson.Add(MakeShared<FJsonValueNumber>(FinalLoc.Y));
	LocJson.Add(MakeShared<FJsonValueNumber>(FinalLoc.Z));
	Result->SetArrayField(TEXT("location"), LocJson);

	TArray<TSharedPtr<FJsonValue>> RotJson;
	RotJson.Add(MakeShared<FJsonValueNumber>(FinalRot.Pitch));
	RotJson.Add(MakeShared<FJsonValueNumber>(FinalRot.Yaw));
	RotJson.Add(MakeShared<FJsonValueNumber>(FinalRot.Roll));
	Result->SetArrayField(TEXT("rotation"), RotJson);

	const bool bOrtho = ViewportClient.IsOrtho();
	Result->SetStringField(TEXT("view_type"), bOrtho ? TEXT("orthographic") : TEXT("perspective"));
	if (!Preset.IsEmpty())
	{
		Result->SetStringField(TEXT("preset"), Preset);
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-viewport-camera: %s at [%.0f, %.0f, %.0f]"),
		bOrtho ? TEXT("ortho") : TEXT("perspective"), FinalLoc.X, FinalLoc.Y, FinalLoc.Z);
	return FBridgeToolResult::Json(Result);
}
