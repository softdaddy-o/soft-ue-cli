// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/AddGraphNodeTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"
#include "Utils/BridgeGraphLayoutUtil.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString UAddGraphNodeTool::GetToolDescription() const
{
	return TEXT("Add a node to a Blueprint or Material graph by class name with intelligent auto-positioning. "
		"For Materials: use expression class names like 'MaterialExpressionAdd', 'MaterialExpressionSceneTexture', 'MaterialExpressionCollectionParameter'. "
		"For Blueprints: use node class names like 'K2Node_CallFunction', 'K2Node_VariableGet', 'K2Node_Event'. "
		"Auto-positioning (enabled by default) places nodes intelligently to avoid overlaps. "
		"Use 'connect_to_node' and 'connect_to_pin' for connection-based layout (execution flows right, data flows left/down). "
		"Properties can be set via the 'properties' parameter.");
}

TMap<FString, FBridgeSchemaProperty> UAddGraphNodeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint or Material");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty NodeClass;
	NodeClass.Type = TEXT("string");
	NodeClass.Description = TEXT("Node class name. For Materials: 'MaterialExpressionAdd', 'MaterialExpressionSceneTexture', etc. "
		"For Blueprints: 'K2Node_CallFunction', 'K2Node_VariableGet', etc.");
	NodeClass.bRequired = true;
	Schema.Add(TEXT("node_class"), NodeClass);

	FBridgeSchemaProperty GraphName;
	GraphName.Type = TEXT("string");
	GraphName.Description = TEXT("Graph name (for Blueprints). Default is 'EventGraph'");
	GraphName.bRequired = false;
	Schema.Add(TEXT("graph_name"), GraphName);

	FBridgeSchemaProperty Position;
	Position.Type = TEXT("array");
	Position.ItemsType = TEXT("number");
	Position.Description = TEXT("Node position as [x, y]. If not specified and auto_position is true, position will be calculated automatically.");
	Position.bRequired = false;
	Schema.Add(TEXT("position"), Position);

	FBridgeSchemaProperty AutoPosition;
	AutoPosition.Type = TEXT("boolean");
	AutoPosition.Description = TEXT("Enable automatic positioning. If true and position is not specified, the node will be positioned intelligently based on graph context. Default: true");
	AutoPosition.bRequired = false;
	Schema.Add(TEXT("auto_position"), AutoPosition);

	FBridgeSchemaProperty ConnectToNode;
	ConnectToNode.Type = TEXT("string");
	ConnectToNode.Description = TEXT("GUID of node to position relative to (for connection-based layout). Only used if auto_position is true.");
	ConnectToNode.bRequired = false;
	Schema.Add(TEXT("connect_to_node"), ConnectToNode);

	FBridgeSchemaProperty ConnectToPin;
	ConnectToPin.Type = TEXT("string");
	ConnectToPin.Description = TEXT("Name of pin on connect_to_node to position relative to. Only used if auto_position is true and connect_to_node is specified.");
	ConnectToPin.bRequired = false;
	Schema.Add(TEXT("connect_to_pin"), ConnectToPin);

	FBridgeSchemaProperty Properties;
	Properties.Type = TEXT("object");
	Properties.Description = TEXT("Properties to set on the node after creation (e.g., {'ParameterName': 'MyParam', 'DefaultValue': 1.0})");
	Properties.bRequired = false;
	Schema.Add(TEXT("properties"), Properties);

	return Schema;
}

TArray<FString> UAddGraphNodeTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("node_class") };
}

