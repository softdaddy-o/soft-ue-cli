// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/InsertGraphNodeTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

FString UInsertGraphNodeTool::GetToolDescription() const
{
	return TEXT("Atomically insert a new node between two connected nodes in a Blueprint graph. "
		"Disconnects the source→target wire, creates the new node, and connects source→new input and new output→target. "
		"All operations happen in a single undo transaction.");
}

TMap<FString, FBridgeSchemaProperty> UInsertGraphNodeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Blueprint or AnimBlueprint");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty NodeClass;
	NodeClass.Type = TEXT("string");
	NodeClass.Description = TEXT("Node class name to insert (e.g. AnimGraphNode_LinkedAnimLayer)");
	NodeClass.bRequired = true;
	Schema.Add(TEXT("node_class"), NodeClass);

	FBridgeSchemaProperty SourceNode;
	SourceNode.Type = TEXT("string");
	SourceNode.Description = TEXT("GUID of the source (upstream) node");
	SourceNode.bRequired = true;
	Schema.Add(TEXT("source_node"), SourceNode);

	FBridgeSchemaProperty SourcePin;
	SourcePin.Type = TEXT("string");
	SourcePin.Description = TEXT("Pin name on the source node (output pin)");
	SourcePin.bRequired = true;
	Schema.Add(TEXT("source_pin"), SourcePin);

	FBridgeSchemaProperty TargetNode;
	TargetNode.Type = TEXT("string");
	TargetNode.Description = TEXT("GUID of the target (downstream) node");
	TargetNode.bRequired = true;
	Schema.Add(TEXT("target_node"), TargetNode);

	FBridgeSchemaProperty TargetPin;
	TargetPin.Type = TEXT("string");
	TargetPin.Description = TEXT("Pin name on the target node (input pin)");
	TargetPin.bRequired = true;
	Schema.Add(TEXT("target_pin"), TargetPin);

	FBridgeSchemaProperty GraphName;
	GraphName.Type = TEXT("string");
	GraphName.Description = TEXT("Graph name (default: EventGraph)");
	GraphName.bRequired = false;
	Schema.Add(TEXT("graph_name"), GraphName);

	FBridgeSchemaProperty NewInputPin;
	NewInputPin.Type = TEXT("string");
	NewInputPin.Description = TEXT("Input pin name on the new node to connect source→new. If not specified, auto-detects the first compatible input pin.");
	NewInputPin.bRequired = false;
	Schema.Add(TEXT("new_input_pin"), NewInputPin);

	FBridgeSchemaProperty NewOutputPin;
	NewOutputPin.Type = TEXT("string");
	NewOutputPin.Description = TEXT("Output pin name on the new node to connect new→target. If not specified, auto-detects the first compatible output pin.");
	NewOutputPin.bRequired = false;
	Schema.Add(TEXT("new_output_pin"), NewOutputPin);

	FBridgeSchemaProperty Properties;
	Properties.Type = TEXT("object");
	Properties.Description = TEXT("Properties to set on the new node after creation");
	Properties.bRequired = false;
	Schema.Add(TEXT("properties"), Properties);

	return Schema;
}

TArray<FString> UInsertGraphNodeTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("node_class"), TEXT("source_node"), TEXT("source_pin"), TEXT("target_node"), TEXT("target_pin") };
}

