// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Editor/CaptureScreenshotTool.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/BridgeToolRegistry.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "SoftUEBridgeEditorModule.h"
#include "Interfaces/IMainFrameModule.h"

namespace
{
	void CollectDockTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedPtr<SDockTab>>& OutTabs)
	{
		if (Widget->GetType() == FName(TEXT("SDockTab")))
		{
			OutTabs.Add(StaticCastSharedRef<SDockTab>(Widget));
		}

		FChildren* Children = Widget->GetChildren();
		if (!Children)
		{
			return;
		}

		for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
		{
			CollectDockTabs(Children->GetChildAt(ChildIndex), OutTabs);
		}
	}

	bool MatchesTabName(const FString& Candidate, const FString& RequestedName)
	{
		return Candidate.Equals(RequestedName, ESearchCase::IgnoreCase)
			|| Candidate.Contains(RequestedName, ESearchCase::IgnoreCase);
	}

	TSharedPtr<SDockTab> FindLiveTabByLabel(const FString& RequestedName)
	{
		TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			TArray<TSharedPtr<SDockTab>> Tabs;
			CollectDockTabs(Window->GetContent(), Tabs);

			for (const TSharedPtr<SDockTab>& Tab : Tabs)
			{
				if (Tab.IsValid() && MatchesTabName(Tab->GetTabLabel().ToString(), RequestedName))
				{
					return Tab;
				}
			}
		}

		return nullptr;
	}

	TSharedPtr<SWindow> FindTopLevelWindowByTitle(const FString& RequestedName)
	{
		TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (MatchesTabName(Window->GetTitle().ToString(), RequestedName))
			{
				return Window;
			}
		}

		return nullptr;
	}

	FString NormalizeColorMode(FString ColorMode)
	{
		ColorMode.TrimStartAndEndInline();
		ColorMode.ToLowerInline();
		return ColorMode.IsEmpty() ? TEXT("color") : ColorMode;
	}

	FIntPoint ResolveCaptureSize(
		int32 OriginalWidth,
		int32 OriginalHeight,
		float ScalePercent,
		int32 TargetWidth,
		int32 TargetHeight)
	{
		float Scale = 1.0f;
		if (TargetWidth > 0 && TargetHeight > 0)
		{
			Scale = FMath::Min(
				static_cast<float>(TargetWidth) / static_cast<float>(OriginalWidth),
				static_cast<float>(TargetHeight) / static_cast<float>(OriginalHeight));
		}
		else if (TargetWidth > 0)
		{
			Scale = static_cast<float>(TargetWidth) / static_cast<float>(OriginalWidth);
		}
		else if (TargetHeight > 0)
		{
			Scale = static_cast<float>(TargetHeight) / static_cast<float>(OriginalHeight);
		}
		else
		{
			Scale = ScalePercent / 100.0f;
		}

		Scale = FMath::Clamp(Scale, 0.0f, 1.0f);
		if (Scale >= 1.0f)
		{
			return FIntPoint(OriginalWidth, OriginalHeight);
		}

		return FIntPoint(
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(OriginalWidth) * Scale)),
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(OriginalHeight) * Scale)));
	}

	bool ResizeImage(TArray<FColor>& ImageData, int32& Width, int32& Height, const FIntPoint& TargetSize)
	{
		if (TargetSize.X == Width && TargetSize.Y == Height)
		{
			return true;
		}

		if (TargetSize.X <= 0 || TargetSize.Y <= 0 || Width <= 0 || Height <= 0)
		{
			return false;
		}

		TArray<FColor> ResizedData;
		ResizedData.SetNumUninitialized(TargetSize.X * TargetSize.Y);

		for (int32 Y = 0; Y < TargetSize.Y; ++Y)
		{
			const int32 SourceY = FMath::Clamp(
				FMath::FloorToInt((static_cast<float>(Y) + 0.5f) * static_cast<float>(Height) / static_cast<float>(TargetSize.Y)),
				0,
				Height - 1);
			for (int32 X = 0; X < TargetSize.X; ++X)
			{
				const int32 SourceX = FMath::Clamp(
					FMath::FloorToInt((static_cast<float>(X) + 0.5f) * static_cast<float>(Width) / static_cast<float>(TargetSize.X)),
					0,
					Width - 1);
				ResizedData[Y * TargetSize.X + X] = ImageData[SourceY * Width + SourceX];
			}
		}

		ImageData = MoveTemp(ResizedData);
		Width = TargetSize.X;
		Height = TargetSize.Y;
		return true;
	}

	void ApplyColorMode(TArray<FColor>& ImageData, const FString& ColorMode)
	{
		for (FColor& Pixel : ImageData)
		{
			Pixel.A = 255;
			if (ColorMode == TEXT("color"))
			{
				continue;
			}

			const uint8 Luma = static_cast<uint8>((77 * Pixel.R + 150 * Pixel.G + 29 * Pixel.B) >> 8);
			const uint8 Value = (ColorMode == TEXT("monochrome"))
				? (Luma >= 128 ? 255 : 0)
				: Luma;
			Pixel.R = Value;
			Pixel.G = Value;
			Pixel.B = Value;
		}
	}

	bool ApplyImageTransform(
		TArray<FColor>& ImageData,
		int32& Width,
		int32& Height,
		float ScalePercent,
		int32 TargetWidth,
		int32 TargetHeight,
		const FString& ColorMode,
		FString& OutColorMode,
		FString& OutError)
	{
		if (Width <= 0 || Height <= 0 || ImageData.Num() != Width * Height)
		{
			OutError = TEXT("Invalid image dimensions");
			return false;
		}

		if (ScalePercent <= 0.0f)
		{
			OutError = TEXT("'scale' must be greater than 0");
			return false;
		}

		if (TargetWidth < 0 || TargetHeight < 0)
		{
			OutError = TEXT("'width' and 'height' must be greater than or equal to 0");
			return false;
		}

		OutColorMode = NormalizeColorMode(ColorMode);
		if (OutColorMode != TEXT("color") &&
			OutColorMode != TEXT("grayscale") &&
			OutColorMode != TEXT("monochrome"))
		{
			OutError = FString::Printf(
				TEXT("Invalid color_mode '%s'. Valid values: color, grayscale, monochrome"),
				*ColorMode);
			return false;
		}

		const FIntPoint TargetSize = ResolveCaptureSize(Width, Height, ScalePercent, TargetWidth, TargetHeight);
		if (!ResizeImage(ImageData, Width, Height, TargetSize))
		{
			OutError = TEXT("Failed to resize screenshot");
			return false;
		}

		ApplyColorMode(ImageData, OutColorMode);
		return true;
	}

	void CopyJsonFields(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target)
	{
		if (!Source.IsValid() || !Target.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
		{
			Target->SetField(Pair.Key, Pair.Value);
		}
	}

	bool TryGetToolResultText(const FBridgeToolResult& ToolResult, FString& OutText)
	{
		if (ToolResult.Content.Num() != 1 || !ToolResult.Content[0].IsValid())
		{
			return false;
		}
		return ToolResult.Content[0]->TryGetStringField(TEXT("text"), OutText);
	}

	TSharedPtr<FJsonObject> ParseJsonToolResult(const FBridgeToolResult& ToolResult)
	{
		FString JsonText;
		if (!TryGetToolResultText(ToolResult, JsonText) || JsonText.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ParsedObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, ParsedObject) || !ParsedObject.IsValid())
		{
			return nullptr;
		}
		return ParsedObject;
	}

	FBridgeToolResult AddJsonFieldsToToolResult(
		const FBridgeToolResult& ToolResult,
		const TSharedPtr<FJsonObject>& ExtraFields)
	{
		if (ToolResult.bIsError)
		{
			return ToolResult;
		}

		TSharedPtr<FJsonObject> ParsedObject = ParseJsonToolResult(ToolResult);
		if (!ParsedObject.IsValid())
		{
			return ToolResult;
		}

		CopyJsonFields(ExtraFields, ParsedObject);
		return FBridgeToolResult::Json(ParsedObject);
	}

	bool IsUnsafeWindowScreenshotDuringPIE()
	{
		if (!GEngine)
		{
			return false;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::PIE
				&& WorldContext.GameViewport
				&& WorldContext.GameViewport->bDisableWorldRendering)
			{
				return true;
			}
		}

		return false;
	}
}

