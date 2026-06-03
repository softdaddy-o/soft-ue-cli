// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/PoseSearchSchemaTools.h"

#include "Tools/Animation/AnimBoneReferenceUtils.h"
#include "Utils/BridgeAssetModifier.h"

#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty PoseSearchSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

UObject* LoadPoseSearchSchema(const FString& SchemaPath, FString& OutError)
{
	UObject* Schema = FBridgeAssetModifier::LoadAssetByPath(SchemaPath, OutError);
	if (!Schema)
	{
		return nullptr;
	}

	if (!SoftUE::AnimBoneReferenceUtils::LooksLikePoseSearchSchema(Schema))
	{
		OutError = FString::Printf(
			TEXT("Asset '%s' is '%s', not a PoseSearchSchema"),
			*SchemaPath,
			*Schema->GetClass()->GetName());
		return nullptr;
	}
	return Schema;
}

void AddSchemaInspectionFields(UObject* Schema, TSharedPtr<FJsonObject>& Result)
{
	TArray<SoftUE::AnimBoneReferenceUtils::FBoneReferenceRecord> BoneReferences;
	TArray<TSharedPtr<FJsonValue>> Skeletons;
	SoftUE::AnimBoneReferenceUtils::CollectBoneReferences(Schema, BoneReferences);
	SoftUE::AnimBoneReferenceUtils::CollectSkeletonReferences(Schema, Skeletons);

	Result->SetArrayField(TEXT("skeletons"), Skeletons);
	Result->SetNumberField(TEXT("skeleton_count"), Skeletons.Num());
	Result->SetArrayField(TEXT("bone_references"), SoftUE::AnimBoneReferenceUtils::BoneRecordsToJson(BoneReferences));
	Result->SetArrayField(TEXT("unique_bones"), SoftUE::AnimBoneReferenceUtils::UniqueBoneNamesToJson(BoneReferences));
	Result->SetNumberField(TEXT("bone_reference_count"), BoneReferences.Num());
}
}

FString UPoseSearchSchemaInspectTool::GetToolDescription() const
{
	return TEXT("Inspect a PoseSearchSchema asset and list skeleton references plus sampled FBoneReference bone names.");
}

TMap<FString, FBridgeSchemaProperty> UPoseSearchSchemaInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("schema_path"), PoseSearchSchemaProperty(TEXT("string"), TEXT("PoseSearchSchema asset path"), true));
	return Schema;
}

TArray<FString> UPoseSearchSchemaInspectTool::GetRequiredParams() const
{
	return { TEXT("schema_path") };
}

FBridgeToolResult UPoseSearchSchemaInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString SchemaPath = GetStringArgOrDefault(Arguments, TEXT("schema_path"), TEXT(""));
	if (SchemaPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("pose-search-schema-inspect: schema_path is required"));
	}

	FString Error;
	UObject* Schema = LoadPoseSearchSchema(SchemaPath, Error);
	if (!Schema)
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("schema_path"), Schema->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Schema->GetClass()->GetName());
	AddSchemaInspectionFields(Schema, Result);
	return FBridgeToolResult::Json(Result);
}

FString UPoseSearchSchemaRemapTool::GetToolDescription() const
{
	return TEXT("Remap PoseSearchSchema sampled FBoneReference bone names and optionally replace schema skeleton references.");
}

TMap<FString, FBridgeSchemaProperty> UPoseSearchSchemaRemapTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("schema_path"), PoseSearchSchemaProperty(TEXT("string"), TEXT("PoseSearchSchema asset path"), true));
	Schema.Add(TEXT("bone_map"), PoseSearchSchemaProperty(TEXT("object"), TEXT("Map of old bone name to new bone name"), true));
	Schema.Add(TEXT("target_skeleton"), PoseSearchSchemaProperty(TEXT("string"), TEXT("Optional skeleton asset path to assign to schema skeleton references")));
	Schema.Add(TEXT("save"), PoseSearchSchemaProperty(TEXT("boolean"), TEXT("Save the schema after mutation")));
	Schema.Add(TEXT("checkout"), PoseSearchSchemaProperty(TEXT("boolean"), TEXT("Checkout the schema before mutation when source control is active")));
	return Schema;
}

TArray<FString> UPoseSearchSchemaRemapTool::GetRequiredParams() const
{
	return { TEXT("schema_path"), TEXT("bone_map") };
}

FBridgeToolResult UPoseSearchSchemaRemapTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString SchemaPath = GetStringArgOrDefault(Arguments, TEXT("schema_path"), TEXT(""));
	if (SchemaPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("pose-search-schema-remap: schema_path is required"));
	}

	TMap<FName, FName> BoneMap;
	TSharedPtr<FJsonObject> BoneMapJson;
	FString Error;
	if (!SoftUE::AnimBoneReferenceUtils::LoadBoneMap(Arguments, TEXT("pose-search-schema-remap"), BoneMap, BoneMapJson, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	UObject* Schema = LoadPoseSearchSchema(SchemaPath, Error);
	if (!Schema)
	{
		return FBridgeToolResult::Error(Error);
	}

	USkeleton* TargetSkeleton = nullptr;
	const FString TargetSkeletonPath = GetStringArgOrDefault(Arguments, TEXT("target_skeleton"), TEXT(""));
	if (!TargetSkeletonPath.IsEmpty())
	{
		TargetSkeleton = FBridgeAssetModifier::LoadAssetByPath<USkeleton>(TargetSkeletonPath, Error);
		if (!TargetSkeleton)
		{
			return FBridgeToolResult::Error(Error);
		}
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	if (!SoftUE::AnimBoneReferenceUtils::CheckoutObjectPackageIfRequested(Schema, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Remap PoseSearch Schema Bone References")));

	FBridgeAssetModifier::MarkModified(Schema);

	TArray<SoftUE::AnimBoneReferenceUtils::FBoneReferenceChange> BoneChanges;
	SoftUE::AnimBoneReferenceUtils::RemapBoneReferences(Schema, BoneMap, BoneChanges);

	TArray<TSharedPtr<FJsonValue>> SkeletonChanges;
	if (TargetSkeleton)
	{
		SoftUE::AnimBoneReferenceUtils::SetSkeletonReferences(Schema, TargetSkeleton, SkeletonChanges);
	}

	if (BoneChanges.Num() > 0 || SkeletonChanges.Num() > 0)
	{
		Schema->PostEditChange();
		FBridgeAssetModifier::MarkPackageDirty(Schema);
	}

	if (bSave && !FBridgeAssetModifier::SaveAsset(Schema, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("schema_path"), Schema->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Schema->GetClass()->GetName());
	Result->SetNumberField(TEXT("changed_bone_reference_count"), BoneChanges.Num());
	Result->SetNumberField(TEXT("changed_skeleton_count"), SkeletonChanges.Num());
	Result->SetBoolField(TEXT("saved"), bSave);
	Result->SetObjectField(TEXT("bone_map"), BoneMapJson);
	Result->SetArrayField(TEXT("changes"), SoftUE::AnimBoneReferenceUtils::BoneChangesToJson(BoneChanges));
	Result->SetArrayField(TEXT("skeleton_changes"), SkeletonChanges);
	if (TargetSkeleton)
	{
		Result->SetStringField(TEXT("target_skeleton"), TargetSkeleton->GetPathName());
	}
	AddSchemaInspectionFields(Schema, Result);
	return FBridgeToolResult::Json(Result);
}
