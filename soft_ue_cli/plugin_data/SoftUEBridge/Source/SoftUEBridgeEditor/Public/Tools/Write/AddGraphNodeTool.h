// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "AddGraphNodeTool.generated.h"

class UMaterial;
class UBlueprint;
class UEdGraph;
class UMaterialExpression;
class UEdGraphNode;

/**
 * Add a node to a Blueprint or Material graph.
 * Uses dynamic class resolution - accepts any material expression or Blueprint node class name.
 *
 * For Materials: Use expression class names like "MaterialExpressionAdd",
 * "MaterialExpressionSceneTexture", "MaterialExpressionCollectionParameter", etc.
 *
 * For Blueprints: Use node class names like "K2Node_CallFunction",
 * "K2Node_VariableGet", "K2Node_Event", etc.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UAddGraphNodeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("add-graph-node"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// Create a material expression node by class name
	UMaterialExpression* CreateMaterialExpression(
		UMaterial* Material,
		const FString& NodeClassName,
		const FVector2D& Position,
		bool bAutoPosition,
		const TSharedPtr<FJsonObject>& Properties,
		FString& OutError);

	// Create a Blueprint node by class name
	UEdGraphNode* CreateBlueprintNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FString& NodeClassName,
		const FVector2D& Position,
		bool bAutoPosition,
		const FString& ConnectToNodeGuid,
		const FString& ConnectToPinName,
		const TSharedPtr<FJsonObject>& Properties,
		FString& OutError);

	// Apply properties to a node via reflection
	void ApplyNodeProperties(UObject* Node, const TSharedPtr<FJsonObject>& Properties);
};
