// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgeGraphLayoutUtil.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "SoftUEBridgeEditorModule.h"

FVector2D FBridgeGraphLayoutUtil::CalculateBlueprintNodePosition(
	UEdGraph* Graph,
	UEdGraphNode* TargetNode,
	UEdGraphPin* TargetPin,
	const FVector2D& NodeSize)
{
	if (!Graph)
	{
		return FVector2D::ZeroVector;
	}

	// Strategy 1: Connection-based positioning
	if (TargetNode && TargetPin)
	{
		FVector2D RelativePos = CalculateRelativePosition(TargetNode, TargetPin, NodeSize, DEFAULT_SPACING);

		// Check if this position overlaps with existing nodes
		if (!DoesPositionOverlap(Graph, RelativePos, NodeSize, DEFAULT_PADDING))
		{
			return RelativePos;
		}

		// If overlapping, try offsetting downward
		for (int32 Offset = 1; Offset <= 5; ++Offset)
		{
			FVector2D AdjustedPos = RelativePos + FVector2D(0, Offset * (NodeSize.Y + DEFAULT_PADDING));
			if (!DoesPositionOverlap(Graph, AdjustedPos, NodeSize, DEFAULT_PADDING))
			{
				return AdjustedPos;
			}
		}
	}

	// Strategy 2: If we have a target node but no pin, position to the right
	if (TargetNode)
	{
		FVector2D RightPos(TargetNode->NodePosX + DEFAULT_NODE_WIDTH + DEFAULT_SPACING, TargetNode->NodePosY);
		if (!DoesPositionOverlap(Graph, RightPos, NodeSize, DEFAULT_PADDING))
		{
			return RightPos;
		}
	}

	// Strategy 3: Find empty space in the graph
	return FindEmptySpaceInGraph(Graph, NodeSize, FVector2D::ZeroVector);
}

FVector2D FBridgeGraphLayoutUtil::CalculateMaterialExpressionPosition(
	UMaterial* Material,
	const FVector2D& NodeSize)
{
	if (!Material)
	{
		return FVector2D::ZeroVector;
	}

	return FindEmptySpaceInMaterial(Material, NodeSize);
}

FVector2D FBridgeGraphLayoutUtil::CalculateRelativePosition(
	UEdGraphNode* TargetNode,
	UEdGraphPin* TargetPin,
	const FVector2D& NodeSize,
	float Spacing)
{
	if (!TargetNode || !TargetPin)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D BasePos(TargetNode->NodePosX, TargetNode->NodePosY);

	// Determine offset based on pin direction and type
	bool bIsExecutionPin = (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
	bool bIsOutputPin = (TargetPin->Direction == EGPD_Output);
	bool bIsInputPin = (TargetPin->Direction == EGPD_Input);

	if (bIsExecutionPin)
	{
		// Execution flow: left-to-right
		if (bIsOutputPin)
		{
			// Place new node to the right
			return BasePos + FVector2D(DEFAULT_NODE_WIDTH + Spacing, 0);
		}
		else if (bIsInputPin)
		{
			// Place new node to the left
			return BasePos + FVector2D(-(NodeSize.X + Spacing), 0);
		}
	}
	else
	{
		// Data flow: typically top-to-bottom or right-to-left
		if (bIsInputPin)
		{
			// Input data pin - place new node to the left (data source)
			return BasePos + FVector2D(-(NodeSize.X + Spacing), 0);
		}
		else if (bIsOutputPin)
		{
			// Output data pin - place new node to the right or below
			return BasePos + FVector2D(DEFAULT_NODE_WIDTH + Spacing, 0);
		}
	}

	// Default: place to the right
	return BasePos + FVector2D(DEFAULT_NODE_WIDTH + Spacing, 0);
}

FVector2D FBridgeGraphLayoutUtil::FindEmptySpaceInGraph(
	UEdGraph* Graph,
	const FVector2D& NodeSize,
	const FVector2D& PreferredRegion)
{
	if (!Graph)
	{
		return FVector2D::ZeroVector;
	}

	// If graph is empty, return a default starting position
	if (Graph->Nodes.Num() == 0)
	{
		return FVector2D(0, 0);
	}

	// Get graph bounds
	FVector2D MinBounds, MaxBounds;
	if (!GetGraphBounds(Graph, MinBounds, MaxBounds))
	{
		return FVector2D(0, 0);
	}

	// Try positions in a grid pattern, starting from bottom-right of existing nodes
	const float GridSpacing = NodeSize.X + DEFAULT_SPACING;
	const float RowHeight = NodeSize.Y + DEFAULT_SPACING;

	// Start searching from the right edge of existing nodes
	FVector2D StartPos(MaxBounds.X + DEFAULT_SPACING, MinBounds.Y);

	// Try positions in columns, moving right and down
	for (int32 Col = 0; Col < 10; ++Col)
	{
		for (int32 Row = 0; Row < 10; ++Row)
		{
			FVector2D TestPos = StartPos + FVector2D(Col * GridSpacing, Row * RowHeight);
			if (!DoesPositionOverlap(Graph, TestPos, NodeSize, DEFAULT_PADDING))
			{
				return TestPos;
			}
		}
	}

	// Fallback: place far to the right
	return FVector2D(MaxBounds.X + DEFAULT_SPACING * 2, MinBounds.Y);
}

FVector2D FBridgeGraphLayoutUtil::FindEmptySpaceInMaterial(
	UMaterial* Material,
	const FVector2D& NodeSize)
{
	if (!Material)
	{
		return FVector2D::ZeroVector;
	}

	TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();

	// If material is empty, return a default starting position
	if (Expressions.Num() == 0)
	{
		return FVector2D(-400, 0); // Materials typically flow right-to-left, start on the left
	}

	// Find the leftmost position (materials flow right-to-left)
	float MinX = 0;
	float MinY = 0;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr)
		{
			MinX = FMath::Min(MinX, static_cast<float>(Expr->MaterialExpressionEditorX));
			MinY = FMath::Min(MinY, static_cast<float>(Expr->MaterialExpressionEditorY));
		}
	}

	// Try positions in a grid pattern, starting from left of existing expressions
	const float GridSpacing = NodeSize.X + DEFAULT_SPACING;
	const float RowHeight = NodeSize.Y + DEFAULT_SPACING;

	// Start searching from the left edge
	FVector2D StartPos(MinX - GridSpacing, MinY);

	// Try positions in columns (left) and rows (down)
	for (int32 Col = 0; Col < 10; ++Col)
	{
		for (int32 Row = 0; Row < 10; ++Row)
		{
			FVector2D TestPos = StartPos + FVector2D(-Col * GridSpacing, Row * RowHeight);

			// Check if this position overlaps with any existing expression
			// Uses same AABB center-based overlap check as Blueprint nodes
			bool bOverlaps = false;
			for (UMaterialExpression* Expr : Expressions)
			{
				if (Expr)
				{
					FVector2D ExprPos(Expr->MaterialExpressionEditorX, Expr->MaterialExpressionEditorY);
					FVector2D ExprSize(DEFAULT_EXPRESSION_WIDTH, DEFAULT_EXPRESSION_HEIGHT);

					// Calculate centers and half-sizes with padding
					float HalfWidth1 = (NodeSize.X + DEFAULT_PADDING) / 2;
					float HalfHeight1 = (NodeSize.Y + DEFAULT_PADDING) / 2;
					float HalfWidth2 = (ExprSize.X + DEFAULT_PADDING) / 2;
					float HalfHeight2 = (ExprSize.Y + DEFAULT_PADDING) / 2;

					FVector2D Center1 = TestPos + NodeSize / 2;
					FVector2D Center2 = ExprPos + ExprSize / 2;

					if (FMath::Abs(Center1.X - Center2.X) < (HalfWidth1 + HalfWidth2) &&
						FMath::Abs(Center1.Y - Center2.Y) < (HalfHeight1 + HalfHeight2))
					{
						bOverlaps = true;
						break;
					}
				}
			}

			if (!bOverlaps)
			{
				return TestPos;
			}
		}
	}

	// Fallback: place far to the left
	return FVector2D(MinX - GridSpacing * 2, MinY);
}

