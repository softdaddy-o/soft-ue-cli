// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimSequenceRetargetTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "Retargeter/IKRetargeter.h"

namespace
{
FBridgeSchemaProperty RetargetSequenceSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

FString NormalizeAssetPath(const FString& AssetPath)
{
	const FString Trimmed = AssetPath.TrimStartAndEnd();
	if (Trimmed.Contains(TEXT(".")))
	{
		return FPackageName::ObjectPathToPackageName(Trimmed);
	}
	return Trimmed;
}

bool ValidatePackagePath(const FString& CommandName, const FString& FieldName, const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s: %s is required"), *CommandName, *FieldName);
		return false;
	}
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		OutError = FString::Printf(TEXT("%s: %s must be a long package path starting with '/'"), *CommandName, *FieldName);
		return false;
	}
	FText PackageError;
	if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageError))
	{
		OutError = FString::Printf(TEXT("%s: invalid %s '%s': %s"), *CommandName, *FieldName, *AssetPath, *PackageError.ToString());
		return false;
	}
	return true;
}

bool CheckoutTargetIfRequested(const FString& TargetSequencePath, bool bCheckout, FString& OutError)
{
	if (!bCheckout)
	{
		return true;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		TargetSequencePath,
		FPackageName::GetAssetPackageExtension());
	if (!IFileManager::Get().FileExists(*PackageFileName))
	{
		return true;
	}

	return FBridgeAssetModifier::CheckoutFile(PackageFileName, OutError);
}
}

FString UAnimSequenceRetargetTool::GetToolDescription() const
{
	return TEXT("Retarget one AnimSequence through Unreal's native IK Retargeter batch operation.");
}

TMap<FString, FBridgeSchemaProperty> UAnimSequenceRetargetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("source_sequence"), RetargetSequenceSchemaProperty(TEXT("string"), TEXT("Source AnimSequence asset path"), true));
	Schema.Add(TEXT("target_sequence"), RetargetSequenceSchemaProperty(TEXT("string"), TEXT("Target AnimSequence asset path to create or replace"), true));
	Schema.Add(TEXT("source_mesh"), RetargetSequenceSchemaProperty(TEXT("string"), TEXT("Source SkeletalMesh asset path"), true));
	Schema.Add(TEXT("target_mesh"), RetargetSequenceSchemaProperty(TEXT("string"), TEXT("Target SkeletalMesh asset path"), true));
	Schema.Add(TEXT("ik_retargeter"), RetargetSequenceSchemaProperty(TEXT("string"), TEXT("IK Retargeter asset path"), true));
	Schema.Add(TEXT("overwrite"), RetargetSequenceSchemaProperty(TEXT("boolean"), TEXT("Replace an existing target asset with the same name")));
	Schema.Add(TEXT("save"), RetargetSequenceSchemaProperty(TEXT("boolean"), TEXT("Save the retargeted sequence after mutation")));
	Schema.Add(TEXT("checkout"), RetargetSequenceSchemaProperty(TEXT("boolean"), TEXT("Checkout the target sequence before replacing it")));
	return Schema;
}

TArray<FString> UAnimSequenceRetargetTool::GetRequiredParams() const
{
	return {
		TEXT("source_sequence"),
		TEXT("target_sequence"),
		TEXT("source_mesh"),
		TEXT("target_mesh"),
		TEXT("ik_retargeter"),
	};
}

