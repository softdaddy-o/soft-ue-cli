// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Widget/InspectRuntimeWidgetsTool.h"
#include "Tools/Widget/WidgetToolUtils.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SWidget.h"
#include "Layout/Geometry.h"

FString UInspectRuntimeWidgetsTool::GetToolDescription() const
{
	return TEXT("Inspect live UMG widget geometry during PIE sessions. Walks the runtime widget tree returning computed geometry (absolute position, local size), slot properties, render settings, and optionally Slate widget data. Query by widget name or class with keyword search. Requires an active PIE session.");
}

TMap<FString, FBridgeSchemaProperty> UInspectRuntimeWidgetsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Filter;
	Filter.Type = TEXT("string");
	Filter.Description = TEXT("Keyword search — substring match (case-insensitive) against widget name and class name. Also matches Slate widget type when include_slate is true.");
	Filter.bRequired = false;
	Schema.Add(TEXT("filter"), Filter);

	FBridgeSchemaProperty ClassFilter;
	ClassFilter.Type = TEXT("string");
	ClassFilter.Description = TEXT("Filter by UWidget class name (substring match)");
	ClassFilter.bRequired = false;
	Schema.Add(TEXT("class_filter"), ClassFilter);

	FBridgeSchemaProperty DepthLimit;
	DepthLimit.Type = TEXT("integer");
	DepthLimit.Description = TEXT("Max hierarchy depth (-1 = unlimited, default: -1)");
	DepthLimit.bRequired = false;
	Schema.Add(TEXT("depth_limit"), DepthLimit);

	FBridgeSchemaProperty IncludeSlate;
	IncludeSlate.Type = TEXT("boolean");
	IncludeSlate.Description = TEXT("Include underlying SWidget geometry for each UWidget (default: false)");
	IncludeSlate.bRequired = false;
	Schema.Add(TEXT("include_slate"), IncludeSlate);

	FBridgeSchemaProperty PieIndex;
	PieIndex.Type = TEXT("integer");
	PieIndex.Description = TEXT("PIE instance index, 0-based (default: 0)");
	PieIndex.bRequired = false;
	Schema.Add(TEXT("pie_index"), PieIndex);

	FBridgeSchemaProperty IncludeGeometry;
	IncludeGeometry.Type = TEXT("boolean");
	IncludeGeometry.Description = TEXT("Include computed geometry (default: true)");
	IncludeGeometry.bRequired = false;
	Schema.Add(TEXT("include_geometry"), IncludeGeometry);

	FBridgeSchemaProperty IncludeProperties;
	IncludeProperties.Type = TEXT("boolean");
	IncludeProperties.Description = TEXT("Include render transform, opacity, visibility, enabled state (default: true)");
	IncludeProperties.bRequired = false;
	Schema.Add(TEXT("include_properties"), IncludeProperties);

	FBridgeSchemaProperty RootWidget;
	RootWidget.Type = TEXT("string");
	RootWidget.Description = TEXT("Start traversal from a specific widget by name instead of all viewport roots");
	RootWidget.bRequired = false;
	Schema.Add(TEXT("root_widget"), RootWidget);

	return Schema;
}