bool FBridgeGraphLayoutUtil::DoesPositionOverlap(
	UEdGraph* Graph,
	const FVector2D& Position,
	const FVector2D& NodeSize,
	float Padding)
{
	if (!Graph)
	{
		return false;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FVector2D NodePos(Node->NodePosX, Node->NodePosY);
		FVector2D ExistingNodeSize(DEFAULT_NODE_WIDTH, DEFAULT_NODE_HEIGHT);

		// Check for AABB overlap with padding
		float HalfWidth1 = (NodeSize.X + Padding) / 2;
		float HalfHeight1 = (NodeSize.Y + Padding) / 2;
		float HalfWidth2 = (ExistingNodeSize.X + Padding) / 2;
		float HalfHeight2 = (ExistingNodeSize.Y + Padding) / 2;

		FVector2D Center1 = Position + NodeSize / 2;
		FVector2D Center2 = NodePos + ExistingNodeSize / 2;

		if (FMath::Abs(Center1.X - Center2.X) < (HalfWidth1 + HalfWidth2) &&
			FMath::Abs(Center1.Y - Center2.Y) < (HalfHeight1 + HalfHeight2))
		{
			return true; // Overlap detected
		}
	}

	return false; // No overlap
}

bool FBridgeGraphLayoutUtil::GetGraphBounds(
	UEdGraph* Graph,
	FVector2D& OutMin,
	FVector2D& OutMax)
{
	if (!Graph || Graph->Nodes.Num() == 0)
	{
		return false;
	}

	OutMin = FVector2D(FLT_MAX, FLT_MAX);
	OutMax = FVector2D(-FLT_MAX, -FLT_MAX);

	bool bFoundValidNode = false;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FVector2D NodePos(Node->NodePosX, Node->NodePosY);
		FVector2D NodeEnd = NodePos + FVector2D(DEFAULT_NODE_WIDTH, DEFAULT_NODE_HEIGHT);

		OutMin.X = FMath::Min(OutMin.X, NodePos.X);
		OutMin.Y = FMath::Min(OutMin.Y, NodePos.Y);
		OutMax.X = FMath::Max(OutMax.X, NodeEnd.X);
		OutMax.Y = FMath::Max(OutMax.Y, NodeEnd.Y);

		bFoundValidNode = true;
	}

	return bFoundValidNode;
}
