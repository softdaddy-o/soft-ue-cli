// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "QueryBlueprintTool.generated.h"

/**
 * Consolidated tool for Blueprint structure analysis.
 * Replaces: analyze-blueprint, get-blueprint-functions, get-blueprint-variables,
 *           get-blueprint-components, get-blueprint-defaults
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UQueryBlueprintTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-blueprint"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// === Structure extraction ===

	/** Extract function information */
	TSharedPtr<FJsonObject> ExtractFunctions(class UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter = TEXT("")) const;

	/** Extract variable information */
	TSharedPtr<FJsonObject> ExtractVariables(class UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter = TEXT("")) const;

	/** Extract component information */
	TSharedPtr<FJsonObject> ExtractComponents(class UBlueprint* Blueprint, bool bDetailed, const FString& SearchFilter = TEXT("")) const;

	/** Extract event graph summary */
	TSharedPtr<FJsonObject> ExtractEventGraphSummary(class UBlueprint* Blueprint) const;

	/** Extract CDO defaults */
	TSharedPtr<FJsonObject> ExtractDefaults(class UBlueprint* Blueprint, bool bIncludeInherited, const FString& CategoryFilter, const FString& PropertyFilter) const;

	/** Extract component instance overrides */
	TSharedPtr<FJsonObject> ExtractComponentOverrides(class UBlueprint* Blueprint, const FString& ComponentFilter, const FString& PropertyFilter, bool bIncludeNonOverridden) const;

	// === Helpers ===

	/** Convert property to JSON */
	TSharedPtr<FJsonObject> PropertyToJson(class FProperty* Property, void* Container, UObject* Owner) const;

	/** Get property type as string */
	FString GetPropertyTypeString(class FProperty* Property) const;
};
