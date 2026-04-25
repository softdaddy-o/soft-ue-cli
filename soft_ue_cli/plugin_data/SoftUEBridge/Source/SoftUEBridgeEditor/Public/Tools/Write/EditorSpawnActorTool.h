// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/SpawnActorTool.h"
#include "EditorSpawnActorTool.generated.h"

/**
 * Editor override of USpawnActorTool.
 * - editor world: wraps spawn in FScopedTransaction, sets actor label, marks level dirty.
 * - pie / game world: delegates to Super::Execute() (runtime path, no transaction).
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UEditorSpawnActorTool : public USpawnActorTool
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("spawn-actor"); }
	virtual FString GetToolDescription() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("actor_class")}; }
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