FBridgeToolResult UAddGraphNodeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeClass = GetStringArgOrDefault(Arguments, TEXT("node_class"));
	FString GraphName = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT("EventGraph"));
	FString ConnectToNodeGuid = GetStringArgOrDefault(Arguments, TEXT("connect_to_node"));
	FString ConnectToPinName = GetStringArgOrDefault(Arguments, TEXT("connect_to_pin"));

	// Get auto_position flag (default: true)
	bool bAutoPosition = true;
	Arguments->TryGetBoolField(TEXT("auto_position"), bAutoPosition);

	// Get position (optional)
	FVector2D Position(0, 0);
	bool bHasExplicitPosition = false;
	const TArray<TSharedPtr<FJsonValue>>* PositionArray;
	if (Arguments->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
	{
		Position.X = (*PositionArray)[0]->AsNumber();
		Position.Y = (*PositionArray)[1]->AsNumber();
		bHasExplicitPosition = true;
		bAutoPosition = false; // Explicit position overrides auto-positioning
	}

	// Get properties object
	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr;
	if (Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr))
	{
		Properties = *PropertiesPtr;
	}

	if (AssetPath.IsEmpty() || NodeClass.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and node_class are required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-graph-node: %s, class=%s"), *AssetPath, *NodeClass);

	// Load the asset
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddNode", "Add {0} node to {1}"),
			FText::FromString(NodeClass),
			FText::FromString(AssetPath)));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("node_class"), NodeClass);

	// Handle Material
	if (UMaterial* Material = Cast<UMaterial>(Object))
	{
		FString Error;
		UMaterialExpression* Expression = CreateMaterialExpression(Material, NodeClass, Position, bAutoPosition, Properties, Error);

		if (!Expression)
		{
			return FBridgeToolResult::Error(Error);
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("expression_name"), Expression->GetName());
		Result->SetStringField(TEXT("expression_class"), Expression->GetClass()->GetName());
		Result->SetBoolField(TEXT("needs_save"), true);

		// Include calculated position in response
		TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
		PositionJson->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
		PositionJson->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
		Result->SetObjectField(TEXT("position"), PositionJson);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-graph-node: Added material expression %s (%s) at (%d, %d)"),
			*Expression->GetName(), *Expression->GetClass()->GetName(),
			Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);

		return FBridgeToolResult::Json(Result);
	}

	// Handle Blueprint
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		// Find the target graph
		UEdGraph* TargetGraph = FBridgeAssetModifier::FindGraphByName(Blueprint, GraphName);
		if (!TargetGraph)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		}

		FString Error;
		UEdGraphNode* NewNode = CreateBlueprintNode(Blueprint, TargetGraph, NodeClass, Position, bAutoPosition, ConnectToNodeGuid, ConnectToPinName, Properties, Error);

		if (!NewNode)
		{
			return FBridgeToolResult::Error(Error);
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Result->SetStringField(TEXT("node_class"), NewNode->GetClass()->GetName());
		Result->SetStringField(TEXT("graph"), GraphName);
		Result->SetBoolField(TEXT("needs_compile"), true);
		Result->SetBoolField(TEXT("needs_save"), true);

		// Include calculated position in response
		TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
		PositionJson->SetNumberField(TEXT("x"), NewNode->NodePosX);
		PositionJson->SetNumberField(TEXT("y"), NewNode->NodePosY);
		Result->SetObjectField(TEXT("position"), PositionJson);

		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("add-graph-node: Added Blueprint node %s (%s) at (%d, %d)"),
			*NewNode->NodeGuid.ToString(), *NewNode->GetClass()->GetName(),
			NewNode->NodePosX, NewNode->NodePosY);

		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(TEXT("Asset must be a Blueprint or Material"));
}

