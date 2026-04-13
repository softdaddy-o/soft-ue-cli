// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "QueryStructTool.generated.h"

UCLASS()
class SOFTUEBRIDGEEDITOR_API UQueryStructTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-struct"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
