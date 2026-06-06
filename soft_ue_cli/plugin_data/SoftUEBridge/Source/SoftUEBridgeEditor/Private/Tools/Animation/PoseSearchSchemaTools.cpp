// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/PoseSearchSchemaTools.h"

#include "Tools/Animation/AnimBoneReferenceUtils.h"
#include "Tools/Asset/AssetReferenceRepointUtils.h"
#include "Utils/BridgeAssetModifier.h"

#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

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

UObject* LoadPoseSearchDatabase(const FString& DatabasePath, FString& OutError)
{
	UObject* Database = FBridgeAssetModifier::LoadAssetByPath(DatabasePath, OutError);
	if (!Database)
	{
		return nullptr;
	}

	if (!SoftUE::AssetReferenceRepointUtils::LooksLikePoseSearchDatabase(Database))
	{
		OutError = FString::Printf(
			TEXT("Asset '%s' is '%s', not a PoseSearchDatabase"),
			*DatabasePath,
			*Database->GetClass()->GetName());
		return nullptr;
	}
	return Database;
}

bool TryInvokeNoParamFunction(UObject* Object, const TArray<FName>& FunctionNames, FString& OutFunctionName)
{
	if (!Object)
	{
		return false;
	}

	for (const FName& FunctionName : FunctionNames)
	{
		UFunction* Function = Object->FindFunction(FunctionName);
		if (Function && Function->NumParms == 0)
		{
			Object->ProcessEvent(Function, nullptr);
			OutFunctionName = FunctionName.ToString();
			return true;
		}
	}
	return false;
}

struct FPoseSearchReindexResult
{
	bool bInvoked = false;
	bool bCompleted = false;
	FString Method;
};

FString JoinReindexMethods(const TArray<FString>& Methods)
{
	FString Joined;
	for (const FString& Method : Methods)
	{
		if (!Joined.IsEmpty())
		{
			Joined += TEXT("; ");
		}
		Joined += Method;
	}
	return Joined;
}

void NotifyChangedPoseSearchProperty(UObject* Database, const FName& PropertyName, TArray<FString>& Methods)
{
	if (!Database || PropertyName.IsNone())
	{
		return;
	}

	FProperty* Property = Database->GetClass()->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return;
	}

	FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
	Database->PostEditChangeProperty(PropertyChangedEvent);
	Methods.Add(FString::Printf(TEXT("PostEditChangeProperty(%s)"), *PropertyName.ToString()));
}

FPoseSearchReindexResult TryReindexPoseSearchDatabase(UObject* Database, const TArray<FName>& ChangedProperties)
{
	FPoseSearchReindexResult Result;
	if (!Database)
	{
		return Result;
	}

	TArray<FString> Methods;
	for (const FName& PropertyName : ChangedProperties)
	{
		NotifyChangedPoseSearchProperty(Database, PropertyName, Methods);
	}
	if (!Methods.IsEmpty())
	{
		Result.bInvoked = true;
	}

#if WITH_EDITOR
	Database->BeginCacheForCookedPlatformData(nullptr);
	Result.bInvoked = true;
	Methods.Add(TEXT("BeginCacheForCookedPlatformData"));

	constexpr int32 MaxCompletionPolls = 10;
	for (int32 Attempt = 0; Attempt < MaxCompletionPolls; ++Attempt)
	{
		Result.bCompleted = Database->IsCachedCookedPlatformDataLoaded(nullptr);
		if (Result.bCompleted)
		{
			break;
		}
		FPlatformProcess::Sleep(0.05f);
	}
	if (!Result.bCompleted)
	{
		Result.bCompleted = Database->IsCachedCookedPlatformDataLoaded(nullptr);
	}
	Methods.Add(Result.bCompleted
		? TEXT("IsCachedCookedPlatformDataLoaded(completed)")
		: TEXT("IsCachedCookedPlatformDataLoaded(pending)"));
#endif

	FString ReflectedFunctionName;
	if (TryInvokeNoParamFunction(
		Database,
		{
			TEXT("BuildIndex"),
			TEXT("BuildSearchIndex"),
			TEXT("RequestAsyncBuildIndex"),
			TEXT("BeginCacheDerivedData")
		},
		ReflectedFunctionName))
	{
		Result.bInvoked = true;
		Methods.Add(FString::Printf(TEXT("ProcessEvent(%s)"), *ReflectedFunctionName));
	}

	Result.Method = JoinReindexMethods(Methods);
	return Result;
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

FString UPoseSearchDatabaseRepointTool::GetToolDescription() const
{
	return TEXT("Repoint PoseSearchDatabase schema and nested animation asset references, with optional best-effort reindexing.");
}

TMap<FString, FBridgeSchemaProperty> UPoseSearchDatabaseRepointTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("database_path"), PoseSearchSchemaProperty(TEXT("string"), TEXT("PoseSearchDatabase asset path"), true));
	Schema.Add(TEXT("schema_path"), PoseSearchSchemaProperty(TEXT("string"), TEXT("Optional target PoseSearchSchema asset path")));
	Schema.Add(TEXT("animation_asset_map"), PoseSearchSchemaProperty(TEXT("object"), TEXT("Optional map of old animation asset path to new animation asset path")));
	Schema.Add(TEXT("reindex"), PoseSearchSchemaProperty(TEXT("boolean"), TEXT("Best-effort trigger of PoseSearchDatabase reindexing after mutation")));
	Schema.Add(TEXT("save"), PoseSearchSchemaProperty(TEXT("boolean"), TEXT("Save the database after mutation")));
	Schema.Add(TEXT("checkout"), PoseSearchSchemaProperty(TEXT("boolean"), TEXT("Checkout the database before mutation when source control is active")));
	return Schema;
}