FBridgeToolResult UInspectRuntimeWidgetsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Check PIE is running
	if (!GEditor || !GEditor->IsPlaySessionInProgress())
	{
		return FBridgeToolResult::Error(TEXT("No PIE session running. Start one with 'pie-session --action start'."));
	}

	// Get PIE world
	int32 PieIndex = GetIntArgOrDefault(Arguments, TEXT("pie_index"), 0);
	int32 TotalPIEWorlds = 0;
	UWorld* PIEWorld = GetPIEWorldByIndex(PieIndex, TotalPIEWorlds);

	if (!PIEWorld)
	{
		if (TotalPIEWorlds == 0)
		{
			return FBridgeToolResult::Error(TEXT("No PIE worlds found. Ensure Play In Editor is running."));
		}
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("PIE instance %d not found. %d instance(s) running."),
			PieIndex, TotalPIEWorlds));
	}

	// Collect root widgets
	TArray<UUserWidget*> RootWidgets = CollectPIEWidgets(PIEWorld);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("inspect-runtime-widgets: PIE index=%d, world='%s', root_widgets=%d"),
		PieIndex, *PIEWorld->GetName(), RootWidgets.Num());

	// Get parameters
	FString Filter = GetStringArgOrDefault(Arguments, TEXT("filter"));
	FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"));
	int32 DepthLimit = GetIntArgOrDefault(Arguments, TEXT("depth_limit"), -1);
	bool bIncludeSlate = GetBoolArgOrDefault(Arguments, TEXT("include_slate"), false);
	bool bIncludeGeometry = GetBoolArgOrDefault(Arguments, TEXT("include_geometry"), true);
	bool bIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), true);
	FString RootWidgetName = GetStringArgOrDefault(Arguments, TEXT("root_widget"));

	int32 MaxDepth = DepthLimit < 0 ? INT32_MAX : DepthLimit;
	bool bHasFilter = !Filter.IsEmpty() || !ClassFilter.IsEmpty();

	// If root_widget is specified, find it
	if (!RootWidgetName.IsEmpty())
	{
		UWidget* FoundRoot = nullptr;
		for (UUserWidget* Root : RootWidgets)
		{
			FoundRoot = FindWidgetByName(Root, RootWidgetName);
			if (FoundRoot)
			{
				break;
			}
		}

		if (!FoundRoot)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Widget '%s' not found in the widget tree."), *RootWidgetName));
		}

		// Replace root widgets with just this one, wrapped in a temporary array
		RootWidgets.Empty();
		// We need to walk from the found widget, so we handle it below
		TArray<TSharedPtr<FJsonValue>> RootArray;
		int32 WidgetCount = 0;
		int32 MatchedCount = 0;

		bool bHasMatch = false;
		TSharedPtr<FJsonObject> WidgetNode = BuildWidgetNode(
			FoundRoot, 0, MaxDepth, bIncludeGeometry, bIncludeProperties,
			bIncludeSlate, Filter, ClassFilter, bHasMatch);

		if (WidgetNode.IsValid())
		{
			RootArray.Add(MakeShareable(new FJsonValueObject(WidgetNode)));
			// Count widgets in subtree
			TFunction<void(const TSharedPtr<FJsonObject>&)> CountWidgets = [&](const TSharedPtr<FJsonObject>& Node)
			{
				WidgetCount++;
				if (bHasFilter && Node->HasField(TEXT("matched")) && Node->GetBoolField(TEXT("matched")))
				{
					MatchedCount++;
				}
				const TArray<TSharedPtr<FJsonValue>>* Children;
				if (Node->TryGetArrayField(TEXT("children"), Children))
				{
					for (const auto& Child : *Children)
					{
						CountWidgets(Child->AsObject());
					}
				}
			};
			CountWidgets(WidgetNode);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("pie_index"), PieIndex);
		Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
		Result->SetNumberField(TEXT("widget_count"), WidgetCount);
		if (bHasFilter)
		{
			Result->SetNumberField(TEXT("matched_count"), MatchedCount);
		}
		Result->SetArrayField(TEXT("root_widgets"), RootArray);

		return FBridgeToolResult::Json(Result);
	}

	// Walk all root widgets
	TArray<TSharedPtr<FJsonValue>> RootArray;
	int32 WidgetCount = 0;
	int32 MatchedCount = 0;

	for (UUserWidget* Root : RootWidgets)
	{
		bool bHasMatch = false;
		TSharedPtr<FJsonObject> WidgetNode = BuildWidgetNode(
			Root, 0, MaxDepth, bIncludeGeometry, bIncludeProperties,
			bIncludeSlate, Filter, ClassFilter, bHasMatch);

		if (WidgetNode.IsValid())
		{
			RootArray.Add(MakeShareable(new FJsonValueObject(WidgetNode)));

			// Count widgets in subtree
			TFunction<void(const TSharedPtr<FJsonObject>&)> CountWidgets = [&](const TSharedPtr<FJsonObject>& Node)
			{
				WidgetCount++;
				if (bHasFilter && Node->HasField(TEXT("matched")) && Node->GetBoolField(TEXT("matched")))
				{
					MatchedCount++;
				}
				const TArray<TSharedPtr<FJsonValue>>* Children;
				if (Node->TryGetArrayField(TEXT("children"), Children))
				{
					for (const auto& Child : *Children)
					{
						CountWidgets(Child->AsObject());
					}
				}
			};
			CountWidgets(WidgetNode);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetNumberField(TEXT("pie_index"), PieIndex);
	Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	Result->SetNumberField(TEXT("widget_count"), WidgetCount);
	if (bHasFilter)
	{
		Result->SetNumberField(TEXT("matched_count"), MatchedCount);
	}
	Result->SetArrayField(TEXT("root_widgets"), RootArray);

	return FBridgeToolResult::Json(Result);
}

UWorld* UInspectRuntimeWidgetsTool::GetPIEWorldByIndex(int32 Index, int32& OutTotalCount) const
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

TArray<UUserWidget*> UInspectRuntimeWidgetsTool::CollectPIEWidgets(UWorld* PIEWorld) const
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

UWidget* UInspectRuntimeWidgetsTool::FindWidgetByName(UWidget* Root, const FString& Name) const
{
	if (!Root)
	{
		return nullptr;
	}

	if (Root->GetName().Equals(Name, ESearchCase::IgnoreCase))
	{
		return Root;
	}

	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Root))
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); i++)
		{
			UWidget* Found = FindWidgetByName(PanelWidget->GetChildAt(i), Name);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> UInspectRuntimeWidgetsTool::BuildWidgetNode(
	UWidget* Widget,
	int32 CurrentDepth,
	int32 MaxDepth,
	bool bIncludeGeometry,
	bool bIncludeProperties,
	bool bIncludeSlate,
	const FString& Filter,
	const FString& ClassFilter,
	bool& bOutHasMatch) const
{
	if (!Widget)
	{
		bOutHasMatch = false;
		return nullptr;
	}

	bool bHasFilter = !Filter.IsEmpty() || !ClassFilter.IsEmpty();
	bool bThisMatches = MatchesFilter(Widget, Filter, ClassFilter, bIncludeSlate);

	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);

	// Basic info
	NodeObj->SetStringField(TEXT("name"), Widget->GetName());
	NodeObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("visibility"), WidgetToolUtils::VisibilityToString(Widget->GetVisibility()));

	if (bHasFilter)
	{
		NodeObj->SetBoolField(TEXT("matched"), bThisMatches);
	}

	// Geometry
	if (bIncludeGeometry)
	{
		TSharedPtr<FJsonObject> GeomObj = ExtractGeometry(Widget);
		if (GeomObj.IsValid())
		{
			NodeObj->SetObjectField(TEXT("geometry"), GeomObj);
		}
		else
		{
			NodeObj->SetBoolField(TEXT("geometry_unavailable"), true);
		}
	}

	// Slot information
	if (Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotInfo = WidgetToolUtils::ExtractSlotInfo(Widget->Slot);
		if (SlotInfo.IsValid())
		{
			NodeObj->SetObjectField(TEXT("slot"), SlotInfo);
		}
	}

	// Properties
	if (bIncludeProperties)
	{
		TSharedPtr<FJsonObject> PropsObj = ExtractProperties(Widget);
		if (PropsObj.IsValid())
		{
			NodeObj->SetObjectField(TEXT("properties"), PropsObj);
		}
	}

	// Slate data
	if (bIncludeSlate)
	{
		TSharedPtr<FJsonObject> SlateObj = ExtractSlateData(Widget);
		if (SlateObj.IsValid())
		{
			NodeObj->SetObjectField(TEXT("slate"), SlateObj);
		}
	}

	// Track if this subtree has any match
	bool bSubtreeHasMatch = bThisMatches;

	// Children (always present, empty array for leaf widgets)
	TArray<TSharedPtr<FJsonValue>> ChildrenArray;
	if (CurrentDepth < MaxDepth)
	{
		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); i++)
			{
				UWidget* Child = PanelWidget->GetChildAt(i);
				if (Child)
				{
					bool bChildHasMatch = false;
					TSharedPtr<FJsonObject> ChildNode = BuildWidgetNode(
						Child, CurrentDepth + 1, MaxDepth, bIncludeGeometry,
						bIncludeProperties, bIncludeSlate, Filter, ClassFilter,
						bChildHasMatch);

					if (ChildNode.IsValid())
					{
						if (!bHasFilter || bChildHasMatch)
						{
							ChildrenArray.Add(MakeShareable(new FJsonValueObject(ChildNode)));
						}
					}

					if (bChildHasMatch)
					{
						bSubtreeHasMatch = true;
					}
				}
			}
		}
	}
	NodeObj->SetArrayField(TEXT("children"), ChildrenArray);

	bOutHasMatch = bSubtreeHasMatch;

	// When filtering, prune subtrees with no matches
	if (bHasFilter && !bSubtreeHasMatch)
	{
		return nullptr;
	}

	return NodeObj;
}