TMap<FString, FBridgeSchemaProperty> UCaptureScreenshotTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Capture mode: 'window' (entire editor), 'tab' (specific panel), 'region' (coordinates), 'viewport' (render target), or 'pie-window' (composited PIE viewport with UMG)"),
		false,
		{TEXT("window"), TEXT("tab"), TEXT("region"), TEXT("viewport"), TEXT("pie-window")}
	});

	Schema.Add(TEXT("window_name"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("For 'tab' mode: Name of the editor tab/panel to capture (e.g., Blueprint, Material, OutputLog)"),
		false
	});

	Schema.Add(TEXT("region"), FBridgeSchemaProperty{
		TEXT("array"),
		TEXT("For 'region' mode: Screen coordinates [x, y, width, height] to capture"),
		false
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' writes to temp directory (default), 'base64' returns encoded data"),
		false,
		{TEXT("file"), TEXT("base64")}
	});

	Schema.Add(TEXT("scale"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Output scale percentage. 100 keeps current size; 50 halves width and height. Requests above 100 are clamped."),
		false
	});

	Schema.Add(TEXT("width"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output width in pixels. Height is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("height"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output height in pixels. Width is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("color_mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output color mode: color (default), grayscale, or monochrome"),
		false,
		{TEXT("color"), TEXT("grayscale"), TEXT("monochrome")}
	});

	Schema.Add(TEXT("cleanup_previous"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Delete previous screenshot files before writing this file output (default: false)"),
		false
	});

	Schema.Add(TEXT("safe_mode"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("When true, window capture during PIE falls back to the composited PIE viewport to avoid unstable full-window Slate capture (default: true)"),
		false
	});

	Schema.Add(TEXT("unsafe_slate_window_capture"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Alias for safe_mode=false; allows direct full-window Slate capture during PIE"),
		false
	});

	return Schema;
}