FBridgeToolResult UAnimSequenceRetargetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString SourceSequencePath = NormalizeAssetPath(GetStringArgOrDefault(Arguments, TEXT("source_sequence"), TEXT("")));
	const FString TargetSequencePath = NormalizeAssetPath(GetStringArgOrDefault(Arguments, TEXT("target_sequence"), TEXT("")));
	const FString SourceMeshPath = NormalizeAssetPath(GetStringArgOrDefault(Arguments, TEXT("source_mesh"), TEXT("")));
	const FString TargetMeshPath = NormalizeAssetPath(GetStringArgOrDefault(Arguments, TEXT("target_mesh"), TEXT("")));
	const FString RetargeterPath = NormalizeAssetPath(GetStringArgOrDefault(Arguments, TEXT("ik_retargeter"), TEXT("")));
	const bool bOverwrite = GetBoolArgOrDefault(Arguments, TEXT("overwrite"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);

	FString Error;
	if (!ValidatePackagePath(TEXT("anim-retarget-sequence"), TEXT("source_sequence"), SourceSequencePath, Error) ||
		!ValidatePackagePath(TEXT("anim-retarget-sequence"), TEXT("target_sequence"), TargetSequencePath, Error) ||
		!ValidatePackagePath(TEXT("anim-retarget-sequence"), TEXT("source_mesh"), SourceMeshPath, Error) ||
		!ValidatePackagePath(TEXT("anim-retarget-sequence"), TEXT("target_mesh"), TargetMeshPath, Error) ||
		!ValidatePackagePath(TEXT("anim-retarget-sequence"), TEXT("ik_retargeter"), RetargeterPath, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	if (SourceSequencePath == TargetSequencePath)
	{
		return FBridgeToolResult::Error(TEXT("anim-retarget-sequence: source_sequence and target_sequence must be different"));
	}
	if (FPackageName::DoesPackageExist(TargetSequencePath) && !bOverwrite)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("anim-retarget-sequence: target_sequence '%s' already exists; pass overwrite=true to replace it"),
			*TargetSequencePath));
	}
	if (!CheckoutTargetIfRequested(TargetSequencePath, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimSequence* SourceSequence = FBridgeAssetModifier::LoadAssetByPath<UAnimSequence>(SourceSequencePath, Error);
	if (!SourceSequence)
	{
		return FBridgeToolResult::Error(Error);
	}

	USkeletalMesh* SourceMesh = FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(SourceMeshPath, Error);
	if (!SourceMesh)
	{
		return FBridgeToolResult::Error(Error);
	}

	USkeletalMesh* TargetMesh = FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(TargetMeshPath, Error);
	if (!TargetMesh)
	{
		return FBridgeToolResult::Error(Error);
	}

	UIKRetargeter* Retargeter = FBridgeAssetModifier::LoadAssetByPath<UIKRetargeter>(RetargeterPath, Error);
	if (!Retargeter)
	{
		return FBridgeToolResult::Error(Error);
	}

	const FString TargetPackagePath = FPackageName::GetLongPackagePath(TargetSequencePath);
	const FString TargetAssetName = FPackageName::GetShortName(TargetSequencePath);

	FIKRetargetBatchOperationContext RetargetContext;
	RetargetContext.AssetsToRetarget.Add(TWeakObjectPtr<UObject>(SourceSequence));
	RetargetContext.SourceMesh = SourceMesh;
	RetargetContext.TargetMesh = TargetMesh;
	RetargetContext.IKRetargetAsset = Retargeter;
	RetargetContext.NameRule.FolderPath = TargetPackagePath;
	RetargetContext.NameRule.ReplaceFrom = SourceSequence->GetName();
	RetargetContext.NameRule.ReplaceTo = TargetAssetName;
	RetargetContext.bIncludeReferencedAssets = false;
	RetargetContext.bOverwriteExistingFiles = bOverwrite;
	RetargetContext.bUseSourcePath = false;

	UIKRetargetBatchOperation* BatchOperation = NewObject<UIKRetargetBatchOperation>();
	BatchOperation->AddToRoot();
	BatchOperation->RunRetarget(RetargetContext);
	BatchOperation->RemoveFromRoot();

	const FString TargetObjectPath = TargetSequencePath + TEXT(".") + TargetAssetName;
	UAnimSequence* TargetSequence = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *TargetObjectPath));
	if (!TargetSequence)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("anim-retarget-sequence: retarget operation did not create expected target '%s'"),
			*TargetSequencePath));
	}

	if (bSave && !FBridgeAssetModifier::SaveAsset(TargetSequence, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_sequence"), SourceSequence->GetPathName());
	Result->SetStringField(TEXT("target_sequence"), TargetSequence->GetPathName());
	Result->SetStringField(TEXT("source_mesh"), SourceMesh->GetPathName());
	Result->SetStringField(TEXT("target_mesh"), TargetMesh->GetPathName());
	Result->SetStringField(TEXT("ik_retargeter"), Retargeter->GetPathName());
	Result->SetBoolField(TEXT("overwrite"), bOverwrite);
	Result->SetBoolField(TEXT("saved"), bSave);
	Result->SetNumberField(TEXT("sequence_length"), TargetSequence->GetPlayLength());
	return FBridgeToolResult::Json(Result);
}
