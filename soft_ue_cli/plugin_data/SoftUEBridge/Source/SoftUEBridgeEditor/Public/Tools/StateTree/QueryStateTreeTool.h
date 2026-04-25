// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "QueryStateTreeTool.generated.h"

class UStateTree;
struct FStateTreeStateLink;

/**
 * Tool for querying StateTree structure: states, transitions, tasks, evaluators, and parameters.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UQueryStateTreeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-statetree"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/** Extract all states from the StateTree */
	TSharedPtr<FJsonObject> ExtractStates(UStateTree* StateTree, bool bDetailed) const;

	/** Extract transitions for all states */
	TSharedPtr<FJsonObject> ExtractTransitions(UStateTree* StateTree) const;

	/** Extract tasks from the StateTree */
	TSharedPtr<FJsonObject> ExtractTasks(UStateTree* StateTree) const;

	/** Extract evaluators from the StateTree */
	TSharedPtr<FJsonObject> ExtractEvaluators(UStateTree* StateTree) const;

	/** Extract schema/parameters from the StateTree */
	TSharedPtr<FJsonObject> ExtractParameters(UStateTree* StateTree) const;

	/** Get state type as string */
	FString GetStateTypeString(uint8 StateType) const;

	/** Get selection behavior as string */
	FString GetSelectionBehaviorString(uint8 SelectionBehavior) const;
};