TArray<FString> UCaptureScreenshotTool::GetRequiredParams() const
{
	return {};
}

FBridgeToolResult UCaptureScreenshotTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("viewport"));
	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString OutputMode = GetStringArgOrDefault(Arguments, TEXT("output"), TEXT("file"));
	const float ScalePercent = GetFloatArgOrDefault(Arguments, TEXT("scale"), 100.0f);
	const int32 TargetWidth = GetIntArgOrDefault(Arguments, TEXT("width"), 0);
	const int32 TargetHeight = GetIntArgOrDefault(Arguments, TEXT("height"), 0);
	const FString ColorMode = GetStringArgOrDefault(Arguments, TEXT("color_mode"), TEXT("color"));
	const bool bCleanupPrevious = GetBoolArgOrDefault(Arguments, TEXT("cleanup_previous"), false);
	bool bSafeMode = GetBoolArgOrDefault(Arguments, TEXT("safe_mode"), true);
	if (GetBoolArgOrDefault(Arguments, TEXT("unsafe_slate_window_capture"), false))
	{
		bSafeMode = false;
	}

	// Viewport mode delegates to the runtime tool (no Slate needed)
	if (Mode == TEXT("viewport"))
	{
		return CaptureViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
	}
	if (Mode == TEXT("pie-window"))
	{
		return CapturePIEWindow(
			Format,
			OutputMode,
			ScalePercent,
			TargetWidth,
			TargetHeight,
			ColorMode,
			bCleanupPrevious,
			bSafeMode,
			Mode,
			TEXT(""));
	}

	// All other modes require Slate
	if (!FSlateApplication::IsInitialized())
	{
		return FBridgeToolResult::Error(TEXT("Slate application not initialized"));
	}

	// Route to appropriate capture mode
	if (Mode == TEXT("window"))
	{
		return CaptureWindow(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious, bSafeMode);
	}
	else if (Mode == TEXT("tab"))
	{
		const FString WindowName = GetStringArgOrDefault(Arguments, TEXT("window_name"), TEXT(""));
		if (WindowName.IsEmpty())
		{
			return FBridgeToolResult::Error(TEXT("'window_name' is required for 'tab' mode"));
		}
		return CaptureTab(WindowName, Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
	}
	else if (Mode == TEXT("region"))
	{
		const TArray<TSharedPtr<FJsonValue>>* RegionArray = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("region"), RegionArray) || !RegionArray || RegionArray->Num() != 4)
		{
			return FBridgeToolResult::Error(TEXT("'region' must be an array of 4 integers [x, y, width, height]"));
		}

		FIntRect Region(
			(*RegionArray)[0]->AsNumber(),
			(*RegionArray)[1]->AsNumber(),
			(*RegionArray)[0]->AsNumber() + (*RegionArray)[2]->AsNumber(),
			(*RegionArray)[1]->AsNumber() + (*RegionArray)[3]->AsNumber()
		);

		return CaptureRegion(Region, Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid mode '%s'. Must be 'window', 'tab', 'region', 'viewport', or 'pie-window'."), *Mode));
	}
}

