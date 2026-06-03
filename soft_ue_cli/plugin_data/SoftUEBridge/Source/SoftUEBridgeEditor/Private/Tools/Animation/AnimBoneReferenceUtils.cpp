// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimBoneReferenceUtils.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/BoneReference.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace SoftUE::AnimBoneReferenceUtils
{
namespace
{
struct FVisitContext
{
	TSet<const UObject*> VisitedObjects;
	TArray<FBoneReferenceRecord>* Records = nullptr;
	TArray<FBoneReferenceChange>* Changes = nullptr;
	TArray<TSharedPtr<FJsonValue>>* Skeletons = nullptr;
	TArray<TSharedPtr<FJsonValue>>* SkeletonChanges = nullptr;
	const TMap<FName, FName>* BoneMap = nullptr;
	USkeleton* TargetSkeleton = nullptr;
	bool bMutate = false;
};

FString JoinPath(const FString& Parent, const FString& Child)
{
	if (Parent.IsEmpty())
	{
		return Child;
	}
	return Parent + TEXT(".") + Child;
}

FString IndexedPath(const FString& Parent, int32 Index)
{
	return FString::Printf(TEXT("%s[%d]"), *Parent, Index);
}

FString MapValuePath(const FString& Parent, const TCHAR* Suffix)
{
	return FString::Printf(TEXT("%s.%s"), *Parent, Suffix);
}

bool IsBoneReferenceStruct(const UScriptStruct* Struct)
{
	return Struct && (Struct == FBoneReference::StaticStruct() || Struct->GetFName() == TEXT("BoneReference"));
}

TSharedPtr<FJsonObject> MakeBoneReferenceJson(const FString& OwnerPath, const FString& PropertyPath, const FName& BoneName)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("owner"), OwnerPath);
	Json->SetStringField(TEXT("path"), PropertyPath);
	Json->SetStringField(TEXT("bone"), BoneName.ToString());
	return Json;
}

void VisitStruct(UStruct* Struct, void* StructData, UObject* RootObject, UObject* OwnerObject, const FString& PropertyPath, FVisitContext& Context);

void VisitObject(UObject* Object, UObject* RootObject, FVisitContext& Context)
{
	if (!Object || Context.VisitedObjects.Contains(Object))
	{
		return;
	}

	Context.VisitedObjects.Add(Object);
	VisitStruct(Object->GetClass(), Object, RootObject ? RootObject : Object, Object, TEXT(""), Context);
}

void RecordOrRemapBoneReference(
	FStructProperty* StructProperty,
	void* StructData,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	FNameProperty* BoneNameProperty = FindFProperty<FNameProperty>(StructProperty->Struct, TEXT("BoneName"));
	if (!BoneNameProperty)
	{
		return;
	}

	void* BoneNamePtr = BoneNameProperty->ContainerPtrToValuePtr<void>(StructData);
	const FName OldBoneName = BoneNameProperty->GetPropertyValue(BoneNamePtr);
	if (OldBoneName.IsNone())
	{
		return;
	}

	const FString OwnerPath = OwnerObject ? OwnerObject->GetPathName() : TEXT("");
	if (Context.Records)
	{
		Context.Records->Add({ OwnerPath, PropertyPath, OldBoneName });
	}

	if (Context.bMutate && Context.BoneMap)
	{
		const FName* NewBoneName = Context.BoneMap->Find(OldBoneName);
		if (NewBoneName && *NewBoneName != OldBoneName)
		{
			if (OwnerObject)
			{
				OwnerObject->Modify();
			}
			BoneNameProperty->SetPropertyValue(BoneNamePtr, *NewBoneName);
			if (Context.Changes)
			{
				Context.Changes->Add({ OwnerPath, PropertyPath, OldBoneName, *NewBoneName });
			}
		}
	}
}

bool IsTraversableInstancedObject(UObject* Object, UObject* RootObject)
{
	if (!Object || Object == RootObject || Object->IsA<UPackage>() || Object->IsA<UClass>())
	{
		return false;
	}
	return RootObject && Object->IsIn(RootObject);
}