UMaterialExpression* UAddGraphNodeTool::CreateMaterialExpression(
	UMaterial* Material,
	const FString& NodeClassName,
	const FVector2D& Position,
	bool bAutoPosition,
	const TSharedPtr<FJsonObject>& Properties,
	FString& OutError)
{
	// Resolve the expression class
	FString ClassError;
	UClass* ExpressionClass = nullptr;

	// Try to find the class directly
	ExpressionClass = FBridgePropertySerializer::ResolveClass(NodeClassName, ClassError);

	// If not found, try with UMaterialExpression prefix
	if (!ExpressionClass && !NodeClassName.StartsWith(TEXT("MaterialExpression")))
	{
		FString PrefixedName = TEXT("MaterialExpression") + NodeClassName;
		ExpressionClass = FBridgePropertySerializer::ResolveClass(PrefixedName, ClassError);
	}

	// Also try with U prefix
	if (!ExpressionClass && !NodeClassName.StartsWith(TEXT("U")))
	{
		FString UPrefixedName = TEXT("U") + NodeClassName;
		ExpressionClass = FBridgePropertySerializer::ResolveClass(UPrefixedName, ClassError);
	}

	if (!ExpressionClass)
	{
		OutError = FString::Printf(TEXT("Material expression class not found: %s. Try 'MaterialExpressionAdd', 'MaterialExpressionSceneTexture', etc."), *NodeClassName);
		return nullptr;
	}

	// Validate it's a material expression
	if (!ExpressionClass->IsChildOf<UMaterialExpression>())
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UMaterialExpression subclass"), *NodeClassName);
		return nullptr;
	}

	FBridgeAssetModifier::MarkModified(Material);

	// Create the expression
	UMaterialExpression* Expression = NewObject<UMaterialExpression>(Material, ExpressionClass, NAME_None, RF_Transactional);
	if (!Expression)
	{
		OutError = FString::Printf(TEXT("Failed to create expression of class %s"), *ExpressionClass->GetName());
		return nullptr;
	}

	// Calculate position
	FVector2D FinalPosition = Position;
	if (bAutoPosition)
	{
		FinalPosition = FBridgeGraphLayoutUtil::CalculateMaterialExpressionPosition(Material);
	}

	// Set position (use RoundToInt to avoid truncation issues)
	Expression->MaterialExpressionEditorX = FMath::RoundToInt(FinalPosition.X);
	Expression->MaterialExpressionEditorY = FMath::RoundToInt(FinalPosition.Y);

	// Special handling: resolve NamedRerouteUsage Declaration pointer
	if (Properties.IsValid())
	{
		if (auto* UsageNode = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
		{
			FString DeclName;
			if (Properties->TryGetStringField(TEXT("Declaration"), DeclName))
			{
				UMaterialExpressionNamedRerouteDeclaration* FoundDecl = nullptr;
				for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
				{
					if (auto* Decl = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expr))
					{
						if (Decl->GetName() == DeclName)
						{
							FoundDecl = Decl;
							break;
						}
					}
				}

				if (FoundDecl)
				{
					UsageNode->Declaration = FoundDecl;
					// Remove from properties so generic path doesn't overwrite with string
					Properties->RemoveField(TEXT("Declaration"));
				}
				else
				{
					UE_LOG(LogSoftUEBridgeEditor, Warning,
						TEXT("NamedRerouteDeclaration '%s' not found in material expressions"), *DeclName);
				}
			}
		}
	}

	// Apply properties if provided
	if (Properties.IsValid())
	{
		ApplyNodeProperties(Expression, Properties);
	}

	// Add to material
	Material->GetExpressionCollection().AddExpression(Expression);

	// Refresh the material to notify editors and trigger recompilation
	FBridgeAssetModifier::RefreshMaterial(Material);
	FBridgeAssetModifier::MarkPackageDirty(Material);

	return Expression;
}

