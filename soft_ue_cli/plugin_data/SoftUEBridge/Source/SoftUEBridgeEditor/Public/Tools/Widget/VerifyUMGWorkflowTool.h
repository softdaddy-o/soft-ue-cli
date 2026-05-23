// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "VerifyUMGWorkflowTool.generated.h"

class UButton;
class UTextBlock;
class UUserWidget;
class UWidget;
class UWidgetSwitcher;

/**
 * Validate a UMG workflow in PIE using real CreateWidget/add-to-viewport and named button clicks.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UVerifyUMGWorkflowTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("verify-umg-workflow"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	UWorld* GetPIEWorldByIndex(int32 Index, int32& OutTotalCount) const;
	UClass* ResolveWidgetClass(const FString& WidgetClassPath, FString& OutError) const;
	TArray<UUserWidget*> CollectPIEWidgets(UWorld* PIEWorld) const;
	UWidget* FindWidgetByName(UWidget* Root, const FString& Name) const;
	UTextBlock* FindTextBlockByText(UWidget* Root, const FString& Text) const;
	bool IsRuntimeVisible(UWidget* Widget) const;
};
