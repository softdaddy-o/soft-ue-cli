// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "PieInputTool.generated.h"

/**
 * Consolidated PIE input simulation tool.
 * Actions: key, action, axis, move-to, look-at
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UPieInputTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-input"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("action")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	FBridgeToolResult ExecuteKey(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FBridgeToolResult ExecuteAction(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FBridgeToolResult ExecuteAxis(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FBridgeToolResult ExecuteMoveTo(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FBridgeToolResult ExecuteLookAt(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);

	UWorld* GetPIEWorld() const;
	APlayerController* GetPlayerController(UWorld* PIEWorld, int32 PlayerIndex) const;
};
