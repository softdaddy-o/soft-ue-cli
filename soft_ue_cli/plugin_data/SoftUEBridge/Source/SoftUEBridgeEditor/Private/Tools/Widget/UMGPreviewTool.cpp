// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/UMGPreviewTool.h"

#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/Layout/Anchors.h"
#include "Tools/Widget/WidgetPreviewRegistry.h"
#include "UObject/UObjectIterator.h"
#include "Utils/BridgeAssetModifier.h"
#include "WidgetBlueprint.h"

namespace
{
	TMap<FString, FBridgeSchemaProperty> PreviewCreateSchema()
	{
		TMap<FString, FBridgeSchemaProperty> Schema;

		FBridgeSchemaProperty WidgetClass;
		WidgetClass.Type = TEXT("string");
		WidgetClass.Description = TEXT("Widget class path or WidgetBlueprint asset path to create and add to the PIE viewport.");
		WidgetClass.bRequired = true;
		Schema.Add(TEXT("widget_class"), WidgetClass);

		FBridgeSchemaProperty PieIndex;
		PieIndex.Type = TEXT("integer");
		PieIndex.Description = TEXT("PIE instance index, 0-based (default: 0).");
		Schema.Add(TEXT("pie_index"), PieIndex);

		FBridgeSchemaProperty ViewportZOrder;
		ViewportZOrder.Type = TEXT("integer");
		ViewportZOrder.Description = TEXT("Viewport Z order for the created preview widget (default: 0).");
		Schema.Add(TEXT("viewport_z_order"), ViewportZOrder);

		FBridgeSchemaProperty Fullscreen;
		Fullscreen.Type = TEXT("boolean");
		Fullscreen.Description = TEXT("Apply full-screen viewport layout defaults to avoid zero-sized preview geometry.");
		Schema.Add(TEXT("fullscreen"), Fullscreen);

		FBridgeSchemaProperty ViewportAnchors;
		ViewportAnchors.Type = TEXT("array");
		ViewportAnchors.Description = TEXT("Viewport anchors as [MinX, MinY, MaxX, MaxY].");
		Schema.Add(TEXT("viewport_anchors"), ViewportAnchors);

		FBridgeSchemaProperty ViewportPosition;
		ViewportPosition.Type = TEXT("array");
		ViewportPosition.Description = TEXT("Viewport position as [X, Y].");
		Schema.Add(TEXT("viewport_position"), ViewportPosition);

		FBridgeSchemaProperty ViewportSize;
		ViewportSize.Type = TEXT("array");
		ViewportSize.Description = TEXT("Desired viewport size as [W, H].");
		Schema.Add(TEXT("viewport_size"), ViewportSize);

		FBridgeSchemaProperty ViewportAlignment;
		ViewportAlignment.Type = TEXT("array");
		ViewportAlignment.Description = TEXT("Viewport alignment as [X, Y].");
		Schema.Add(TEXT("viewport_alignment"), ViewportAlignment);

		FBridgeSchemaProperty CaptureAfter;
		CaptureAfter.Type = TEXT("boolean");
		CaptureAfter.Description = TEXT("Return a capture-viewport recommendation after creating the preview.");
		Schema.Add(TEXT("capture_after"), CaptureAfter);

		return Schema;
	}

	TMap<FString, FBridgeSchemaProperty> PreviewRemoveSchema()
	{
		TMap<FString, FBridgeSchemaProperty> Schema;

		FBridgeSchemaProperty PreviewHandle;
		PreviewHandle.Type = TEXT("string");
		PreviewHandle.Description = TEXT("Specific preview handle to remove. If omitted, removes all tool-owned previews for the selected PIE world.");
		Schema.Add(TEXT("preview_handle"), PreviewHandle);

		FBridgeSchemaProperty PieIndex;
		PieIndex.Type = TEXT("integer");
		PieIndex.Description = TEXT("PIE instance index, 0-based (default: 0). Used when preview_handle is omitted.");
		Schema.Add(TEXT("pie_index"), PieIndex);

		return Schema;
	}

	TMap<FString, FBridgeSchemaProperty> PreviewListSchema()
	{
		TMap<FString, FBridgeSchemaProperty> Schema;

		FBridgeSchemaProperty PieIndex;
		PieIndex.Type = TEXT("integer");
		PieIndex.Description = TEXT("Optional PIE instance index. If omitted, lists all valid tool-owned preview widgets.");
		Schema.Add(TEXT("pie_index"), PieIndex);

		return Schema;
	}

