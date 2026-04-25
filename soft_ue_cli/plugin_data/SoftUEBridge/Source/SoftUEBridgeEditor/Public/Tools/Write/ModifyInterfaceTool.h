// Copyright soft-ue-expert. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "ModifyInterfaceTool.generated.h"

/**
 * Add or remove implemented interfaces on a Blueprint or AnimBlueprint.
 * Uses FBlueprintEditorUtils for safe interface manipulation with undo support.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UModifyInterfaceTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("modify-interface"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Resolve an interface class from a user-provided string (class name or full path) */
	UClass* ResolveInterfaceClass(const FString& InterfaceClassStr, FString& OutError) const;
};