FBridgeToolResult UCaptureScreenshotTool::CaptureViewport(
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
{
	// Delegate to the runtime capture-viewport tool (works in both PIE and standalone)
	TSharedPtr<FJsonObject> Args = MakeShareable(new FJsonObject);
	Args->SetStringField(TEXT("format"), Format);
	Args->SetStringField(TEXT("output"), OutputMode);
	Args->SetNumberField(TEXT("scale"), ScalePercent);
	Args->SetNumberField(TEXT("width"), TargetWidth);
	Args->SetNumberField(TEXT("height"), TargetHeight);
	Args->SetStringField(TEXT("color_mode"), ColorMode);
	Args->SetBoolField(TEXT("cleanup_previous"), bCleanupPrevious);
	return FBridgeToolRegistry::Get().ExecuteTool(TEXT("capture-viewport"), Args, FBridgeToolContext());
}

FBridgeToolResult UCaptureScreenshotTool::CaptureWindow(
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious,
	bool bSafeMode)
{
	if (bSafeMode && GEditor && GEditor->IsPlaySessionInProgress())
	{
		return CapturePIEWindow(
			Format,
			OutputMode,
			ScalePercent,
			TargetWidth,
			TargetHeight,
			ColorMode,
			bCleanupPrevious,
			true,
			TEXT("window"),
			TEXT("safe_mode_pie_window_fallback"));
	}

	// Prefer the main editor window regardless of focus — agents call this tool
	// without the editor being the active/foreground window.
	TSharedPtr<SWindow> EditorWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
		EditorWindow = MainFrame.GetParentWindow();
	}

	// Fall back to the focused window (works in non-editor contexts)
	if (!EditorWindow.IsValid())
	{
		EditorWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	}

	if (!EditorWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No editor window found"));
	}

	if (IsUnsafeWindowScreenshotDuringPIE())
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("capture-screenshot window: PIE game viewport has bDisableWorldRendering=true; falling back to viewport capture to avoid an unsafe Slate full-window screenshot."));
		return CaptureViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
	}

	TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
	ExtraFields->SetStringField(TEXT("requested_mode"), TEXT("window"));
	ExtraFields->SetStringField(TEXT("capture_source"), TEXT("slate_window"));
	ExtraFields->SetBoolField(TEXT("safe_mode"), bSafeMode);
	ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
	ExtraFields->SetStringField(TEXT("window_title"), EditorWindow->GetTitle().ToString());
	if (EditorWindow->GetNativeWindow().IsValid())
	{
		const void* NativeHandle = EditorWindow->GetNativeWindow()->GetOSWindowHandle();
		ExtraFields->SetStringField(TEXT("native_window_handle"), FString::Printf(TEXT("%p"), NativeHandle));
	}

	return CaptureFromWindow(EditorWindow, Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious, ExtraFields);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureTab(
	const FString& TabName,
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
{
	// Try to find the tab by name
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*TabName));
	if (!Tab.IsValid())
	{
		Tab = FindLiveTabByLabel(TabName);
	}

	if (!Tab.IsValid())
	{
		if (TSharedPtr<SWindow> Window = FindTopLevelWindowByTitle(TabName))
		{
			return CaptureFromWindow(Window, Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
		}

		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' not found by tab id, visible tab label, or top-level window title"), *TabName));
	}

	// Get the tab's content widget
	TSharedPtr<SWidget> Content = Tab->GetContent();
	if (!Content.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' has no content to capture"), *TabName));
	}

	// Capture the widget
	TArray<FColor> RawData;
	int32 Width, Height;
	if (!TakeWidgetScreenshot(Content.ToSharedRef(), RawData, Width, Height))
	{
		if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
		{
			return CaptureFromWindow(Window, Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious);
		}

		return FBridgeToolResult::Error(TEXT("Failed to capture tab screenshot"));
	}

	const int32 OriginalWidth = Width;
	const int32 OriginalHeight = Height;
	int32 OutputWidth = Width;
	int32 OutputHeight = Height;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(
		ImageData,
		Format,
		OutputMode,
		FString::Printf(TEXT("tab '%s'"), *TabName),
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureRegion(
	const FIntRect& Region,
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious)
{
	// Get the virtual desktop geometry
	FSlateRect VirtualDesktopRect = FSlateApplication::Get().GetWorkArea(FSlateRect());
	FVector2D ScreenSize = VirtualDesktopRect.GetSize();

	// Validate region is within screen bounds
	if (Region.Min.X < 0 || Region.Min.Y < 0 ||
		Region.Max.X > ScreenSize.X || Region.Max.Y > ScreenSize.Y)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Region [%d, %d, %d, %d] is outside screen bounds [0, 0, %d, %d]"),
			Region.Min.X, Region.Min.Y, Region.Max.X, Region.Max.Y,
			(int32)ScreenSize.X, (int32)ScreenSize.Y));
	}

	// Get active window for screenshot
	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No active editor window found for region capture"));
	}

	// Take screenshot using Slate
	TArray<FColor> RawData;
	FIntVector OutSize;

	if (!FSlateApplication::Get().TakeScreenshot(
		ActiveWindow.ToSharedRef(),
		Region,
		RawData,
		OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture region screenshot"));
	}

	const int32 OriginalWidth = OutSize.X;
	const int32 OriginalHeight = OutSize.Y;
	int32 OutputWidth = OriginalWidth;
	int32 OutputHeight = OriginalHeight;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(ImageData, Format, OutputMode,
		FString::Printf(TEXT("region [%d, %d, %dx%d]"),
			Region.Min.X, Region.Min.Y, Region.Width(), Region.Height()),
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureFromWindow(
	TSharedPtr<SWindow> Window,
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious,
	TSharedPtr<FJsonObject> ExtraFields)
{
	if (!Window.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Invalid window"));
	}

	TArray<FColor> RawData;
	FIntVector OutSize;

	// Capture the window
	if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), RawData, OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture window screenshot"));
	}

	const int32 OriginalWidth = OutSize.X;
	const int32 OriginalHeight = OutSize.Y;
	int32 OutputWidth = OriginalWidth;
	int32 OutputHeight = OriginalHeight;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	// Compress and output
	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	return OutputImage(
		ImageData,
		Format,
		OutputMode,
		TEXT("editor window"),
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious,
		ExtraFields);
}