void RecordOrSetSkeletonReference(
	FObjectPropertyBase* ObjectProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (ObjectProperty->GetFName() != TEXT("Skeleton"))
	{
		return;
	}

	UObject* CurrentObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
	USkeleton* CurrentSkeleton = Cast<USkeleton>(CurrentObject);
	const bool bCanHoldSkeleton = CurrentSkeleton || (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(USkeleton::StaticClass()));
	if (!bCanHoldSkeleton)
	{
		return;
	}

	const FString OwnerPath = OwnerObject ? OwnerObject->GetPathName() : TEXT("");
	if (Context.Skeletons && CurrentSkeleton)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("owner"), OwnerPath);
		Json->SetStringField(TEXT("path"), PropertyPath);
		Json->SetStringField(TEXT("skeleton"), CurrentSkeleton->GetPathName());
		Context.Skeletons->Add(MakeShared<FJsonValueObject>(Json));
	}

	if (Context.bMutate && Context.TargetSkeleton && CurrentSkeleton != Context.TargetSkeleton)
	{
		if (OwnerObject)
		{
			OwnerObject->Modify();
		}
		ObjectProperty->SetObjectPropertyValue(ValuePtr, Context.TargetSkeleton);
		if (Context.SkeletonChanges)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("owner"), OwnerPath);
			Json->SetStringField(TEXT("path"), PropertyPath);
			Json->SetStringField(TEXT("old_skeleton"), CurrentSkeleton ? CurrentSkeleton->GetPathName() : TEXT(""));
			Json->SetStringField(TEXT("new_skeleton"), Context.TargetSkeleton->GetPathName());
			Context.SkeletonChanges->Add(MakeShared<FJsonValueObject>(Json));
		}
	}
}

void VisitPropertyValue(
	FProperty* Property,
	void* ValuePtr,
	UObject* RootObject,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (IsBoneReferenceStruct(StructProperty->Struct))
		{
			RecordOrRemapBoneReference(StructProperty, ValuePtr, OwnerObject, PropertyPath, Context);
			return;
		}
		VisitStruct(StructProperty->Struct, ValuePtr, RootObject, OwnerObject, PropertyPath, Context);
		return;
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			VisitPropertyValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), RootObject, OwnerObject, IndexedPath(PropertyPath, Index), Context);
		}
		return;
	}

	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				VisitPropertyValue(SetProperty->ElementProp, Helper.GetElementPtr(Index), RootObject, OwnerObject, IndexedPath(PropertyPath, Index), Context);
			}
		}
		return;
	}

	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper Helper(MapProperty, ValuePtr);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (Helper.IsValidIndex(Index))
			{
				VisitPropertyValue(MapProperty->KeyProp, Helper.GetKeyPtr(Index), RootObject, OwnerObject, MapValuePath(IndexedPath(PropertyPath, Index), TEXT("key")), Context);
				VisitPropertyValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), RootObject, OwnerObject, MapValuePath(IndexedPath(PropertyPath, Index), TEXT("value")), Context);
			}
		}
		return;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		RecordOrSetSkeletonReference(ObjectProperty, ValuePtr, OwnerObject, PropertyPath, Context);

		UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		if (IsTraversableInstancedObject(ObjectValue, RootObject))
		{
			VisitObject(ObjectValue, RootObject, Context);
		}
	}
}

void VisitStruct(UStruct* Struct, void* StructData, UObject* RootObject, UObject* OwnerObject, const FString& PropertyPath, FVisitContext& Context)
{
	if (!Struct || !StructData)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Property = *It;
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructData);
		VisitPropertyValue(Property, ValuePtr, RootObject, OwnerObject, JoinPath(PropertyPath, Property->GetName()), Context);
	}
}
}