bool UInspectRuntimeWidgetsTool::MatchesFilter(UWidget* Widget, const FString& Filter,
	const FString& ClassFilter, bool bIncludeSlate) const
{
	if (Filter.IsEmpty() && ClassFilter.IsEmpty())
	{
		return true;
	}

	FString WidgetName = Widget->GetName();
	FString ClassName = Widget->GetClass()->GetName();

	bool bFilterMatch = true;
	bool bClassFilterMatch = true;

	if (!Filter.IsEmpty())
	{
		bFilterMatch = WidgetName.Contains(Filter, ESearchCase::IgnoreCase)
			|| ClassName.Contains(Filter, ESearchCase::IgnoreCase);

		if (!bFilterMatch && bIncludeSlate)
		{
			TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
			if (SlateWidget.IsValid())
			{
				FString SlateType = SlateWidget->GetType().ToString();
				bFilterMatch = SlateType.Contains(Filter, ESearchCase::IgnoreCase);
			}
		}
	}

	if (!ClassFilter.IsEmpty())
	{
		bClassFilterMatch = ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase);
	}

	return bFilterMatch && bClassFilterMatch;
}

TSharedPtr<FJsonObject> UInspectRuntimeWidgetsTool::ExtractGeometry(UWidget* Widget) const
{
	TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return nullptr;
	}

	const FGeometry& Geom = SlateWidget->GetCachedGeometry();
	FVector2D LocalSize = Geom.GetLocalSize();

	if (LocalSize.IsNearlyZero())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> GeomObj = MakeShareable(new FJsonObject);

	GeomObj->SetArrayField(TEXT("absolute_position"),
		WidgetToolUtils::Vector2dToJsonArray(FVector2D(Geom.GetAbsolutePosition())));
	GeomObj->SetArrayField(TEXT("local_size"),
		WidgetToolUtils::Vector2dToJsonArray(LocalSize));

	const FSlateRenderTransform& RT = Geom.GetAccumulatedRenderTransform();
	const FVector2D Row0 = RT.GetMatrix().TransformVector(FVector2D(1.0, 0.0));
	const FVector2D Row1 = RT.GetMatrix().TransformVector(FVector2D(0.0, 1.0));
	FString TransformStr = FString::Printf(TEXT("[[%.2f,%.2f],[%.2f,%.2f],[%.2f,%.2f]]"),
		Row0.X, Row0.Y,
		Row1.X, Row1.Y,
		RT.GetTranslation().X, RT.GetTranslation().Y);
	GeomObj->SetStringField(TEXT("accumulated_render_transform"), TransformStr);

	return GeomObj;
}

