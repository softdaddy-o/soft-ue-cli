// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/SetNodePropertyTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgePropertySerializer.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString USetNodePropertyTool::GetToolDescription() const
{
	return TEXT("Set properties on a graph node by GUID. Supports UPROPERTY members, "
		"inner anim node struct properties (e.g. SpringBone.BoneName), and pin defaults (e.g. Alpha). "
		"Use query-blueprint-graph to find node GUIDs.");
}

TMap<FString, FBridgeSchemaProperty> USetNodePropertyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Blueprint or AnimBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty NodeGuid;
	NodeGuid.Type = TEXT("string");
	NodeGuid.Description = TEXT("GUID of the target node");
	NodeGuid.bRequired = true;
	Schema.Add(TEXT("node_guid"), NodeGuid);

	FBridgeSchemaProperty Properties;
	Properties.Type = TEXT("object");
	Properties.Description = TEXT("Properties to set as JSON object (e.g. {\"SpringStiffness\": 450, \"Alpha\": 0.08})");
	Properties.bRequired = true;
	Schema.Add(TEXT("properties"), Properties);

	return Schema;
}

TArray<FString> USetNodePropertyTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("node_guid"), TEXT("properties") };
}

FBridgeToolResult USetNodePropertyTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeGuidStr = GetStringArgOrDefault(Arguments, TEXT("node_guid"));

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr;
	if (Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr))
	{
		Properties = *PropertiesPtr;
	}

	if (AssetPath.IsEmpty() || NodeGuidStr.IsEmpty() || !Properties.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, node_guid, and properties are required"));
	}

	// Load Blueprint
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(TEXT("set-node-property only supports Blueprint assets"));
	}

	// Find node
	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return FBridgeToolResult::Error(TEXT("Invalid node_guid format"));
	}

	UEdGraphNode* Node = FBridgeAssetModifier::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeGuidStr));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("set-node-property: %s on node %s"), *AssetPath, *NodeGuidStr);

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "SetNodeProp", "Set properties on node {0}"),
			FText::FromString(NodeGuidStr)));

	FBridgeAssetModifier::MarkModified(Blueprint);

	// Apply properties
	TArray<FString> Errors = ApplyProperties(Node, Properties);

	// Reconstruct node to reflect property changes in pins
	Node->ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBridgeAssetModifier::MarkPackageDirty(Blueprint);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("node_guid"), NodeGuidStr);
	Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);

	if (Errors.Num() > 0)
	{
		Result->SetStringField(TEXT("property_warnings"), FString::Join(Errors, TEXT("; ")));
	}

	return FBridgeToolResult::Json(Result);
}

TArray<FString> USetNodePropertyTool::ApplyProperties(UObject* Node, const TSharedPtr<FJsonObject>& Properties)
{
	TArray<FString> Errors;
	if (!Node || !Properties.IsValid())
	{
		return Errors;
	}

	// Inner "Node" struct for anim graph nodes (FAnimNode_*)
	FStructProperty* InnerNodeProp = CastField<FStructProperty>(Node->GetClass()->FindPropertyByName(TEXT("Node")));
	void* InnerNodeContainer = InnerNodeProp ? InnerNodeProp->ContainerPtrToValuePtr<void>(Node) : nullptr;
	UScriptStruct* InnerNodeStruct = InnerNodeProp ? InnerNodeProp->Struct : nullptr;

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropertyName = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		FProperty* Property = Node->GetClass()->FindPropertyByName(*PropertyName);
		void* Container = Node;

		if (!Property)
		{
			FString FindError;
			if (!FBridgeAssetModifier::FindPropertyByPath(Node, PropertyName, Property, Container, FindError))
			{
				if (InnerNodeStruct && InnerNodeContainer)
				{
					Property = InnerNodeStruct->FindPropertyByName(*PropertyName);
					if (Property)
					{
						Container = InnerNodeContainer;
					}
					else
					{
						FString InnerPath = FString::Printf(TEXT("Node.%s"), *PropertyName);
						FBridgeAssetModifier::FindPropertyByPath(Node, InnerPath, Property, Container, FindError);
					}
				}

				if (!Property)
				{
					FString Msg = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
					UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("%s"), *Msg);
					Errors.Add(Msg);
					continue;
				}
			}
		}

		FString SetError;
		if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Value, SetError))
		{
			FString Msg = FString::Printf(TEXT("Failed to set property %s: %s"), *PropertyName, *SetError);
			UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("%s"), *Msg);
			Errors.Add(Msg);
		}
	}

	// Pin default fallback for unresolved properties.
	// Anim graph nodes expose some values (Alpha, BlendWeight, etc.) as pins
	// with DefaultValue strings, not as UPROPERTY members.
	UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Node);
	if (GraphNode)
	{
		TArray<FString> ResolvedByPin;
		for (const FString& ErrMsg : Errors)
		{
			if (!ErrMsg.StartsWith(TEXT("Property not found: ")))
			{
				continue;
			}
			FString PropName = ErrMsg.RightChop(20);
			if (PropName.IsEmpty()) continue;

			for (UEdGraphPin* Pin : GraphNode->Pins)
			{
				if (Pin && Pin->PinName.ToString() == PropName)
				{
					const TSharedPtr<FJsonValue>* ValuePtr = Properties->Values.Find(PropName);
					if (ValuePtr && ValuePtr->IsValid())
					{
						FString StringValue;
						if ((*ValuePtr)->Type == EJson::Number)
						{
							StringValue = FString::Printf(TEXT("%g"), (*ValuePtr)->AsNumber());
						}
						else if ((*ValuePtr)->Type == EJson::Boolean)
						{
							StringValue = (*ValuePtr)->AsBool() ? TEXT("true") : TEXT("false");
						}
						else
						{
							StringValue = (*ValuePtr)->AsString();
						}

						Pin->DefaultValue = StringValue;
						ResolvedByPin.Add(PropName);
						UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("Set pin default %s = %s"), *PropName, *StringValue);
					}
					break;
				}
			}
		}

		for (const FString& Resolved : ResolvedByPin)
		{
			FString ErrToRemove = FString::Printf(TEXT("Property not found: %s"), *Resolved);
			Errors.Remove(ErrToRemove);
		}
	}

	return Errors;
}