FBridgeToolResult UCaptureScreenshotTool::CapturePIEWindow(
	const FString& Format,
	const FString& OutputMode,
	float ScalePercent,
	int32 TargetWidth,
	int32 TargetHeight,
	const FString& ColorMode,
	bool bCleanupPrevious,
	bool bSafeMode,
	const FString& RequestedMode,
	const FString& FallbackReason)
{
	if (bSafeMode)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("capture-screenshot pie-window: defaulting to runtime viewport capture to avoid unsafe PIE Slate window screenshot; pass unsafe_slate_window_capture=true to opt in."));

		TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
		ExtraFields->SetStringField(TEXT("requested_mode"), RequestedMode);
		ExtraFields->SetStringField(TEXT("requested_capture_mode"), TEXT("pie-window"));
		ExtraFields->SetStringField(TEXT("capture_mode"), TEXT("viewport"));
		ExtraFields->SetStringField(TEXT("capture_source"), TEXT("runtime_viewport_readpixels"));
		ExtraFields->SetStringField(TEXT("capture_fallback_kind"), TEXT("pie_window_safe_viewport_fallback"));
		ExtraFields->SetStringField(TEXT("fallback_reason"), FallbackReason.IsEmpty()
			? TEXT("pie_window_safe_viewport_fallback")
			: FallbackReason);
		ExtraFields->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		ExtraFields->SetBoolField(TEXT("safe_mode"), true);
		ExtraFields->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), false);
		ExtraFields->SetBoolField(TEXT("structured_exception"), false);

		FBridgeToolResult ViewportResult = CaptureViewport(
			Format,
			OutputMode,
			ScalePercent,
			TargetWidth,
			TargetHeight,
			ColorMode,
			bCleanupPrevious);

		if (ViewportResult.bIsError)
		{
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			CopyJsonFields(ExtraFields, Result);
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("PIE window safe viewport fallback failed"));

			FString FallbackError;
			if (TryGetToolResultText(ViewportResult, FallbackError) && !FallbackError.IsEmpty())
			{
				Result->SetStringField(TEXT("fallback_error"), FallbackError);
			}
			return FBridgeToolResult::Json(Result);
		}

		return AddJsonFieldsToToolResult(ViewportResult, ExtraFields);
	}

	if (!FSlateApplication::IsInitialized())
	{
		return FBridgeToolResult::Error(TEXT("Slate application not initialized"));
	}

	TSharedPtr<SWidget> PIEViewportWidget = FindPIEGameViewportWidget();
	if (!PIEViewportWidget.IsValid())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("No composited PIE game viewport widget found"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("no_pie_viewport_widget"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	const FVector2D WidgetSize = PIEViewportWidget->GetTickSpaceGeometry().GetLocalSize();
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Composited PIE viewport widget has invalid geometry"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("invalid_widget_geometry"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	TArray<FColor> RawData;
	int32 Width = 0;
	int32 Height = 0;
	if (!TakeWidgetScreenshot(PIEViewportWidget.ToSharedRef(), RawData, Width, Height))
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to capture composited PIE viewport widget"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("slate_capture_failed"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	const int32 OriginalWidth = Width;
	const int32 OriginalHeight = Height;
	int32 OutputWidth = Width;
	int32 OutputHeight = Height;
	FString NormalizedColorMode;
	FString TransformError;
	if (!ApplyImageTransform(
		RawData,
		OutputWidth,
		OutputHeight,
		ScalePercent,
		TargetWidth,
		TargetHeight,
		ColorMode,
		NormalizedColorMode,
		TransformError))
	{
		return FBridgeToolResult::Error(TransformError);
	}

	TArray<uint8> ImageData = CompressImage(RawData, OutputWidth, OutputHeight, Format);
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Failed to compress screenshot image"));
	}

	TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
	ExtraFields->SetStringField(TEXT("requested_mode"), RequestedMode);
	ExtraFields->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
	ExtraFields->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
	ExtraFields->SetBoolField(TEXT("safe_mode"), bSafeMode);
	ExtraFields->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
	ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
	ExtraFields->SetBoolField(TEXT("structured_exception"), false);
	ExtraFields->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
	if (!FallbackReason.IsEmpty())
	{
		ExtraFields->SetStringField(TEXT("fallback_reason"), FallbackReason);
	}

	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(PIEViewportWidget.ToSharedRef());
	if (WidgetWindow.IsValid())
	{
		ExtraFields->SetStringField(TEXT("window_title"), WidgetWindow->GetTitle().ToString());
		if (WidgetWindow->GetNativeWindow().IsValid())
		{
			const void* NativeHandle = WidgetWindow->GetNativeWindow()->GetOSWindowHandle();
			ExtraFields->SetStringField(TEXT("native_window_handle"), FString::Printf(TEXT("%p"), NativeHandle));
		}
	}

	return OutputImage(
		ImageData,
		Format,
		OutputMode,
		TEXT("PIE game viewport (composited)"),
		OutputWidth,
		OutputHeight,
		OriginalWidth,
		OriginalHeight,
		NormalizedColorMode,
		bCleanupPrevious,
		ExtraFields);
}