TArray<FString> UPoseSearchDatabaseRepointTool::GetRequiredParams() const
{
	return { TEXT("database_path") };
}

FBridgeToolResult UPoseSearchDatabaseRepointTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString DatabasePath = GetStringArgOrDefault(Arguments, TEXT("database_path"), TEXT(""));
	if (DatabasePath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("pose-search-database-repoint: database_path is required"));
	}

	const FString TargetSchemaPath = GetStringArgOrDefault(Arguments, TEXT("schema_path"), TEXT(""));
	const TSharedPtr<FJsonObject>* AnimationAssetMapJson = nullptr;
	const bool bHasAnimationAssetMap = Arguments.IsValid()
		&& Arguments->TryGetObjectField(TEXT("animation_asset_map"), AnimationAssetMapJson)
		&& AnimationAssetMapJson;
	if (TargetSchemaPath.IsEmpty() && !bHasAnimationAssetMap)
	{
		return FBridgeToolResult::Error(TEXT("pose-search-database-repoint: provide schema_path, animation_asset_map, or both"));
	}

	FString Error;
	UObject* Database = LoadPoseSearchDatabase(DatabasePath, Error);
	if (!Database)
	{
		return FBridgeToolResult::Error(Error);
	}

	UObject* TargetSchema = nullptr;
	if (!TargetSchemaPath.IsEmpty())
	{
		TargetSchema = LoadPoseSearchSchema(TargetSchemaPath, Error);
		if (!TargetSchema)
		{
			return FBridgeToolResult::Error(Error);
		}
	}

	SoftUE::AssetReferenceRepointUtils::FReplacementMap AnimationAssetMap;
	if (bHasAnimationAssetMap)
	{
		if (!SoftUE::AssetReferenceRepointUtils::LoadReplacementMap(
			Arguments,
			TEXT("animation_asset_map"),
			TEXT("pose-search-database-repoint"),
			AnimationAssetMap,
			Error))
		{
			return FBridgeToolResult::Error(Error);
		}
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	const bool bReindex = GetBoolArgOrDefault(Arguments, TEXT("reindex"), false);
	if (!SoftUE::AssetReferenceRepointUtils::CheckoutObjectPackageIfRequested(Database, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Repoint PoseSearch Database")));

	FBridgeAssetModifier::MarkModified(Database);

	TArray<SoftUE::AssetReferenceRepointUtils::FAssetReferenceChange> SchemaChanges;
	if (TargetSchema)
	{
		SoftUE::AssetReferenceRepointUtils::SetNamedObjectReferences(
			Database,
			{ TEXT("Schema") },
			TargetSchema,
			SchemaChanges);
	}

	TArray<SoftUE::AssetReferenceRepointUtils::FAssetReferenceChange> AnimationAssetChanges;
	if (bHasAnimationAssetMap)
	{
		SoftUE::AssetReferenceRepointUtils::RepointObjectReferences(Database, AnimationAssetMap, AnimationAssetChanges);
	}

	const bool bChanged = SchemaChanges.Num() > 0 || AnimationAssetChanges.Num() > 0;
	bool bReindexInvoked = false;
	bool bReindexCompleted = false;
	FString ReindexFunction;
	if (bChanged)
	{
		Database->PostEditChange();
		FBridgeAssetModifier::MarkPackageDirty(Database);
		if (bReindex)
		{
			TArray<FName> ChangedProperties;
			if (SchemaChanges.Num() > 0)
			{
				ChangedProperties.AddUnique(TEXT("Schema"));
			}
			if (AnimationAssetChanges.Num() > 0)
			{
				ChangedProperties.AddUnique(TEXT("DatabaseAnimationAssets"));
				ChangedProperties.AddUnique(TEXT("AnimationAssets"));
				ChangedProperties.AddUnique(TEXT("AnimationAssets_DEPRECATED"));
			}

			const FPoseSearchReindexResult ReindexResult = TryReindexPoseSearchDatabase(Database, ChangedProperties);
			bReindexInvoked = ReindexResult.bInvoked;
			bReindexCompleted = ReindexResult.bCompleted;
			ReindexFunction = ReindexResult.Method;
		}
	}

	if (bSave && bChanged && !FBridgeAssetModifier::SaveAsset(Database, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("database_path"), Database->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Database->GetClass()->GetName());
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetNumberField(TEXT("changed_schema_reference_count"), SchemaChanges.Num());
	Result->SetNumberField(TEXT("changed_animation_asset_reference_count"), AnimationAssetChanges.Num());
	Result->SetArrayField(TEXT("schema_changes"), SoftUE::AssetReferenceRepointUtils::ChangesToJson(SchemaChanges));
	Result->SetArrayField(TEXT("animation_asset_changes"), SoftUE::AssetReferenceRepointUtils::ChangesToJson(AnimationAssetChanges));
	Result->SetBoolField(TEXT("reindex_requested"), bReindex);
	Result->SetBoolField(TEXT("reindex_invoked"), bReindexInvoked);
	Result->SetBoolField(TEXT("reindex_completed"), bReindexCompleted);
	Result->SetStringField(TEXT("reindex_function"), ReindexFunction);
	Result->SetBoolField(TEXT("saved"), bChanged && bSave);
	if (TargetSchema)
	{
		Result->SetStringField(TEXT("schema_path"), TargetSchema->GetPathName());
	}
	if (bHasAnimationAssetMap)
	{
		Result->SetObjectField(TEXT("animation_asset_map"), AnimationAssetMap.ReplacementPaths);
	}
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