	UWorld* GetPIEWorldByIndex(int32 Index, int32& OutTotalCount)
	{
		OutTotalCount = 0;
		UWorld* FoundWorld = nullptr;
		if (!GEngine)
		{
			return nullptr;
		}

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

	FBridgeToolResult ResolvePIEWorldForTool(const TSharedPtr<FJsonObject>& Arguments, UWorld*& OutWorld, int32& OutPieIndex)
	{
		if (!GEditor || !GEditor->IsPlaySessionInProgress())
		{
			return FBridgeToolResult::Error(TEXT("No PIE session running. Start one with 'pie-session --action start'."));
		}

		OutPieIndex = 0;
		if (Arguments.IsValid())
		{
			Arguments->TryGetNumberField(TEXT("pie_index"), OutPieIndex);
		}

		int32 TotalPIEWorlds = 0;
		OutWorld = GetPIEWorldByIndex(OutPieIndex, TotalPIEWorlds);
		if (!OutWorld)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("PIE instance %d not found. %d instance(s) running."),
				OutPieIndex,
				TotalPIEWorlds));
		}

		return FBridgeToolResult::Text(TEXT(""));
	}

	UClass* ResolveWidgetClass(const FString& WidgetClassPath, FString& OutError)
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

	TArray<UUserWidget*> CollectPIERootWidgets(UWorld* PIEWorld)
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
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> BuildRootWidgetSummaries(UWorld* PIEWorld)
	{
		TArray<TSharedPtr<FJsonValue>> RootSummaries;
		for (UUserWidget* Root : CollectPIERootWidgets(PIEWorld))
		{
			if (!Root)
			{
				continue;
			}

			TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
			Summary->SetStringField(TEXT("name"), Root->GetName());
			Summary->SetStringField(TEXT("class"), Root->GetClass() ? Root->GetClass()->GetName() : TEXT(""));
			RootSummaries.Add(MakeShareable(new FJsonValueObject(Summary)));
		}
		return RootSummaries;
	}

	TArray<TSharedPtr<FJsonValue>> BuildPreviewSummaries(UWorld* World)
	{
		TArray<FWidgetPreviewSummary> Summaries;
		FWidgetPreviewRegistry::ListPreviewsForWorld(World, Summaries);

		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FWidgetPreviewSummary& Summary : Summaries)
		{
			TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
			Item->SetStringField(TEXT("preview_handle"), Summary.Handle);
			Item->SetStringField(TEXT("world_name"), Summary.WorldName);
			Item->SetStringField(TEXT("widget_name"), Summary.WidgetName);
			Item->SetStringField(TEXT("widget_class"), Summary.WidgetClass);
			Values.Add(MakeShareable(new FJsonValueObject(Item)));
		}
		return Values;
	}

	struct FPreviewViewportLayout
	{
		bool bApply = false;
		bool bFullscreen = false;
		bool bHasAnchors = false;
		bool bHasPosition = false;
		bool bHasSize = false;
		bool bHasAlignment = false;
		FAnchors Anchors = FAnchors(0.0f, 0.0f, 0.0f, 0.0f);
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		FVector2D Alignment = FVector2D::ZeroVector;
	};

	bool TryReadVector2(
		const TSharedPtr<FJsonObject>& Obj,
		const TCHAR* Field,
		FVector2D& OutValue,
		bool& bOutFound,
		FString& OutError)
	{
		bOutFound = false;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, Array))
		{
			return true;
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
		bOutFound = true;
		return true;
	}

	bool TryReadAnchors(
		const TSharedPtr<FJsonObject>& Obj,
		const TCHAR* Field,
		FAnchors& OutValue,
		bool& bOutFound,
		FString& OutError)
	{
		bOutFound = false;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, Array))
		{
			return true;
		}
		if (!Array || Array->Num() != 4)
		{
			OutError = FString::Printf(TEXT("%s must be an array [MinX, MinY, MaxX, MaxY]"), Field);
			return false;
		}

		double MinX = 0.0;
		double MinY = 0.0;
		double MaxX = 0.0;
		double MaxY = 0.0;
		if (!(*Array)[0]->TryGetNumber(MinX)
			|| !(*Array)[1]->TryGetNumber(MinY)
			|| !(*Array)[2]->TryGetNumber(MaxX)
			|| !(*Array)[3]->TryGetNumber(MaxY))
		{
			OutError = FString::Printf(TEXT("%s must contain numeric values"), Field);
			return false;
		}

		OutValue = FAnchors(MinX, MinY, MaxX, MaxY);
		bOutFound = true;
		return true;
	}

	bool ParsePreviewViewportLayout(
		const TSharedPtr<FJsonObject>& Arguments,
		FPreviewViewportLayout& OutLayout,
		FString& OutError)
	{
		if (!Arguments.IsValid())
		{
			return true;
		}

		bool bFullscreen = false;
		if (Arguments->TryGetBoolField(TEXT("fullscreen"), bFullscreen) && bFullscreen)
		{
			OutLayout.bApply = true;
			OutLayout.bFullscreen = true;
			OutLayout.bHasAnchors = true;
			OutLayout.bHasPosition = true;
			OutLayout.bHasSize = true;
			OutLayout.bHasAlignment = true;
			OutLayout.Anchors = FAnchors(0.0f, 0.0f, 1.0f, 1.0f);
			OutLayout.Position = FVector2D::ZeroVector;
			OutLayout.Size = FVector2D(1920.0, 1080.0);
			OutLayout.Alignment = FVector2D::ZeroVector;
		}

		bool bFound = false;
		FAnchors Anchors;
		if (!TryReadAnchors(Arguments, TEXT("viewport_anchors"), Anchors, bFound, OutError))
		{
			return false;
		}
		if (bFound)
		{
			OutLayout.bApply = true;
			OutLayout.bHasAnchors = true;
			OutLayout.Anchors = Anchors;
		}

		FVector2D Position;
		if (!TryReadVector2(Arguments, TEXT("viewport_position"), Position, bFound, OutError))
		{
			return false;
		}
		if (bFound)
		{
			OutLayout.bApply = true;
			OutLayout.bHasPosition = true;
			OutLayout.Position = Position;
		}

		FVector2D Size;
		if (!TryReadVector2(Arguments, TEXT("viewport_size"), Size, bFound, OutError))
		{
			return false;
		}
		if (bFound)
		{
			OutLayout.bApply = true;
			OutLayout.bHasSize = true;
			OutLayout.Size = Size;
		}

		FVector2D Alignment;
		if (!TryReadVector2(Arguments, TEXT("viewport_alignment"), Alignment, bFound, OutError))
		{
			return false;
		}
		if (bFound)
		{
			OutLayout.bApply = true;
			OutLayout.bHasAlignment = true;
			OutLayout.Alignment = Alignment;
		}

		return true;
	}

	TArray<TSharedPtr<FJsonValue>> Vector2DToJsonArray(const FVector2D& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShareable(new FJsonValueNumber(Value.X)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Y)));
		return Array;
	}

	TArray<TSharedPtr<FJsonValue>> AnchorsToJsonArray(const FAnchors& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Minimum.X)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Minimum.Y)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Maximum.X)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Maximum.Y)));
		return Array;
	}

	TSharedPtr<FJsonObject> BuildViewportLayoutResult(const FPreviewViewportLayout& Layout)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("applied"), Layout.bApply);
		Result->SetBoolField(TEXT("fullscreen"), Layout.bFullscreen);
		if (Layout.bHasAnchors)
		{
			Result->SetArrayField(TEXT("viewport_anchors"), AnchorsToJsonArray(Layout.Anchors));
		}
		if (Layout.bHasPosition)
		{
			Result->SetArrayField(TEXT("viewport_position"), Vector2DToJsonArray(Layout.Position));
		}
		if (Layout.bHasSize)
		{
			Result->SetArrayField(TEXT("viewport_size"), Vector2DToJsonArray(Layout.Size));
		}
		if (Layout.bHasAlignment)
		{
			Result->SetArrayField(TEXT("viewport_alignment"), Vector2DToJsonArray(Layout.Alignment));
		}
		return Result;
	}

	void ApplyPreviewViewportLayout(UUserWidget* Widget, const FPreviewViewportLayout& Layout)
	{
		if (!Widget || !Layout.bApply)
		{
			return;
		}
		if (Layout.bHasAnchors)
		{
			Widget->SetAnchorsInViewport(Layout.Anchors);
		}
		if (Layout.bHasAlignment)
		{
			Widget->SetAlignmentInViewport(Layout.Alignment);
		}
		if (Layout.bHasPosition)
		{
			Widget->SetPositionInViewport(Layout.Position, false);
		}
		if (Layout.bHasSize)
		{
			Widget->SetDesiredSizeInViewport(Layout.Size);
		}
		Widget->ForceLayoutPrepass();
	}

	void SetRemovedHandles(TSharedPtr<FJsonObject> Result, const TArray<FString>& RemovedHandles)
	{
		TArray<TSharedPtr<FJsonValue>> RemovedHandleValues;
		for (const FString& RemovedHandle : RemovedHandles)
		{
			RemovedHandleValues.Add(MakeShareable(new FJsonValueString(RemovedHandle)));
		}
		Result->SetArrayField(TEXT("removed_preview_handles"), RemovedHandleValues);
	}

	FBridgeToolResult CreatePreviewWidget(
		const TSharedPtr<FJsonObject>& Arguments,
		const FString& Action,
		bool bReplaceExisting)
	{
		UWorld* PIEWorld = nullptr;
		int32 PieIndex = 0;
		FBridgeToolResult WorldResult = ResolvePIEWorldForTool(Arguments, PIEWorld, PieIndex);
		if (!PIEWorld)
		{
			return WorldResult;
		}

		FString WidgetClassPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("widget_class"), WidgetClassPath) || WidgetClassPath.IsEmpty())
		{
			return FBridgeToolResult::Error(TEXT("widget_class is required"));
		}

		TArray<FString> RemovedPreviewHandles;
		int32 RemovedExistingPreviews = 0;
		if (bReplaceExisting)
		{
			RemovedExistingPreviews = FWidgetPreviewRegistry::RemovePreviewsForWorld(PIEWorld, &RemovedPreviewHandles);
		}

		FString ResolveError;
		UClass* WidgetClass = ResolveWidgetClass(WidgetClassPath, ResolveError);
		if (!WidgetClass)
		{
			return FBridgeToolResult::Error(ResolveError);
		}

		UUserWidget* CreatedWidget = CreateWidget<UUserWidget>(PIEWorld, WidgetClass);
		if (!CreatedWidget)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("CreateWidget failed for %s"), *WidgetClassPath));
		}

		int32 ViewportZOrder = 0;
		bool bCaptureAfter = false;
		if (Arguments.IsValid())
		{
			Arguments->TryGetNumberField(TEXT("viewport_z_order"), ViewportZOrder);
			Arguments->TryGetBoolField(TEXT("capture_after"), bCaptureAfter);
		}

		FPreviewViewportLayout ViewportLayout;
		FString ViewportLayoutError;
		if (!ParsePreviewViewportLayout(Arguments, ViewportLayout, ViewportLayoutError))
		{
			return FBridgeToolResult::Error(ViewportLayoutError);
		}

		CreatedWidget->AddToViewport(ViewportZOrder);
		ApplyPreviewViewportLayout(CreatedWidget, ViewportLayout);
		const FString PreviewHandle = FWidgetPreviewRegistry::MakeHandle(WidgetClassPath, PIEWorld);
		FWidgetPreviewRegistry::RegisterPreview(PIEWorld, CreatedWidget, PreviewHandle);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("action"), Action);
		Result->SetNumberField(TEXT("pie_index"), PieIndex);
		Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
		Result->SetStringField(TEXT("widget_class"), WidgetClassPath);
		Result->SetStringField(TEXT("widget_name"), CreatedWidget->GetName());
		Result->SetStringField(TEXT("preview_handle"), PreviewHandle);
		Result->SetNumberField(TEXT("viewport_z_order"), ViewportZOrder);
		Result->SetObjectField(TEXT("viewport_layout"), BuildViewportLayoutResult(ViewportLayout));
		Result->SetNumberField(TEXT("removed_existing_previews"), RemovedExistingPreviews);
		Result->SetNumberField(TEXT("tool_preview_count"), FWidgetPreviewRegistry::CountPreviewsForWorld(PIEWorld));
		SetRemovedHandles(Result, RemovedPreviewHandles);
		Result->SetArrayField(TEXT("previews"), BuildPreviewSummaries(PIEWorld));
		Result->SetArrayField(TEXT("root_widgets"), BuildRootWidgetSummaries(PIEWorld));
		if (bCaptureAfter)
		{
			Result->SetBoolField(TEXT("capture_after"), true);
			Result->SetStringField(TEXT("capture_command"), TEXT("soft-ue-cli capture-viewport --source game --output file"));
		}

		return FBridgeToolResult::Json(Result);
	}
}

