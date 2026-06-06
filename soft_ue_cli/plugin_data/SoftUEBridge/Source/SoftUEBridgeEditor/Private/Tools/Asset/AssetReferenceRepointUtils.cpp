// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/AssetReferenceRepointUtils.h"

#include "Utils/BridgeAssetModifier.h"

#include "Misc/PackageName.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

namespace SoftUE::AssetReferenceRepointUtils
{
namespace
{
struct FVisitContext
{
	TSet<const UObject*> VisitedObjects;
	const FReplacementMap* ReplacementMap = nullptr;
	const TSet<FName>* NamedProperties = nullptr;
	UObject* NamedTargetObject = nullptr;
	TArray<FAssetReferenceChange>* Changes = nullptr;
};

FString JoinPath(const FString& Parent, const FString& Child)
{
	return Parent.IsEmpty() ? Child : Parent + TEXT(".") + Child;
}

FString IndexedPath(const FString& Parent, int32 Index)
{
	return FString::Printf(TEXT("%s[%d]"), *Parent, Index);
}

FString MapValuePath(const FString& Parent, const TCHAR* Suffix)
{
	return FString::Printf(TEXT("%s.%s"), *Parent, Suffix);
}

bool IsTraversableInstancedObject(UObject* Object, UObject* RootObject)
{
	if (!Object || Object == RootObject || Object->IsA<UPackage>() || Object->IsA<UClass>())
	{
		return false;
	}
	return RootObject && Object->IsIn(RootObject);
}

FString OwnerPath(UObject* OwnerObject)
{
	return OwnerObject ? OwnerObject->GetPathName() : TEXT("");
}

bool CanAssignObject(FObjectPropertyBase* Property, UObject* NewObject)
{
	return Property && NewObject && (!Property->PropertyClass || NewObject->IsA(Property->PropertyClass));
}

void AddChange(
	UObject* OwnerObject,
	const FString& PropertyPath,
	const FString& OldReference,
	const FString& NewReference,
	const FString& Kind,
	FVisitContext& Context)
{
	if (Context.Changes)
	{
		Context.Changes->Add({ OwnerPath(OwnerObject), PropertyPath, OldReference, NewReference, Kind });
	}
}

void AddSoftPathVariant(FReplacementMap& ReplacementMap, const FString& OldPath, const FSoftObjectPath& NewPath)
{
	const FString Trimmed = OldPath.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}