bool LoadBoneMap(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& ToolName,
	TMap<FName, FName>& OutBoneMap,
	TSharedPtr<FJsonObject>& OutBoneMapJson,
	FString& OutError)
{
	const TSharedPtr<FJsonObject>* BoneMapObject = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(TEXT("bone_map"), BoneMapObject) || !BoneMapObject)
	{
		OutError = FString::Printf(TEXT("%s: bone_map is required"), *ToolName);
		return false;
	}

	OutBoneMapJson = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*BoneMapObject)->Values)
	{
		const FString OldName = Pair.Key.TrimStartAndEnd();
		const FString NewName = Pair.Value.IsValid() ? Pair.Value->AsString().TrimStartAndEnd() : TEXT("");
		if (OldName.IsEmpty() || NewName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s: bone_map entries must be old_bone: new_bone strings"), *ToolName);
			return false;
		}
		OutBoneMap.Add(FName(*OldName), FName(*NewName));
		OutBoneMapJson->SetStringField(OldName, NewName);
	}

	if (OutBoneMap.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s: bone_map must contain at least one entry"), *ToolName);
		return false;
	}

	return true;
}

void CollectBoneReferences(UObject* RootObject, TArray<FBoneReferenceRecord>& OutRecords)
{
	FVisitContext Context;
	Context.Records = &OutRecords;
	VisitObject(RootObject, RootObject, Context);
}

int32 RemapBoneReferences(
	UObject* RootObject,
	const TMap<FName, FName>& BoneMap,
	TArray<FBoneReferenceChange>& OutChanges)
{
	FVisitContext Context;
	Context.BoneMap = &BoneMap;
	Context.Changes = &OutChanges;
	Context.bMutate = true;
	VisitObject(RootObject, RootObject, Context);
	return OutChanges.Num();
}

void CollectSkeletonReferences(UObject* RootObject, TArray<TSharedPtr<FJsonValue>>& OutSkeletons)
{
	FVisitContext Context;
	Context.Skeletons = &OutSkeletons;
	VisitObject(RootObject, RootObject, Context);
}

int32 SetSkeletonReferences(
	UObject* RootObject,
	USkeleton* TargetSkeleton,
	TArray<TSharedPtr<FJsonValue>>& OutChanges)
{
	FVisitContext Context;
	Context.TargetSkeleton = TargetSkeleton;
	Context.SkeletonChanges = &OutChanges;
	Context.bMutate = true;
	VisitObject(RootObject, RootObject, Context);
	return OutChanges.Num();
}

TArray<TSharedPtr<FJsonValue>> BoneRecordsToJson(const TArray<FBoneReferenceRecord>& Records)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FBoneReferenceRecord& Record : Records)
	{
		Values.Add(MakeShared<FJsonValueObject>(MakeBoneReferenceJson(Record.OwnerPath, Record.PropertyPath, Record.BoneName)));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> BoneChangesToJson(const TArray<FBoneReferenceChange>& Changes)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FBoneReferenceChange& Change : Changes)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("owner"), Change.OwnerPath);
		Json->SetStringField(TEXT("path"), Change.PropertyPath);
		Json->SetStringField(TEXT("old_bone"), Change.OldBoneName.ToString());
		Json->SetStringField(TEXT("new_bone"), Change.NewBoneName.ToString());
		Values.Add(MakeShared<FJsonValueObject>(Json));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> UniqueBoneNamesToJson(const TArray<FBoneReferenceRecord>& Records)
{
	TSet<FName> UniqueBones;
	for (const FBoneReferenceRecord& Record : Records)
	{
		UniqueBones.Add(Record.BoneName);
	}

	TArray<FName> SortedBones = UniqueBones.Array();
	SortedBones.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName& BoneName : SortedBones)
	{
		Values.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
	}
	return Values;
}

bool CheckoutObjectPackageIfRequested(UObject* Object, bool bCheckout, FString& OutError)
{
	if (!bCheckout || !Object)
	{
		return true;
	}

	UPackage* Package = Object->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Object has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	if (!IFileManager::Get().FileExists(*PackageFileName))
	{
		return true;
	}
	return FBridgeAssetModifier::CheckoutFile(PackageFileName, OutError);
}

bool LooksLikePoseSearchSchema(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	UClass* Class = Object->GetClass();
	while (Class)
	{
		if (Class->GetFName() == TEXT("PoseSearchSchema") || Class->GetPathName().Contains(TEXT("PoseSearchSchema")))
		{
			return true;
		}
		Class = Class->GetSuperClass();
	}
	return false;
}
}