FString UUMGPreviewCreateTool::GetToolDescription() const
{
	return TEXT("Create a tool-owned UMG preview widget in an active PIE viewport and return its preview handle.");
}

TMap<FString, FBridgeSchemaProperty> UUMGPreviewCreateTool::GetInputSchema() const
{
	return PreviewCreateSchema();
}

TArray<FString> UUMGPreviewCreateTool::GetRequiredParams() const
{
	return {TEXT("widget_class")};
}

FBridgeToolResult UUMGPreviewCreateTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	return CreatePreviewWidget(Arguments, TEXT("create"), false);
}

FString UUMGPreviewReplaceTool::GetToolDescription() const
{
	return TEXT("Replace existing tool-owned UMG previews in a PIE viewport, then create a new preview widget.");
}

TMap<FString, FBridgeSchemaProperty> UUMGPreviewReplaceTool::GetInputSchema() const
{
	return PreviewCreateSchema();
}

TArray<FString> UUMGPreviewReplaceTool::GetRequiredParams() const
{
	return {TEXT("widget_class")};
}

FBridgeToolResult UUMGPreviewReplaceTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	return CreatePreviewWidget(Arguments, TEXT("replace"), true);
}

FString UUMGPreviewRemoveTool::GetToolDescription() const
{
	return TEXT("Remove tool-owned UMG preview widgets by handle, or all previews in a selected PIE viewport.");
}