TSharedPtr<FJsonObject> UInspectRuntimeWidgetsTool::ExtractProperties(UWidget* Widget) const
{
	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);

	FWidgetTransform RenderTransform = Widget->GetRenderTransform();
	TSharedPtr<FJsonObject> TransformObj = MakeShareable(new FJsonObject);
	TransformObj->SetArrayField(TEXT("translation"),
		WidgetToolUtils::Vector2dToJsonArray(RenderTransform.Translation));
	TransformObj->SetNumberField(TEXT("angle"), RenderTransform.Angle);
	TransformObj->SetArrayField(TEXT("scale"),
		WidgetToolUtils::Vector2dToJsonArray(RenderTransform.Scale));
	TransformObj->SetArrayField(TEXT("shear"),
		WidgetToolUtils::Vector2dToJsonArray(RenderTransform.Shear));
	PropsObj->SetObjectField(TEXT("render_transform"), TransformObj);

	PropsObj->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
	PropsObj->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());

	return PropsObj;
}

TSharedPtr<FJsonObject> UInspectRuntimeWidgetsTool::ExtractSlateData(UWidget* Widget) const
{
	TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SlateObj = MakeShareable(new FJsonObject);
	SlateObj->SetStringField(TEXT("slate_widget_type"), SlateWidget->GetType().ToString());

	SlateObj->SetArrayField(TEXT("desired_size"),
		WidgetToolUtils::Vector2dToJsonArray(SlateWidget->GetDesiredSize()));

	const FGeometry& TickGeom = SlateWidget->GetTickSpaceGeometry();
	TSharedPtr<FJsonObject> TickGeomObj = MakeShareable(new FJsonObject);
	TickGeomObj->SetArrayField(TEXT("position"),
		WidgetToolUtils::Vector2dToJsonArray(FVector2D(TickGeom.GetAbsolutePosition())));
	TickGeomObj->SetArrayField(TEXT("size"),
		WidgetToolUtils::Vector2dToJsonArray(TickGeom.GetLocalSize()));
	SlateObj->SetObjectField(TEXT("tick_geometry"), TickGeomObj);

	return SlateObj;
}
