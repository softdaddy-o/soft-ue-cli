// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "EditorSetPropertyTool.generated.h"

/**
 * Universal property setter tool (editor version).
 * Sets any property on any asset type using UE reflection.
 *
 * Supports:
 * - Simple properties (int, float, bool, string, name, text)
 * - Nested properties via dot notation (e.g., "Stats.MaxHealth")
 * - Array elements via bracket notation (e.g., "Items[0].Value")
 * - Struct properties (as JSON objects or arrays for FVector, FRotator, FLinearColor)
 * - Enum properties (by name or integer value)
 *
 * Examples:
 * - Blueprint variable: { "asset_path": "/Game/BP_Player", "property_path": "Health", "value": 100 }
 * - Nested struct: { "asset_path": "/Game/BP_Player", "property_path": "Stats.MaxHealth", "value": 200 }
 * - Vector: { "asset_path": "/Game/BP_Actor", "property_path": "Location", "value": [100, 200, 0] }
 * - Enum: { "asset_path": "/Game/BP_Actor", "property_path": "State", "value": "Active" }
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UEditorSetPropertyTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("set-asset-property"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