TMap<FString, FBridgeSchemaProperty> UUMGPreviewRemoveTool::GetInputSchema() const
{
	return PreviewRemoveSchema();
}

FBridgeToolResult UUMGPreviewRemoveTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString PreviewHandle;
	if (Arguments.IsValid())
	{
		Arguments->TryGetStringField(TEXT("preview_handle"), PreviewHandle);
	}

	TArray<FString> RemovedPreviewHandles;
	int32 RemovedCount = 0;
	int32 PieIndex = 0;
	UWorld* PIEWorld = nullptr;

	if (!PreviewHandle.IsEmpty())
	{
		RemovedCount = FWidgetPreviewRegistry::RemovePreviewByHandle(PreviewHandle, &RemovedPreviewHandles);
	}
	else
	{
		FBridgeToolResult WorldResult = ResolvePIEWorldForTool(Arguments, PIEWorld, PieIndex);
		if (!PIEWorld)
		{
			return WorldResult;
		}
		RemovedCount = FWidgetPreviewRegistry::RemovePreviewsForWorld(PIEWorld, &RemovedPreviewHandles);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action"), TEXT("remove"));
	Result->SetNumberField(TEXT("removed_preview_count"), RemovedCount);
	SetRemovedHandles(Result, RemovedPreviewHandles);
	if (!PreviewHandle.IsEmpty())
	{
		Result->SetStringField(TEXT("preview_handle"), PreviewHandle);
	}
	if (PIEWorld)
	{
		Result->SetNumberField(TEXT("pie_index"), PieIndex);
		Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
		Result->SetNumberField(TEXT("tool_preview_count"), FWidgetPreviewRegistry::CountPreviewsForWorld(PIEWorld));
		Result->SetArrayField(TEXT("previews"), BuildPreviewSummaries(PIEWorld));
		Result->SetArrayField(TEXT("root_widgets"), BuildRootWidgetSummaries(PIEWorld));
	}
	return FBridgeToolResult::Json(Result);
}