FBridgeToolResult UInsertGraphNodeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeClass = GetStringArgOrDefault(Arguments, TEXT("node_class"));
	FString SourceNodeStr = GetStringArgOrDefault(Arguments, TEXT("source_node"));
	FString SourcePinName = GetStringArgOrDefault(Arguments, TEXT("source_pin"));
	FString TargetNodeStr = GetStringArgOrDefault(Arguments, TEXT("target_node"));
	FString TargetPinName = GetStringArgOrDefault(Arguments, TEXT("target_pin"));
	FString GraphName = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT("EventGraph"));
	FString NewInputPinName = GetStringArgOrDefault(Arguments, TEXT("new_input_pin"));
	FString NewOutputPinName = GetStringArgOrDefault(Arguments, TEXT("new_output_pin"));

	if (AssetPath.IsEmpty() || NodeClass.IsEmpty() ||
		SourceNodeStr.IsEmpty() || SourcePinName.IsEmpty() ||
		TargetNodeStr.IsEmpty() || TargetPinName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, node_class, source_node, source_pin, target_node, and target_pin are all required"));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insert-graph-node: %s between %s.%s and %s.%s in %s"),
		*NodeClass, *SourceNodeStr, *SourcePinName, *TargetNodeStr, *TargetPinName, *AssetPath);

	// Load the asset as Blueprint
	FString LoadError;
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(TEXT("insert-graph-node only supports Blueprint assets"));
	}

	// Parse GUIDs
	FGuid SourceGuid, TargetGuid;
	if (!FGuid::Parse(SourceNodeStr, SourceGuid))
	{
		return FBridgeToolResult::Error(TEXT("Invalid source_node GUID format"));
	}
	if (!FGuid::Parse(TargetNodeStr, TargetGuid))
	{
		return FBridgeToolResult::Error(TEXT("Invalid target_node GUID format"));
	}

	// Find source and target nodes
	UEdGraphNode* SourceNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, SourceGuid);
	UEdGraphNode* TargetNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, TargetGuid);

	if (!SourceNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeStr));
	}
	if (!TargetNode)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeStr));
	}

	// Find the source output pin
	UEdGraphPin* SourceOutputPin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString().Equals(SourcePinName, ESearchCase::IgnoreCase))
		{
			SourceOutputPin = Pin;
			break;
		}
	}
	if (!SourceOutputPin)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Source pin not found: %s"), *SourcePinName));
	}

	// Find the target input pin
	UEdGraphPin* TargetInputPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString().Equals(TargetPinName, ESearchCase::IgnoreCase))
		{
			TargetInputPin = Pin;
			break;
		}
	}
	if (!TargetInputPin)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target pin not found: %s"), *TargetPinName));
	}

	// Verify the source→target connection actually exists
	if (!SourceOutputPin->LinkedTo.Contains(TargetInputPin))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("No connection exists between %s.%s and %s.%s"),
			*SourceNodeStr, *SourcePinName, *TargetNodeStr, *TargetPinName));
	}

	// Begin a single transaction for the entire operation
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "InsertNode", "Insert {0} between nodes"),
			FText::FromString(NodeClass)));

	FBridgeAssetModifier::MarkModified(Blueprint);

	// Step 1: Disconnect the specific source→target connection
	SourceOutputPin->BreakLinkTo(TargetInputPin);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insert-graph-node: Disconnected %s.%s → %s.%s"),
		*SourceNodeStr, *SourcePinName, *TargetNodeStr, *TargetPinName);

	// Step 2: Create the new node via the add-graph-node tool
	TSharedPtr<FJsonObject> AddNodeArgs = MakeShareable(new FJsonObject);
	AddNodeArgs->SetStringField(TEXT("asset_path"), AssetPath);
	AddNodeArgs->SetStringField(TEXT("node_class"), NodeClass);
	AddNodeArgs->SetStringField(TEXT("graph_name"), GraphName);
	// Position between source and target
	int32 MidX = (SourceNode->NodePosX + TargetNode->NodePosX) / 2;
	int32 MidY = (SourceNode->NodePosY + TargetNode->NodePosY) / 2;
	TArray<TSharedPtr<FJsonValue>> PosArray;
	PosArray.Add(MakeShareable(new FJsonValueNumber(MidX)));
	PosArray.Add(MakeShareable(new FJsonValueNumber(MidY)));
	AddNodeArgs->SetArrayField(TEXT("position"), PosArray);

	// Forward properties if provided
	const TSharedPtr<FJsonObject>* PropertiesPtr;
	if (Arguments->TryGetObjectField(TEXT("properties"), PropertiesPtr))
	{
		AddNodeArgs->SetObjectField(TEXT("properties"), *PropertiesPtr);
	}

	FBridgeToolResult AddResult = FBridgeToolRegistry::Get().ExecuteTool(TEXT("add-graph-node"), AddNodeArgs, Context);
	if (AddResult.bIsError)
	{
		// Reconnect source→target to restore state before returning error
		const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema();
		Schema->TryCreateConnection(SourceOutputPin, TargetInputPin);

		FString ErrorText;
		if (AddResult.Content.Num() > 0)
		{
			AddResult.Content[0]->TryGetStringField(TEXT("text"), ErrorText);
		}
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create node: %s"), *ErrorText));
	}

	// Extract the new node's GUID from the add-graph-node result
	// The result content is a JSON string serialized as text
	FString NewNodeGuidStr;
	{
		FString ResultText;
		if (AddResult.Content.Num() > 0)
		{
			AddResult.Content[0]->TryGetStringField(TEXT("text"), ResultText);
		}

		TSharedPtr<FJsonObject> AddResultJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultText);
		if (!FJsonSerializer::Deserialize(Reader, AddResultJson) || !AddResultJson.IsValid())
		{
			const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema();
			Schema->TryCreateConnection(SourceOutputPin, TargetInputPin);
			return FBridgeToolResult::Error(TEXT("Failed to parse add-graph-node result"));
		}

		if (!AddResultJson->TryGetStringField(TEXT("node_guid"), NewNodeGuidStr))
		{
			const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema();
			Schema->TryCreateConnection(SourceOutputPin, TargetInputPin);
			return FBridgeToolResult::Error(TEXT("add-graph-node did not return node_guid"));
		}
	}

	FGuid NewNodeGuid;
	if (!FGuid::Parse(NewNodeGuidStr, NewNodeGuid))
	{
		const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema();
		Schema->TryCreateConnection(SourceOutputPin, TargetInputPin);
		return FBridgeToolResult::Error(TEXT("add-graph-node returned invalid node_guid"));
	}

	UEdGraphNode* NewNode = FBridgeAssetModifier::FindNodeByGuid(Blueprint, NewNodeGuid);
	if (!NewNode)
	{
		const UEdGraphSchema* Schema = SourceNode->GetGraph()->GetSchema();
		Schema->TryCreateConnection(SourceOutputPin, TargetInputPin);
		return FBridgeToolResult::Error(TEXT("Could not find newly created node"));
	}

	// Step 3: Find the input and output pins on the new node
	UEdGraphPin* NewInputPin = nullptr;
	UEdGraphPin* NewOutputPin = nullptr;
	const UEdGraphSchema* Schema = NewNode->GetGraph()->GetSchema();

	if (!NewInputPinName.IsEmpty())
	{
		// User specified the pin name
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(NewInputPinName, ESearchCase::IgnoreCase))
			{
				NewInputPin = Pin;
				break;
			}
		}
		if (!NewInputPin)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("new_input_pin not found on new node: %s"), *NewInputPinName));
		}
	}
	else
	{
		// Auto-detect: find the first input pin compatible with source output
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				FPinConnectionResponse Response = Schema->CanCreateConnection(SourceOutputPin, Pin);
				if (Response.Response != CONNECT_RESPONSE_DISALLOW)
				{
					NewInputPin = Pin;
					break;
				}
			}
		}
		if (!NewInputPin)
		{
			return FBridgeToolResult::Error(TEXT("Could not find a compatible input pin on the new node. Specify new_input_pin explicitly."));
		}
	}

	if (!NewOutputPinName.IsEmpty())
	{
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(NewOutputPinName, ESearchCase::IgnoreCase))
			{
				NewOutputPin = Pin;
				break;
			}
		}
		if (!NewOutputPin)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("new_output_pin not found on new node: %s"), *NewOutputPinName));
		}
	}
	else
	{
		// Auto-detect: find the first output pin compatible with target input
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				FPinConnectionResponse Response = Schema->CanCreateConnection(Pin, TargetInputPin);
				if (Response.Response != CONNECT_RESPONSE_DISALLOW)
				{
					NewOutputPin = Pin;
					break;
				}
			}
		}
		if (!NewOutputPin)
		{
			return FBridgeToolResult::Error(TEXT("Could not find a compatible output pin on the new node. Specify new_output_pin explicitly."));
		}
	}

	// Step 4: Wire source→new and new→target
	bool bConnected1 = Schema->TryCreateConnection(SourceOutputPin, NewInputPin);
	bool bConnected2 = Schema->TryCreateConnection(NewOutputPin, TargetInputPin);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBridgeAssetModifier::MarkPackageDirty(Blueprint);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("node_class"), NodeClass);
	Result->SetStringField(TEXT("node_guid"), NewNodeGuidStr);
	Result->SetBoolField(TEXT("source_to_new_connected"), bConnected1);
	Result->SetBoolField(TEXT("new_to_target_connected"), bConnected2);
	Result->SetBoolField(TEXT("success"), bConnected1 && bConnected2);
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);

	if (!bConnected1)
	{
		Result->SetStringField(TEXT("warning"), TEXT("Failed to connect source→new node"));
	}
	if (!bConnected2)
	{
		FString Warning = Result->HasField(TEXT("warning"))
			? Result->GetStringField(TEXT("warning")) + TEXT("; Failed to connect new node→target")
			: TEXT("Failed to connect new node→target");
		Result->SetStringField(TEXT("warning"), Warning);
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insert-graph-node: Inserted %s (%s), source→new=%s, new→target=%s"),
		*NodeClass, *NewNodeGuidStr,
		bConnected1 ? TEXT("ok") : TEXT("FAILED"),
		bConnected2 ? TEXT("ok") : TEXT("FAILED"));

	return FBridgeToolResult::Json(Result);
}
