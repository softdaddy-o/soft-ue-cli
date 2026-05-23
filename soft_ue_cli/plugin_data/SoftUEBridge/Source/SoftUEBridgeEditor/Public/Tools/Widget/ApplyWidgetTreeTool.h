// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "ApplyWidgetTreeTool.generated.h"

class UClass;
class UPanelSlot;
class UWidget;
class UWidgetBlueprint;
class UWidgetTree;

/**
 * Build or replace a WidgetBlueprint Designer tree from a declarative JSON spec.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UApplyWidgetTreeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("apply-widget-tree"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UWidget* BuildWidgetFromSpec(
		UWidgetBlueprint* WidgetBP,
		UWidgetTree* WidgetTree,
		const TSharedPtr<FJsonObject>& NodeSpec,
		UWidget* ParentWidget,
		TArray<TSharedPtr<FJsonValue>>& OutCreatedWidgets,
		FString& OutError);

	UClass* ResolveWidgetClass(const FString& WidgetClass, FString& OutError) const;
	bool AttachToParent(UWidget* ParentWidget, UWidget* ChildWidget, FString& OutError) const;
	bool ApplyWidgetProperties(UWidget* Widget, const TSharedPtr<FJsonObject>& NodeSpec, FString& OutError) const;
	bool ApplySlotProperties(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& SlotSpec, FString& OutError) const;
};
