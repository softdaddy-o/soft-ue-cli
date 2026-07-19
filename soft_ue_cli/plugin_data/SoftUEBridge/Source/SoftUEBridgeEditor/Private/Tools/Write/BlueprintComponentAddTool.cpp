// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/BlueprintComponentAddTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
	FBridgeSchemaProperty BlueprintComponentSchemaProperty(
		const FString& Type,
		const FString& Description,
		bool bRequired = false)
	{
		FBridgeSchemaProperty Property;
		Property.Type = Type;
		Property.Description = Description;
		Property.bRequired = bRequired;
		return Property;
	}

	UClass* ResolveActorComponentClass(const FString& ComponentClass)
	{
		if (ComponentClass.IsEmpty())
		{
			return nullptr;
		}

		UClass* Class = nullptr;
		if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
		{
			Class = LoadObject<UClass>(nullptr, *ComponentClass);
		}
		if (!Class)
		{
			Class = FindFirstObject<UClass>(*ComponentClass, EFindFirstObjectOptions::ExactClass);
		}
		if (!Class && !ComponentClass.StartsWith(TEXT("U")))
		{
			Class = FindFirstObject<UClass>(*(TEXT("U") + ComponentClass), EFindFirstObjectOptions::ExactClass);
		}
		if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()))
		{
			return nullptr;
		}
		return Class;
	}

	USCS_Node* FindScsNodeByName(UBlueprint* Blueprint, const FString& NodeName)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript || NodeName.IsEmpty())
		{
			return nullptr;
		}

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			const FString VariableName = Node->GetVariableName().ToString();
			const FString TemplateName = Node->ComponentTemplate ? Node->ComponentTemplate->GetName() : TEXT("");
			if (VariableName.Equals(NodeName, ESearchCase::IgnoreCase)
				|| TemplateName.Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}
}

FString UBlueprintComponentAddTool::GetToolDescription() const
{
	return TEXT("Add a component template to a Blueprint Simple Construction Script, with optional parent component and parent socket.");
}

TMap<FString, FBridgeSchemaProperty> UBlueprintComponentAddTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), BlueprintComponentSchemaProperty(
		TEXT("string"),
		TEXT("Blueprint asset path, e.g. /Game/Blueprints/BP_Player"),
		true));
	Schema.Add(TEXT("component_class"), BlueprintComponentSchemaProperty(
		TEXT("string"),
		TEXT("Component class name or class path, e.g. StaticMeshComponent or /Script/Engine.SkeletalMeshComponent"),
		true));
	Schema.Add(TEXT("component_name"), BlueprintComponentSchemaProperty(
		TEXT("string"),
		TEXT("Optional Blueprint component variable name")));
	Schema.Add(TEXT("attach_to"), BlueprintComponentSchemaProperty(
		TEXT("string"),
		TEXT("Optional parent SCS component variable/template name")));
	Schema.Add(TEXT("attach_socket"), BlueprintComponentSchemaProperty(
		TEXT("string"),
		TEXT("Optional parent socket or bone name stored in USCS_Node.AttachToName")));
	return Schema;
}

TArray<FString> UBlueprintComponentAddTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("component_class") };
}

FBridgeToolResult UBlueprintComponentAddTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	const FString ComponentClassName = GetStringArgOrDefault(Arguments, TEXT("component_class"), TEXT(""));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"), TEXT(""));
	const FString AttachTo = GetStringArgOrDefault(Arguments, TEXT("attach_to"), TEXT(""));
	const FString AttachSocket = GetStringArgOrDefault(Arguments, TEXT("attach_socket"), TEXT(""));

	if (AssetPath.IsEmpty() || ComponentClassName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and component_class are required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FBridgeAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(LoadError);
	}
	if (!Blueprint->SimpleConstructionScript)
	{
		return FBridgeToolResult::Error(TEXT("Blueprint has no SimpleConstructionScript"));
	}

	UClass* ComponentClass = ResolveActorComponentClass(ComponentClassName);
	if (!ComponentClass)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Component class not found or is not an ActorComponent: %s"),
			*ComponentClassName));
	}

	if (!ComponentName.IsEmpty() && FindScsNodeByName(Blueprint, ComponentName))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Blueprint component already exists: %s"),
			*ComponentName));
	}

	USCS_Node* ParentNode = nullptr;
	if (!AttachTo.IsEmpty())
	{
		ParentNode = FindScsNodeByName(Blueprint, AttachTo);
		if (!ParentNode)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Parent Blueprint component not found: %s"),
				*AttachTo));
		}
	}

	const FName NewVariableName = ComponentName.IsEmpty()
		? NAME_None
		: FName(*ComponentName.TrimStartAndEnd());

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(
			NSLOCTEXT("MCP", "BlueprintComponentAdd", "Add component {0} to {1}"),
			FText::FromString(ComponentClassName),
			FText::FromString(AssetPath)));

	FBridgeAssetModifier::MarkModified(Blueprint);
	FBridgeAssetModifier::MarkModified(Blueprint->SimpleConstructionScript);

	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, NewVariableName);
	if (!NewNode || !NewNode->ComponentTemplate)
	{
		return FBridgeToolResult::Error(TEXT("Failed to create Blueprint component SCS node"));
	}

	NewNode->Modify();
	if (!AttachSocket.IsEmpty())
	{
		NewNode->AttachToName = FName(*AttachSocket.TrimStartAndEnd());
	}

	if (ParentNode)
	{
		ParentNode->Modify();
		ParentNode->AddChildNode(NewNode);
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBridgeAssetModifier::RefreshBlueprintNodes(Blueprint);
	FBridgeAssetModifier::MarkPackageDirty(Blueprint);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("component_class"), ComponentClass->GetName());
	Result->SetStringField(TEXT("component_name"), NewNode->GetVariableName().ToString());
	if (NewNode->ComponentTemplate)
	{
		Result->SetStringField(TEXT("component_template"), NewNode->ComponentTemplate->GetName());
	}
	if (ParentNode)
	{
		Result->SetStringField(TEXT("attach_to"), ParentNode->GetVariableName().ToString());
	}
	if (!AttachSocket.IsEmpty())
	{
		Result->SetStringField(TEXT("attach_socket"), NewNode->AttachToName.ToString());
	}
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("blueprint-component-add: Added %s to %s"),
		*Result->GetStringField(TEXT("component_name")),
		*AssetPath);

	return FBridgeToolResult::Json(Result);
}