TSharedPtr<SWidget> UCaptureScreenshotTool::FindPIEGameViewportWidget() const
{
	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType != EWorldType::PIE)
			{
				continue;
			}

			UWorld* World = WorldContext.World();
			UGameViewportClient* GameViewportClient = World ? World->GetGameViewport() : nullptr;
			if (!GameViewportClient)
			{
				continue;
			}

			TSharedPtr<SViewport> GameViewportWidget = GameViewportClient->GetGameViewportWidget();
			if (GameViewportWidget.IsValid())
			{
				return StaticCastSharedPtr<SWidget>(GameViewportWidget);
			}
		}
	}

	if (GEditor && GEditor->PlayWorld)
	{
		if (UGameViewportClient* GameViewportClient = GEditor->PlayWorld->GetGameViewport())
		{
			TSharedPtr<SViewport> GameViewportWidget = GameViewportClient->GetGameViewportWidget();
			if (GameViewportWidget.IsValid())
			{
				return StaticCastSharedPtr<SWidget>(GameViewportWidget);
			}
		}
	}

	return nullptr;
}

bool UCaptureScreenshotTool::TakeWidgetScreenshot(
	TSharedRef<SWidget> Widget,
	TArray<FColor>& OutImageData,
	int32& OutWidth,
	int32& OutHeight)
{
	// Get widget geometry
	FGeometry WidgetGeometry = Widget->GetCachedGeometry();
	FVector2D LocalSize = WidgetGeometry.GetLocalSize();

	OutWidth = FMath::TruncToInt(LocalSize.X);
	OutHeight = FMath::TruncToInt(LocalSize.Y);

	if (OutWidth <= 0 || OutHeight <= 0)
	{
		return false;
	}

	// Create render target
	FIntPoint Size(OutWidth, OutHeight);
	TArray<FColor> ColorData;

	// Use Slate's screenshot functionality
	FIntVector OutSize;
	FSlateRect WidgetRect = WidgetGeometry.GetRenderBoundingRect();

	// Get the window containing this widget
	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
	if (!WidgetWindow.IsValid())
	{
		return false;
	}

	// Take screenshot of the widget area
	FIntRect CaptureRect(
		FMath::TruncToInt(WidgetRect.Left),
		FMath::TruncToInt(WidgetRect.Top),
		FMath::TruncToInt(WidgetRect.Right),
		FMath::TruncToInt(WidgetRect.Bottom)
	);

	if (!FSlateApplication::Get().TakeScreenshot(WidgetWindow.ToSharedRef(), CaptureRect, ColorData, OutSize))
	{
		return false;
	}

	OutImageData = MoveTemp(ColorData);
	OutWidth = OutSize.X;
	OutHeight = OutSize.Y;

	return true;
}