UEdGraphNode* UAddGraphNodeTool::CreateBlueprintNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& NodeClassName,
	const FVector2D& Position,
	bool bAutoPosition,
	const FString& ConnectToNodeGuid,
	const FString& ConnectToPinName,
	const TSharedPtr<FJsonObject>& Properties,
	FString& OutError)
{
	// Resolve the node class
	FString ClassError;
	UClass* NodeClass = nullptr;

	// Try to find the class directly
	NodeClass = FBridgePropertySerializer::ResolveClass(NodeClassName, ClassError);

	// If not found, try with UK2Node_ prefix
	if (!NodeClass && !NodeClassName.StartsWith(TEXT("K2Node")))
	{
		FString PrefixedName = TEXT("K2Node_") + NodeClassName;
		NodeClass = FBridgePropertySerializer::ResolveClass(PrefixedName, ClassError);

		// Also try UK2Node_ prefix
		if (!NodeClass)
		{
			PrefixedName = TEXT("UK2Node_") + NodeClassName;
			NodeClass = FBridgePropertySerializer::ResolveClass(PrefixedName, ClassError);
		}
	}

	if (!NodeClass)
	{
		OutError = FString::Printf(TEXT("Blueprint node class not found: %s. Try 'K2Node_CallFunction', 'K2Node_VariableGet', etc."), *NodeClassName);
		return nullptr;
	}

	// Validate it's a graph node
	if (!NodeClass->IsChildOf<UEdGraphNode>())
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UEdGraphNode subclass"), *NodeClassName);
		return nullptr;
	}

	FBridgeAssetModifier::MarkModified(Blueprint);

	// Create the node
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("Failed to create node of class %s"), *NodeClass->GetName());
		return nullptr;
	}

	// Initialize node
	NewNode->CreateNewGuid();

	// Calculate position
	FVector2D FinalPosition = Position;
	if (bAutoPosition)
	{
		// Try to find target node and pin for connection-based positioning
		UEdGraphNode* TargetNode = nullptr;
		UEdGraphPin* TargetPin = nullptr;

		if (!ConnectToNodeGuid.IsEmpty())
		{
			FGuid NodeGuid;
			if (FGuid::Parse(ConnectToNodeGuid, NodeGuid))
			{
				FString GraphName, GraphType;
				TargetNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, NodeGuid);

				if (TargetNode && !ConnectToPinName.IsEmpty())
				{
					// Find the specified pin
					for (UEdGraphPin* Pin : TargetNode->Pins)
					{
						if (Pin && Pin->PinName.ToString() == ConnectToPinName)
						{
							TargetPin = Pin;
							break;
						}
					}
				}
			}
		}

		FinalPosition = FBridgeGraphLayoutUtil::CalculateBlueprintNodePosition(Graph, TargetNode, TargetPin);
	}

	// Use RoundToInt to avoid truncation issues with float-to-int conversion
	NewNode->NodePosX = FMath::RoundToInt(FinalPosition.X);
	NewNode->NodePosY = FMath::RoundToInt(FinalPosition.Y);

	// Apply properties if provided
	if (Properties.IsValid())
	{
		ApplyNodeProperties(NewNode, Properties);
	}

	// Special handling for certain node types that need extra setup
	if (UK2Node* K2Node = Cast<UK2Node>(NewNode))
	{
		// For CallFunction nodes, try to set up the function reference from properties
		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
		{
			// Function setup is handled via properties (FunctionReference.MemberName)
		}
		else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(NewNode))
		{
			// Variable reference setup is handled via properties
		}
		else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NewNode))
		{
			// Variable reference setup is handled via properties
		}

		// Allocate default pins
		K2Node->AllocateDefaultPins();
	}

	// Add to graph
	Graph->AddNode(NewNode, true, false);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBridgeAssetModifier::MarkPackageDirty(Blueprint);

	return NewNode;
}

void UAddGraphNodeTool::ApplyNodeProperties(UObject* Node, const TSharedPtr<FJsonObject>& Properties)
{
	if (!Node || !Properties.IsValid())
	{
		return;
	}

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Find the property - first try direct lookup
		FProperty* Property = Node->GetClass()->FindPropertyByName(*PropertyName);
		void* Container = Node;

		if (!Property)
		{
			// Try nested property path
			FString FindError;
			if (!FBridgeAssetModifier::FindPropertyByPath(Node, PropertyName, Property, Container, FindError))
			{
				UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("Property not found: %s - %s"), *PropertyName, *FindError);
				continue;
			}
		}

		// Set the property value
		FString SetError;
		if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Value, SetError))
		{
			UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("Failed to set property %s: %s"), *PropertyName, *SetError);
		}
	}
}
