// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/SetNodePropertyTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeJsonObjectUtils.h"
#include "Utils/BridgePropertySerializer.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	static bool IsAnimGraphCacheNameInput(const FString& PropertyName)
	{
		return PropertyName.Equals(TEXT("CachePoseName"), ESearchCase::IgnoreCase) ||
			PropertyName.Equals(TEXT("CacheName"), ESearchCase::IgnoreCase) ||
			PropertyName.Equals(TEXT("cache_name"), ESearchCase::IgnoreCase) ||
			PropertyName.Equals(TEXT("NameOfCache"), ESearchCase::IgnoreCase);
	}

	static bool JsonValueToString(const TSharedPtr<FJsonValue>& Value, FString& OutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->TryGetString(OutValue))
		{
			return true;
		}
		if (Value->Type == EJson::Number)
		{
			OutValue = FString::Printf(TEXT("%g"), Value->AsNumber());
			return true;
		}
		if (Value->Type == EJson::Boolean)
		{
			OutValue = Value->AsBool() ? TEXT("true") : TEXT("false");
			return true;
		}
		return false;
	}

	static bool ParsePathSegment(const FString& Segment, FString& OutName, int32& OutIndex)
	{
		OutIndex = INDEX_NONE;

		int32 BracketStart = INDEX_NONE;
		if (!Segment.FindChar(TEXT('['), BracketStart))
		{
			OutName = Segment;
			return !OutName.IsEmpty();
		}

		int32 BracketEnd = INDEX_NONE;
		if (!Segment.FindChar(TEXT(']'), BracketEnd) || BracketEnd <= BracketStart + 1)
		{
			return false;
		}

		OutName = Segment.Left(BracketStart);
		const FString IndexString = Segment.Mid(BracketStart + 1, BracketEnd - BracketStart - 1);
		if (OutName.IsEmpty() || !FCString::IsNumeric(*IndexString))
		{
			return false;
		}

		OutIndex = FCString::Atoi(*IndexString);
		return OutIndex >= 0;
	}

	static bool ResolvePropertyPathAgainstStruct(
		UStruct* RootStruct,
		void* RootContainer,
		const FString& PropertyPath,
		FProperty*& OutProperty,
		void*& OutContainer,
		FString& OutError)
	{
		if (!RootStruct || !RootContainer)
		{
			OutError = TEXT("Struct root is null");
			return false;
		}
		if (PropertyPath.IsEmpty())
		{
			OutError = TEXT("Property path is empty");
			return false;
		}

		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			OutError = TEXT("Invalid property path");
			return false;
		}

		UStruct* CurrentStruct = RootStruct;
		void* CurrentContainer = RootContainer;
		FProperty* CurrentProperty = nullptr;

		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const FString& Segment = Segments[Index];
			FString PropertyName;
			int32 ArrayIndex = INDEX_NONE;
			if (!ParsePathSegment(Segment, PropertyName, ArrayIndex))
			{
				OutError = FString::Printf(TEXT("Invalid array index in segment: %s"), *Segment);
				return false;
			}

			CurrentProperty = CurrentStruct ? CurrentStruct->FindPropertyByName(*PropertyName) : nullptr;
			if (!CurrentProperty)
			{
				OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
				return false;
			}

			if (ArrayIndex >= 0)
			{
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(CurrentProperty);
				if (!ArrayProp)
				{
					OutError = FString::Printf(TEXT("Property '%s' is not an array"), *PropertyName);
					return false;
				}

				FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentContainer));
				if (ArrayIndex >= ArrayHelper.Num())
				{
					OutError = FString::Printf(TEXT("Array index %d out of bounds (size: %d)"), ArrayIndex, ArrayHelper.Num());
					return false;
				}

				CurrentProperty = ArrayProp->Inner;
				CurrentContainer = ArrayHelper.GetRawPtr(ArrayIndex);
				if (Index == Segments.Num() - 1)
				{
					break;
				}

				FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
				if (!InnerStructProp)
				{
					OutError = FString::Printf(TEXT("Cannot traverse into non-struct array element at: %s"), *Segment);
					return false;
				}

				CurrentStruct = InnerStructProp->Struct;
				if (CurrentStruct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
					if (!InstancedStruct || !InstancedStruct->IsValid())
					{
						OutError = FString::Printf(TEXT("InstancedStruct array element '%s' is empty"), *Segment);
						return false;
					}

					CurrentContainer = InstancedStruct->GetMutableMemory();
					CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
				}
				continue;
			}

			if (Index == Segments.Num() - 1)
			{
				break;
			}

			if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProperty))
			{
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
				if (CurrentStruct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
					if (!InstancedStruct || !InstancedStruct->IsValid())
					{
						OutError = FString::Printf(TEXT("InstancedStruct property '%s' is empty"), *PropertyName);
						return false;
					}

					CurrentContainer = InstancedStruct->GetMutableMemory();
					CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
				}
			}
			else if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(CurrentProperty))
			{
				UObject* ObjectValue = ObjectProp->GetObjectPropertyValue_InContainer(CurrentContainer);
				if (!ObjectValue)
				{
					OutError = FString::Printf(TEXT("Object property '%s' is null"), *PropertyName);
					return false;
				}
				CurrentContainer = ObjectValue;
				CurrentStruct = ObjectValue->GetClass();
			}
			else
			{
				OutError = FString::Printf(TEXT("Cannot traverse property '%s' - not a struct or object"), *PropertyName);
				return false;
			}
		}

		OutProperty = CurrentProperty;
		OutContainer = CurrentContainer;
		return OutProperty != nullptr;
	}

	static bool TryResolveInnerAnimNodePropertyPath(
		UObject* Object,
		const FString& PropertyName,
		FProperty*& OutProperty,
		void*& OutContainer,
		FString& OutError)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* InnerNodeProp = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(TEXT("Node")));
		void* InnerNodeContainer = InnerNodeProp ? InnerNodeProp->ContainerPtrToValuePtr<void>(Object) : nullptr;
		UScriptStruct* InnerNodeStruct = InnerNodeProp ? InnerNodeProp->Struct : nullptr;
		if (!InnerNodeStruct || !InnerNodeContainer)
		{
			return false;
		}

		FString InnerPath = PropertyName;
		if (InnerPath.StartsWith(TEXT("Node."), ESearchCase::IgnoreCase))
		{
			InnerPath = InnerPath.RightChop(5);
		}

		return ResolvePropertyPathAgainstStruct(InnerNodeStruct, InnerNodeContainer, InnerPath, OutProperty, OutContainer, OutError);
	}

	static bool SetNamedPropertyValue(UObject* Object, const TCHAR* PropertyName, const TSharedPtr<FJsonValue>& Value, TArray<FString>& Errors)
	{
		if (!Object)
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		Object->PreEditChange(Property);
		FString SetError;
		if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Object, Value, SetError))
		{
			Object->PostEditChange();
			Errors.Add(FString::Printf(TEXT("Failed to sync property %s: %s"), PropertyName, *SetError));
			return false;
		}

		FPropertyChangedEvent ChangeEvent(Property);
		Object->PostEditChangeProperty(ChangeEvent);
		return true;
	}

	static bool SetInnerAnimNodePropertyValue(UObject* Object, const TCHAR* PropertyName, const TSharedPtr<FJsonValue>& Value, TArray<FString>& Errors)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* InnerNodeProp = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(TEXT("Node")));
		void* InnerNodeContainer = InnerNodeProp ? InnerNodeProp->ContainerPtrToValuePtr<void>(Object) : nullptr;
		UScriptStruct* InnerNodeStruct = InnerNodeProp ? InnerNodeProp->Struct : nullptr;
		if (!InnerNodeStruct || !InnerNodeContainer)
		{
			return false;
		}

		FProperty* Property = InnerNodeStruct->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		FString SetError;
		if (!FBridgePropertySerializer::DeserializePropertyValue(Property, InnerNodeContainer, Value, SetError))
		{
			Errors.Add(FString::Printf(TEXT("Failed to sync inner Node.%s: %s"), PropertyName, *SetError));
			return false;
		}
		return true;
	}

	static bool SyncAnimGraphCacheName(UObject* Node, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, TArray<FString>& Errors)
	{
		if (!Node || !IsAnimGraphCacheNameInput(PropertyName) ||
			!Node->GetClass()->GetName().Contains(TEXT("AnimGraphNode_SaveCachedPose"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		FString CacheName;
		if (!JsonValueToString(Value, CacheName) || CacheName.IsEmpty())
		{
			return false;
		}

		TSharedPtr<FJsonValue> NameValue = MakeShared<FJsonValueString>(CacheName);
		bool bSyncedAny = false;
		bSyncedAny |= SetNamedPropertyValue(Node, TEXT("CacheName"), NameValue, Errors);
		bSyncedAny |= SetNamedPropertyValue(Node, TEXT("CachePoseName"), NameValue, Errors);
		bSyncedAny |= SetNamedPropertyValue(Node, TEXT("cache_name"), NameValue, Errors);
		bSyncedAny |= SetInnerAnimNodePropertyValue(Node, TEXT("CachePoseName"), NameValue, Errors);
		bSyncedAny |= SetInnerAnimNodePropertyValue(Node, TEXT("NameOfCache"), NameValue, Errors);
		return bSyncedAny;
	}

	static bool ApplyCallFunctionReferenceStringShortcut(
		UEdGraphNode* GraphNode,
		UBlueprint* Blueprint,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value,
		TArray<FString>& Errors)
	{
		if (!PropertyName.Equals(TEXT("FunctionReference"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(GraphNode);
		if (!CallNode)
		{
			return false;
		}

		FString MemberName;
		if (!Value.IsValid() || !Value->TryGetString(MemberName) || MemberName.IsEmpty())
		{
			return false;
		}

		TArray<UClass*> SearchClasses;
		auto AddSearchClass = [&SearchClasses](UClass* Candidate)
		{
			if (Candidate && !SearchClasses.Contains(Candidate))
			{
				SearchClasses.Add(Candidate);
			}
		};

		if (UFunction* CurrentFunction = CallNode->GetTargetFunction())
		{
			AddSearchClass(Cast<UClass>(CurrentFunction->GetOuter()));
		}
		AddSearchClass(Blueprint ? Blueprint->GeneratedClass : nullptr);
		AddSearchClass(Blueprint ? Blueprint->SkeletonGeneratedClass : nullptr);
		AddSearchClass(Blueprint ? Blueprint->ParentClass : nullptr);

		for (UClass* CandidateClass : SearchClasses)
		{
			if (UFunction* Function = CandidateClass->FindFunctionByName(FName(*MemberName)))
			{
				CallNode->SetFromFunction(Function);
				CallNode->ReconstructNode();
				return true;
			}
		}

		Errors.Add(FString::Printf(
			TEXT("FunctionReference string shorthand could not resolve MemberName '%s'; use object form {\"FunctionReference\":{\"MemberName\":\"%s\"}} if the function lives on a different target class."),
			*MemberName,
			*MemberName));
		return true;
	}
}

FString USetNodePropertyTool::GetToolDescription() const
{
	return TEXT("Set properties on a graph node by GUID. Supports UPROPERTY members, "
		"inner anim node struct properties, nested struct paths, struct-array element paths "
		"(e.g. SpringBone.BoneName, Node.Input.Bones[0].BoneName), and pin defaults (e.g. Alpha). "
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
	Properties.Description = TEXT("Properties to set as JSON object (e.g. {\"SpringStiffness\": 450, \"Alpha\": 0.08, \"Node.Input.Bones[0].BoneName\": \"pelvis\"})");
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
	TArray<FString> Errors = ApplyProperties(Blueprint, Node, Properties);

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

TArray<FString> USetNodePropertyTool::ApplyProperties(UBlueprint* Blueprint, UObject* Node, const TSharedPtr<FJsonObject>& Properties)
{
	TArray<FString> Errors;
	if (!Node || !Properties.IsValid())
	{
		return Errors;
	}

	for (const auto& Pair : Properties->Values)
	{
		const FString PropertyName = SoftUE::JsonObjectUtils::KeyToString(Pair.Key);
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		if (ApplyCallFunctionReferenceStringShortcut(Cast<UEdGraphNode>(Node), Blueprint, PropertyName, Value, Errors))
		{
			continue;
		}

		FProperty* Property = Node->GetClass()->FindPropertyByName(*PropertyName);
		void* Container = Node;

		if (!Property)
		{
			FString FindError;
			if (!FBridgeAssetModifier::FindPropertyByPath(Node, PropertyName, Property, Container, FindError) &&
				!TryResolveInnerAnimNodePropertyPath(Node, PropertyName, Property, Container, FindError))
			{
				if (SyncAnimGraphCacheName(Node, PropertyName, Value, Errors))
				{
					continue;
				}

				FString Msg = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
				UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("%s"), *Msg);
				Errors.Add(Msg);
				continue;
			}
		}

		FString SetError;
		if (!FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Value, SetError))
		{
			FString Msg = FString::Printf(TEXT("Failed to set property %s: %s"), *PropertyName, *SetError);
			UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("%s"), *Msg);
			Errors.Add(Msg);
		}
		else
		{
			SyncAnimGraphCacheName(Node, PropertyName, Value, Errors);
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
					const TSharedPtr<FJsonValue> ValuePtr = SoftUE::JsonObjectUtils::FindField(Properties, PropName);
					if (ValuePtr.IsValid())
					{
						FString StringValue;
						if (ValuePtr->Type == EJson::Number)
						{
							StringValue = FString::Printf(TEXT("%g"), ValuePtr->AsNumber());
						}
						else if (ValuePtr->Type == EJson::Boolean)
						{
							StringValue = ValuePtr->AsBool() ? TEXT("true") : TEXT("false");
						}
						else
						{
							StringValue = ValuePtr->AsString();
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
