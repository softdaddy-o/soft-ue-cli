// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InspectRuntimeWidgetsTool.generated.h"

class UWidget;
class UUserWidget;
class UPanelWidget;

/**
 * Inspects live UMG widget geometry during PIE sessions.
 * Walks the runtime widget tree and returns computed layout,
 * slot info, properties, and optionally Slate widget data.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInspectRuntimeWidgetsTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("inspect-runtime-widgets"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Find PIE world by index (0-based) */
	UWorld* GetPIEWorldByIndex(int32 Index, int32& OutTotalCount) const;

	/** Collect all top-level UUserWidget instances in the given PIE world */
	TArray<UUserWidget*> CollectPIEWidgets(UWorld* PIEWorld) const;

	/** Find a widget by name in the tree, searching recursively */
	UWidget* FindWidgetByName(UWidget* Root, const FString& Name) const;

	/** Build a JSON node for a widget, recursively including children */
	TSharedPtr<FJsonObject> BuildWidgetNode(
		UWidget* Widget,
		int32 CurrentDepth,
		int32 MaxDepth,
		bool bIncludeGeometry,
		bool bIncludeProperties,
		bool bIncludeSlate,
		const FString& Filter,
		const FString& ClassFilter,
		bool& bOutHasMatch) const;

	/** Check if a widget matches the filter criteria */
	bool MatchesFilter(UWidget* Widget, const FString& Filter,
		const FString& ClassFilter, bool bIncludeSlate) const;

	/** Extract computed geometry from the underlying Slate widget */
	TSharedPtr<FJsonObject> ExtractGeometry(UWidget* Widget) const;

	/** Extract widget properties (render transform, opacity, enabled) */
	TSharedPtr<FJsonObject> ExtractProperties(UWidget* Widget) const;

	/** Extract Slate widget data */
	TSharedPtr<FJsonObject> ExtractSlateData(UWidget* Widget) const;
};