TArray<uint8> UCaptureScreenshotTool::CompressImage(
	const TArray<FColor>& RawData,
	int32 Width,
	int32 Height,
	const FString& Format)
{
	if (Width <= 0 || Height <= 0)
	{
		return {};
	}

	const int64 ExpectedPixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
	if (ExpectedPixelCount <= 0 || RawData.Num() != ExpectedPixelCount || ExpectedPixelCount > MAX_int32 / 4)
	{
		return {};
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = (Format == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		return {};
	}

	// Convert FColor to raw RGBA bytes
	// Note: FColor stores data as BGRA internally, but IImageWrapper expects RGBA
	// Manual channel reordering is required for correct color output
	const int32 DataSize = static_cast<int32>(ExpectedPixelCount * 4);
	TArray<uint8> RawBytes;
	RawBytes.SetNumUninitialized(DataSize);

	for (int32 i = 0; i < RawData.Num(); ++i)
	{
		RawBytes[i * 4 + 0] = RawData[i].R;
		RawBytes[i * 4 + 1] = RawData[i].G;
		RawBytes[i * 4 + 2] = RawData[i].B;
		RawBytes[i * 4 + 3] = RawData[i].A;
	}

	// Set image data
	if (!ImageWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		return {};
	}

	// Get compressed data
	int32 Quality = (Format == TEXT("jpeg")) ? 85 : 0; // PNG ignores quality
	TArray<uint8, FDefaultAllocator64> CompressedData64 = ImageWrapper->GetCompressed(Quality);
	// Convert from FDefaultAllocator64 to FDefaultAllocator
	TArray<uint8> CompressedData;
	CompressedData.Append(CompressedData64);
	return CompressedData;
}

FBridgeToolResult UCaptureScreenshotTool::OutputImage(
	const TArray<uint8>& ImageData,
	const FString& Format,
	const FString& OutputMode,
	const FString& CaptureMode,
	int32 Width,
	int32 Height,
	int32 OriginalWidth,
	int32 OriginalHeight,
	const FString& ColorMode,
	bool bCleanupPrevious,
	TSharedPtr<FJsonObject> ExtraFields)
{
	if (ImageData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Empty image data"));
	}

	// Base64 mode - return image content
	if (OutputMode == TEXT("base64"))
	{
		const FString MimeType = Format == TEXT("jpeg") ? TEXT("image/jpeg") : TEXT("image/png");

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-screenshot: Captured %s as %s (%d bytes)"),
			*CaptureMode, *Format, ImageData.Num());

		TSharedPtr<FJsonObject> ImageResult = MakeShareable(new FJsonObject);
		ImageResult->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));
		ImageResult->SetStringField(TEXT("mime_type"), MimeType);
		ImageResult->SetStringField(TEXT("mode"), TEXT("base64"));
		ImageResult->SetStringField(TEXT("captured"), CaptureMode);
		ImageResult->SetStringField(TEXT("format"), Format);
		ImageResult->SetNumberField(TEXT("size_bytes"), ImageData.Num());
		ImageResult->SetNumberField(TEXT("width"), Width);
		ImageResult->SetNumberField(TEXT("height"), Height);
		ImageResult->SetNumberField(TEXT("original_width"), OriginalWidth);
		ImageResult->SetNumberField(TEXT("original_height"), OriginalHeight);
		ImageResult->SetStringField(TEXT("color_mode"), ColorMode);
		CopyJsonFields(ExtraFields, ImageResult);
		return FBridgeToolResult::Json(ImageResult);
	}

	// File mode - write to temp directory
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	if (bCleanupPrevious)
	{
		CleanupPreviousScreenshots(TempDir);
	}

	// Generate filename from content hash
	const FString Hash = FMD5::HashBytes(ImageData.GetData(), FMath::Min(1024, ImageData.Num()));
	const FString FileName = FString::Printf(TEXT("screenshot_%s.%s"), *Hash.Left(8), *Format);
	const FString FilePath = FPaths::Combine(TempDir, FileName);

	// Write file
	if (!FFileHelper::SaveArrayToFile(ImageData, *FilePath))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to write screenshot to %s"), *FilePath));
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("mode"), TEXT("file"));
	Result->SetStringField(TEXT("file_path"), FilePath);
	Result->SetStringField(TEXT("captured"), CaptureMode);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetNumberField(TEXT("size_bytes"), ImageData.Num());
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("original_width"), OriginalWidth);
	Result->SetNumberField(TEXT("original_height"), OriginalHeight);
	Result->SetStringField(TEXT("color_mode"), ColorMode);
	CopyJsonFields(ExtraFields, Result);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Screenshot of %s saved to %s"), *CaptureMode, *FilePath));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-screenshot: Captured %s as %s -> %s (%d bytes)"),
		*CaptureMode, *Format, *FilePath, ImageData.Num());

	return FBridgeToolResult::Json(Result);
}

void UCaptureScreenshotTool::CleanupPreviousScreenshots(const FString& TempDir)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *(TempDir / TEXT("screenshot_*")), true, false);

	for (const FString& FileName : FoundFiles)
	{
		const FString FullPath = FPaths::Combine(TempDir, FileName);
		IFileManager::Get().Delete(*FullPath, false, true);
	}
}
