// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "PieSessionTool.generated.h"

/**
 * Consolidated PIE session control tool.
 * Actions: start, stop, pause, resume, get-state, wait-for
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UPieSessionTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-session"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("action")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	FBridgeToolResult ExecuteStart(const TSharedPtr<FJsonObject>& Arguments);
	FBridgeToolResult ExecuteStop(const TSharedPtr<FJsonObject>& Arguments);
	FBridgeToolResult ExecutePause(const TSharedPtr<FJsonObject>& Arguments);
	FBridgeToolResult ExecuteResume(const TSharedPtr<FJsonObject>& Arguments);
	FBridgeToolResult ExecuteGetState(const TSharedPtr<FJsonObject>& Arguments);
	FBridgeToolResult ExecuteWaitFor(const TSharedPtr<FJsonObject>& Arguments);

	FString GenerateSessionId() const;
	AActor* FindActorByName(UWorld* World, const FString& ActorName) const;
	TSharedPtr<FJsonValue> GetActorProperty(AActor* Actor, const FString& PropertyName) const;
	UWorld* GetPIEWorld() const;
	bool WaitForPIEReady(float TimeoutSeconds) const;
	TSharedPtr<FJsonObject> GetWorldInfo(UWorld* PIEWorld) const;
	TArray<TSharedPtr<FJsonValue>> GetPlayersInfo(UWorld* PIEWorld) const;
};
