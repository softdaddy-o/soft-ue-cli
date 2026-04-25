// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "Tools/BridgeToolBase.h"
#include "TriggerInputTool.generated.h"

/**
 * Simulate player input in PIE or packaged builds.
 * Actions: key, action, move-to, look-at
 */
UCLASS()
class UTriggerInputTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("trigger-input"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("action")}; }
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;

private:
	FBridgeToolResult ExecuteKey(const TSharedPtr<FJsonObject>& Args, UWorld* World);
	FBridgeToolResult ExecuteAction(const TSharedPtr<FJsonObject>& Args, UWorld* World);
	FBridgeToolResult ExecuteMoveTo(const TSharedPtr<FJsonObject>& Args, UWorld* World);
	FBridgeToolResult ExecuteLookAt(const TSharedPtr<FJsonObject>& Args, UWorld* World);

	/** Find the best game world — prefers Game, falls back to PIE. */
	static UWorld* FindGameWorld();
	static APlayerController* GetPlayerController(UWorld* World, int32 PlayerIndex);
};