FString UUMGPreviewListTool::GetToolDescription() const
{
	return TEXT("List tool-owned UMG preview widgets, optionally scoped to one active PIE viewport.");
}

TMap<FString, FBridgeSchemaProperty> UUMGPreviewListTool::GetInputSchema() const
{
	return PreviewListSchema();
}

FBridgeToolResult UUMGPreviewListTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	UWorld* PIEWorld = nullptr;
	int32 PieIndex = 0;
	const bool bHasPieIndex = Arguments.IsValid() && Arguments->HasField(TEXT("pie_index"));
	if (bHasPieIndex)
	{
		FBridgeToolResult WorldResult = ResolvePIEWorldForTool(Arguments, PIEWorld, PieIndex);
		if (!PIEWorld)
		{
			return WorldResult;
		}
	}

	TArray<FWidgetPreviewSummary> PreviewSummaries;
	FWidgetPreviewRegistry::ListPreviewsForWorld(PIEWorld, PreviewSummaries);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action"), TEXT("list"));
	Result->SetNumberField(TEXT("preview_count"), PreviewSummaries.Num());
	Result->SetArrayField(TEXT("previews"), BuildPreviewSummaries(PIEWorld));
	if (PIEWorld)
	{
		Result->SetNumberField(TEXT("pie_index"), PieIndex);
		Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
		Result->SetNumberField(TEXT("tool_preview_count"), FWidgetPreviewRegistry::CountPreviewsForWorld(PIEWorld));
		Result->SetArrayField(TEXT("root_widgets"), BuildRootWidgetSummaries(PIEWorld));
	}
	return FBridgeToolResult::Json(Result);
}
