// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UMaterial;

/**
 * Utility class for intelligent graph node positioning.
 * Provides auto-layout algorithms to place new nodes without overlapping existing ones.
 */
class SOFTUEBRIDGEEDITOR_API FBridgeGraphLayoutUtil
{
public:
	/**
	 * Calculate optimal position for a new Blueprint node.
	 *
	 * @param Graph - The graph to add the node to
	 * @param TargetNode - Optional node to position relative to (for connection-based layout)
	 * @param TargetPin - Optional pin to connect to (determines offset direction)
	 * @param NodeSize - Approximate size of the new node (default: 200x100)
	 * @return Calculated position as FVector2D
	 */
	static FVector2D CalculateBlueprintNodePosition(
		UEdGraph* Graph,
		UEdGraphNode* TargetNode = nullptr,
		UEdGraphPin* TargetPin = nullptr,
		const FVector2D& NodeSize = FVector2D(200, 100));

	/**
	 * Calculate optimal position for a new Material expression.
	 *
	 * @param Material - The material to add the expression to
	 * @param NodeSize - Approximate size of the new expression (default: 150x80)
	 * @return Calculated position as FVector2D
	 */
	static FVector2D CalculateMaterialExpressionPosition(
		UMaterial* Material,
		const FVector2D& NodeSize = FVector2D(150, 80));

	/**
	 * Find empty space in a Blueprint graph by analyzing existing node positions.
	 *
	 * @param Graph - The graph to analyze
	 * @param NodeSize - Size of the node we want to place
	 * @param PreferredRegion - Preferred region to search (0,0 = top-left of existing nodes)
	 * @return Position with sufficient empty space
	 */
	static FVector2D FindEmptySpaceInGraph(
		UEdGraph* Graph,
		const FVector2D& NodeSize,
		const FVector2D& PreferredRegion = FVector2D::ZeroVector);

	/**
	 * Find empty space in a Material graph by analyzing existing expression positions.
	 *
	 * @param Material - The material to analyze
	 * @param NodeSize - Size of the expression we want to place
	 * @return Position with sufficient empty space
	 */
	static FVector2D FindEmptySpaceInMaterial(
		UMaterial* Material,
		const FVector2D& NodeSize);

	/**
	 * Calculate position relative to a target node based on pin direction.
	 * Execution pins flow left-to-right, data pins flow top-to-bottom.
	 *
	 * @param TargetNode - Node to position relative to
	 * @param TargetPin - Pin we'll connect to (determines direction)
	 * @param NodeSize - Size of the new node
	 * @param Spacing - Spacing between nodes (default: 100 pixels)
	 * @return Calculated relative position
	 */
	static FVector2D CalculateRelativePosition(
		UEdGraphNode* TargetNode,
		UEdGraphPin* TargetPin,
		const FVector2D& NodeSize,
		float Spacing = 100.0f);

	/**
	 * Check if a position would overlap with existing nodes.
	 *
	 * @param Graph - Graph to check
	 * @param Position - Position to test
	 * @param NodeSize - Size of the node
	 * @param Padding - Additional padding around nodes (default: 20 pixels)
	 * @return true if position overlaps with existing nodes
	 */
	static bool DoesPositionOverlap(
		UEdGraph* Graph,
		const FVector2D& Position,
		const FVector2D& NodeSize,
		float Padding = 20.0f);

	/**
	 * Get the bounds of all nodes in a graph.
	 *
	 * @param Graph - Graph to analyze
	 * @param OutMin - Output minimum bounds
	 * @param OutMax - Output maximum bounds
	 * @return true if graph has nodes, false if empty
	 */
	static bool GetGraphBounds(
		UEdGraph* Graph,
		FVector2D& OutMin,
		FVector2D& OutMax);

private:
	// Default node sizes for common node types
	static constexpr float DEFAULT_NODE_WIDTH = 200.0f;
	static constexpr float DEFAULT_NODE_HEIGHT = 100.0f;
	static constexpr float DEFAULT_SPACING = 100.0f;
	static constexpr float DEFAULT_PADDING = 20.0f;

	// Material expression default sizes
	static constexpr float DEFAULT_EXPRESSION_WIDTH = 150.0f;
	static constexpr float DEFAULT_EXPRESSION_HEIGHT = 80.0f;
};
