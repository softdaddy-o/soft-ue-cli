// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimBlueprintRetargetTool.h"

#include "Tools/Animation/AnimBoneReferenceUtils.h"
#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty AnimRetargetSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}
}

FString UAnimBlueprintRetargetTool::GetToolDescription() const
{
	return TEXT("Duplicate an AnimBlueprint onto a target skeleton and remap authored FBoneReference bone names in AnimGraph nodes.");
}

TMap<FString, FBridgeSchemaProperty> UAnimBlueprintRetargetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("source_blueprint"), AnimRetargetSchemaProperty(TEXT("string"), TEXT("Source AnimBlueprint asset path"), true));
	Schema.Add(TEXT("target_blueprint"), AnimRetargetSchemaProperty(TEXT("string"), TEXT("New duplicated AnimBlueprint asset path"), true));
	Schema.Add(TEXT("target_skeleton"), AnimRetargetSchemaProperty(TEXT("string"), TEXT("Target skeleton asset path"), true));
	Schema.Add(TEXT("bone_map"), AnimRetargetSchemaProperty(TEXT("object"), TEXT("Map of old bone name to new bone name"), true));
	Schema.Add(TEXT("save"), AnimRetargetSchemaProperty(TEXT("boolean"), TEXT("Save the duplicated Blueprint after mutation")));
	Schema.Add(TEXT("checkout"), AnimRetargetSchemaProperty(TEXT("boolean"), TEXT("Checkout the duplicated asset before saving when source control is active")));
	return Schema;
}

TArray<FString> UAnimBlueprintRetargetTool::GetRequiredParams() const
{
	return { TEXT("source_blueprint"), TEXT("target_blueprint"), TEXT("target_skeleton"), TEXT("bone_map") };
}

FBridgeToolResult UAnimBlueprintRetargetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString SourceBlueprintPath = GetStringArgOrDefault(Arguments, TEXT("source_blueprint"), TEXT(""));
	const FString TargetBlueprintPath = GetStringArgOrDefault(Arguments, TEXT("target_blueprint"), TEXT(""));
	const FString TargetSkeletonPath = GetStringArgOrDefault(Arguments, TEXT("target_skeleton"), TEXT(""));
	if (SourceBlueprintPath.IsEmpty() || TargetBlueprintPath.IsEmpty() || TargetSkeletonPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("anim-retarget-blueprint: source_blueprint, target_blueprint, and target_skeleton are required"));
	}

	TMap<FName, FName> BoneMap;
	TSharedPtr<FJsonObject> BoneMapJson;
	FString Error;
	if (!SoftUE::AnimBoneReferenceUtils::LoadBoneMap(Arguments, TEXT("anim-retarget-blueprint"), BoneMap, BoneMapJson, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimBlueprint* SourceBlueprint = FBridgeAssetModifier::LoadAssetByPath<UAnimBlueprint>(SourceBlueprintPath, Error);
	if (!SourceBlueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	USkeleton* TargetSkeleton = FBridgeAssetModifier::LoadAssetByPath<USkeleton>(TargetSkeletonPath, Error);
	if (!TargetSkeleton)
	{
		return FBridgeToolResult::Error(Error);
	}

	if (FBridgeAssetModifier::AssetExists(TargetBlueprintPath))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Target AnimBlueprint already exists: %s"), *TargetBlueprintPath));
	}

	if (!FBridgeAssetModifier::ValidateAssetPath(TargetBlueprintPath, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	const FString TargetPackagePath = FPackageName::GetLongPackagePath(TargetBlueprintPath);
	const FString TargetAssetName = FPackageName::GetShortName(TargetBlueprintPath);

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Retarget AnimBlueprint Bone References")));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* DuplicatedObject = AssetTools.DuplicateAsset(TargetAssetName, TargetPackagePath, SourceBlueprint);
	UAnimBlueprint* TargetBlueprint = Cast<UAnimBlueprint>(DuplicatedObject);
	if (!TargetBlueprint)
	{
		return FBridgeToolResult::Error(TEXT("Failed to duplicate source AnimBlueprint"));
	}

	FBridgeAssetModifier::MarkModified(TargetBlueprint);
	TargetBlueprint->TargetSkeleton = TargetSkeleton;

	TArray<UEdGraph*> Graphs;
	FBridgeAssetModifier::GetAllSearchableGraphs(TargetBlueprint, Graphs);

	TArray<SoftUE::AnimBoneReferenceUtils::FBoneReferenceChange> Changes;
	int32 ChangedNodeCount = 0;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			const int32 BeforeCount = Changes.Num();
			SoftUE::AnimBoneReferenceUtils::RemapBoneReferences(Node, BoneMap, Changes);
			if (Changes.Num() != BeforeCount)
			{
				Node->Modify();
				++ChangedNodeCount;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(TargetBlueprint);
	FBridgeAssetModifier::MarkPackageDirty(TargetBlueprint);
	FBridgeAssetModifier::RefreshBlueprintNodes(TargetBlueprint);
	if (!FBridgeAssetModifier::CompileBlueprint(TargetBlueprint, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	if (!SoftUE::AnimBoneReferenceUtils::CheckoutObjectPackageIfRequested(TargetBlueprint, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	if (bSave && !FBridgeAssetModifier::SaveAsset(TargetBlueprint, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TArray<SoftUE::AnimBoneReferenceUtils::FBoneReferenceRecord> AfterReferences;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				SoftUE::AnimBoneReferenceUtils::CollectBoneReferences(Node, AfterReferences);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_blueprint"), SourceBlueprint->GetPathName());
	Result->SetStringField(TEXT("target_blueprint"), TargetBlueprint->GetPathName());
	Result->SetStringField(TEXT("target_skeleton"), TargetSkeleton->GetPathName());
	Result->SetNumberField(TEXT("changed_bone_reference_count"), Changes.Num());
	Result->SetNumberField(TEXT("changed_node_count"), ChangedNodeCount);
	Result->SetBoolField(TEXT("saved"), bSave);
	Result->SetObjectField(TEXT("bone_map"), BoneMapJson);
	Result->SetArrayField(TEXT("changes"), SoftUE::AnimBoneReferenceUtils::BoneChangesToJson(Changes));
	Result->SetArrayField(TEXT("bone_references_after"), SoftUE::AnimBoneReferenceUtils::BoneRecordsToJson(AfterReferences));
	Result->SetArrayField(TEXT("unique_bones_after"), SoftUE::AnimBoneReferenceUtils::UniqueBoneNamesToJson(AfterReferences));
	return FBridgeToolResult::Json(Result);
}
