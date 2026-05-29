// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InspectMetaSoundTool.generated.h"

/**
 * Read-only inspection of a MetaSound Source or Patch asset.
 *
 * Thin shell (side effects only): resolves the asset, obtains the const Frontend document
 * via IMetaSoundDocumentInterface, and delegates all serialization to the pure
 * MetaSoundGraphSerializer so the logic stays unit-testable.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInspectMetaSoundTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("metasound-inspect"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