	ReplacementMap.SoftObjectPathMap.Add(Trimmed, NewPath);
	if (Trimmed.StartsWith(TEXT("/")) && !Trimmed.Contains(TEXT(".")))
	{
		ReplacementMap.SoftObjectPathMap.Add(
			Trimmed + TEXT(".") + FPackageName::GetShortName(Trimmed),
			NewPath);
	}
}

void AddReplacementPair(
	FReplacementMap& ReplacementMap,
	const FString& OldInputPath,
	UObject* OldObject,
	UObject* NewObject)
{
	if (!OldObject || !NewObject)
	{
		return;
	}

	const FSoftObjectPath NewSoftPath(NewObject);
	ReplacementMap.HardObjectMap.Add(OldObject, NewObject);
	AddSoftPathVariant(ReplacementMap, OldInputPath, NewSoftPath);
	AddSoftPathVariant(ReplacementMap, OldObject->GetPathName(), NewSoftPath);
	AddSoftPathVariant(ReplacementMap, FSoftObjectPath(OldObject).ToString(), NewSoftPath);
	if (UPackage* Package = OldObject->GetOutermost())
	{
		AddSoftPathVariant(ReplacementMap, Package->GetName(), NewSoftPath);
	}
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

bool TrySetNamedObjectReference(
	FObjectPropertyBase* ObjectProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (!ObjectProperty || !Context.NamedProperties || !Context.NamedTargetObject)
	{
		return false;
	}
	if (!Context.NamedProperties->Contains(ObjectProperty->GetFName()))
	{
		return false;
	}
	if (!CanAssignObject(ObjectProperty, Context.NamedTargetObject))
	{
		return false;
	}

	UObject* CurrentObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
	if (CurrentObject == Context.NamedTargetObject)
	{
		return false;
	}

	if (OwnerObject)
	{
		OwnerObject->Modify();
	}
	ObjectProperty->SetObjectPropertyValue(ValuePtr, Context.NamedTargetObject);
	AddChange(
		OwnerObject,
		PropertyPath,
		CurrentObject ? CurrentObject->GetPathName() : TEXT(""),
		Context.NamedTargetObject->GetPathName(),
		TEXT("hard_object"),
		Context);
	return true;
}

bool TryRepointHardObjectReference(
	FObjectPropertyBase* ObjectProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (!ObjectProperty || !Context.ReplacementMap)
	{
		return false;
	}

	UObject* CurrentObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
	UObject* const* NewObject = CurrentObject ? Context.ReplacementMap->HardObjectMap.Find(CurrentObject) : nullptr;
	if (!NewObject || !CanAssignObject(ObjectProperty, *NewObject))
	{
		return false;
	}

	if (OwnerObject)
	{
		OwnerObject->Modify();
	}
	ObjectProperty->SetObjectPropertyValue(ValuePtr, *NewObject);
	AddChange(OwnerObject, PropertyPath, CurrentObject->GetPathName(), (*NewObject)->GetPathName(), TEXT("hard_object"), Context);
	return true;
}

bool TrySetNamedSoftReference(
	FSoftObjectProperty* SoftObjectProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (!SoftObjectProperty || !Context.NamedProperties || !Context.NamedTargetObject)
	{
		return false;
	}
	if (!Context.NamedProperties->Contains(SoftObjectProperty->GetFName()))
	{
		return false;
	}
	if (SoftObjectProperty->PropertyClass && !Context.NamedTargetObject->IsA(SoftObjectProperty->PropertyClass))
	{
		return false;
	}

	const FSoftObjectPtr CurrentPtr = SoftObjectProperty->GetPropertyValue(ValuePtr);
	const FString OldPath = CurrentPtr.ToSoftObjectPath().ToString();
	const FSoftObjectPath NewPath(Context.NamedTargetObject);
	if (OldPath == NewPath.ToString())
	{
		return false;
	}

	if (OwnerObject)
	{
		OwnerObject->Modify();
	}
	SoftObjectProperty->SetPropertyValue(ValuePtr, FSoftObjectPtr(NewPath));
	AddChange(OwnerObject, PropertyPath, OldPath, NewPath.ToString(), TEXT("soft_object"), Context);
	return true;
}

bool TryRepointSoftObjectReference(
	FSoftObjectProperty* SoftObjectProperty,
	void* ValuePtr,
	UObject* OwnerObject,
	const FString& PropertyPath,
	FVisitContext& Context)
{
	if (!SoftObjectProperty || !Context.ReplacementMap)
	{
		return false;
	}

	const FSoftObjectPtr CurrentPtr = SoftObjectProperty->GetPropertyValue(ValuePtr);
	const FSoftObjectPath CurrentPath = CurrentPtr.ToSoftObjectPath();
	const FString CurrentPathString = CurrentPath.ToString();
	if (CurrentPathString.IsEmpty())
	{
		return false;
	}

	const FSoftObjectPath* NewPath = Context.ReplacementMap->SoftObjectPathMap.Find(CurrentPathString);
	if (!NewPath)
	{
		NewPath = Context.ReplacementMap->SoftObjectPathMap.Find(CurrentPath.GetLongPackageName());
	}
	if (!NewPath)
	{
		return false;
	}

	if (SoftObjectProperty->PropertyClass)
	{
		UObject* NewObject = NewPath->TryLoad();
		if (!NewObject || !NewObject->IsA(SoftObjectProperty->PropertyClass))
		{
			return false;
		}
	}

	if (OwnerObject)
	{
		OwnerObject->Modify();
	}
	SoftObjectProperty->SetPropertyValue(ValuePtr, FSoftObjectPtr(*NewPath));
	AddChange(OwnerObject, PropertyPath, CurrentPathString, NewPath->ToString(), TEXT("soft_object"), Context);
	return true;
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
		if (StructProperty->Struct == FInstancedStruct::StaticStruct())
		{
			FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(ValuePtr);
			if (InstancedStruct && InstancedStruct->IsValid())
			{
				UScriptStruct* InnerStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
				if (InnerStruct)
				{
					VisitStruct(
						InnerStruct,
						InstancedStruct->GetMutableMemory(),
						RootObject,
						OwnerObject,
						PropertyPath,
						Context);
				}
			}
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

	if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		if (TrySetNamedSoftReference(SoftObjectProperty, ValuePtr, OwnerObject, PropertyPath, Context))
		{
			return;
		}
		TryRepointSoftObjectReference(SoftObjectProperty, ValuePtr, OwnerObject, PropertyPath, Context);
		return;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (TrySetNamedObjectReference(ObjectProperty, ValuePtr, OwnerObject, PropertyPath, Context))
		{
			return;
		}
		if (TryRepointHardObjectReference(ObjectProperty, ValuePtr, OwnerObject, PropertyPath, Context))
		{
			return;
		}

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

bool LoadReplacementMap(
	const TSharedPtr<FJsonObject>& Arguments,
	const FString& FieldName,
	const FString& ToolName,
	FReplacementMap& OutReplacementMap,
	FString& OutError)
{
	const TSharedPtr<FJsonObject>* ReplacementObject = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetObjectField(FieldName, ReplacementObject) || !ReplacementObject)
	{
		OutError = FString::Printf(TEXT("%s: %s is required"), *ToolName, *FieldName);
		return false;
	}

	OutReplacementMap.ReplacementPaths = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ReplacementObject)->Values)
	{
		const FString OldPath = Pair.Key.TrimStartAndEnd();
		const FString NewPath = Pair.Value.IsValid() ? Pair.Value->AsString().TrimStartAndEnd() : TEXT("");
		if (OldPath.IsEmpty() || NewPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s: %s entries must be old_path: new_path strings"), *ToolName, *FieldName);
			return false;
		}

		FString LoadError;
		UObject* OldObject = FBridgeAssetModifier::LoadAssetByPath(OldPath, LoadError);
		if (!OldObject)
		{
			OutError = FString::Printf(TEXT("Failed to load replacement source '%s': %s"), *OldPath, *LoadError);
			return false;
		}
		UObject* NewObject = FBridgeAssetModifier::LoadAssetByPath(NewPath, LoadError);
		if (!NewObject)
		{
			OutError = FString::Printf(TEXT("Failed to load replacement target '%s': %s"), *NewPath, *LoadError);
			return false;
		}

		AddReplacementPair(OutReplacementMap, OldPath, OldObject, NewObject);
		OutReplacementMap.ReplacementPaths->SetStringField(OldObject->GetPathName(), NewObject->GetPathName());
	}

	if (OutReplacementMap.HardObjectMap.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s: %s must contain at least one entry"), *ToolName, *FieldName);
		return false;
	}
	return true;
}

int32 RepointObjectReferences(
	UObject* RootObject,
	const FReplacementMap& ReplacementMap,
	TArray<FAssetReferenceChange>& OutChanges)
{
	FVisitContext Context;
	Context.ReplacementMap = &ReplacementMap;
	Context.Changes = &OutChanges;
	VisitObject(RootObject, RootObject, Context);
	return OutChanges.Num();
}

int32 SetNamedObjectReferences(
	UObject* RootObject,
	const TSet<FName>& PropertyNames,
	UObject* TargetObject,
	TArray<FAssetReferenceChange>& OutChanges)
{
	FVisitContext Context;
	Context.NamedProperties = &PropertyNames;
	Context.NamedTargetObject = TargetObject;
	Context.Changes = &OutChanges;
	VisitObject(RootObject, RootObject, Context);
	return OutChanges.Num();
}

TArray<TSharedPtr<FJsonValue>> ChangesToJson(const TArray<FAssetReferenceChange>& Changes)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FAssetReferenceChange& Change : Changes)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("owner"), Change.OwnerPath);
		Json->SetStringField(TEXT("path"), Change.PropertyPath);
		Json->SetStringField(TEXT("old_reference"), Change.OldReference);
		Json->SetStringField(TEXT("new_reference"), Change.NewReference);
		Json->SetStringField(TEXT("kind"), Change.Kind);
		Values.Add(MakeShared<FJsonValueObject>(Json));
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

bool LooksLikePoseSearchDatabase(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	UClass* Class = Object->GetClass();
	while (Class)
	{
		if (Class->GetFName() == TEXT("PoseSearchDatabase") || Class->GetPathName().Contains(TEXT("PoseSearchDatabase")))
		{
			return true;
		}
		Class = Class->GetSuperClass();
	}
	return false;
}
}
