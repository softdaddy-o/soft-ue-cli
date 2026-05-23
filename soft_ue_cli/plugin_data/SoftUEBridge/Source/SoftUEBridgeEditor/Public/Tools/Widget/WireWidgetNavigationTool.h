// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "WireWidgetNavigationTool.generated.h"

class UWidget;
class UWidgetBlueprint;
class UWidgetTree;

/**
 * Validate UMG navigation button contracts and expose named widgets for parent-class binding.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UWireWidgetNavigationTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("wire-widget-navigation"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UWidget* FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name) const;
	bool EnsureVariable(UWidget* Widget, TArray<TSharedPtr<FJsonValue>>& OutChangedWidgets) const;
};
