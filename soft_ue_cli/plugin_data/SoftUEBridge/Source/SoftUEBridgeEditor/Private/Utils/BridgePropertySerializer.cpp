// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgePropertySerializer.h"
#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "UObject/EnumProperty.h"

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializePropertyValue(
	FProperty* Property,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth)
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

	// Boolean
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShareable(new FJsonValueBoolean(BoolProp->GetPropertyValue(ValuePtr)));
	}

	// Numeric (int, float, double, etc.)
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (NumericProp->IsFloatingPoint())
		{
			return MakeShareable(new FJsonValueNumber(NumericProp->GetFloatingPointPropertyValue(ValuePtr)));
		}
		else if (NumericProp->IsInteger())
		{
			return MakeShareable(new FJsonValueNumber(static_cast<double>(NumericProp->GetSignedIntPropertyValue(ValuePtr))));
		}
	}

	// String
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(StrProp->GetPropertyValue(ValuePtr)));
	}

	// Name
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(NameProp->GetPropertyValue(ValuePtr).ToString()));
	}

	// Text
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(TextProp->GetPropertyValue(ValuePtr).ToString()));
	}

	// Enum
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
		FString EnumName = EnumProp->GetEnum()->GetNameStringByValue(EnumValue);
		return MakeShareable(new FJsonValueString(EnumName));
	}

	// Byte enum
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			uint8 ByteValue = ByteProp->GetPropertyValue(ValuePtr);
			FString EnumName = ByteProp->Enum->GetNameStringByValue(ByteValue);
			return MakeShareable(new FJsonValueString(EnumName));
		}
		else
		{
			return MakeShareable(new FJsonValueNumber(ByteProp->GetPropertyValue(ValuePtr)));
		}
	}

	// Struct
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return SerializeStructProperty(StructProp, Container, Owner, Depth, MaxDepth);
	}

	// Array
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return SerializeArrayProperty(ArrayProp, Container, Owner, Depth, MaxDepth);
	}

	// Map
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return SerializeMapProperty(MapProp, Container, Owner, Depth, MaxDepth);
	}

	// Set
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return SerializeSetProperty(SetProp, Container, Owner, Depth, MaxDepth);
	}

	// Object reference
	if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
	{
		return SerializeObjectProperty(ObjectProp, Container, Depth, MaxDepth);
	}

	// Soft object reference
	if (FSoftObjectProperty* SoftObjectProp = CastField<FSoftObjectProperty>(Property))
	{
		FSoftObjectPtr SoftPtr = SoftObjectProp->GetPropertyValue(ValuePtr);
		return MakeShareable(new FJsonValueString(SoftPtr.ToString()));
	}

	// Class reference
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue(ValuePtr));
		if (ClassValue)
		{
			return MakeShareable(new FJsonValueString(ClassValue->GetPathName()));
		}
		return MakeShareable(new FJsonValueNull());
	}

	// Fallback: use ExportText
	FString ExportedValue;
	Property->ExportText_Direct(ExportedValue, ValuePtr, ValuePtr, Owner, PPF_None);
	return MakeShareable(new FJsonValueString(ExportedValue));
}

TSharedPtr<FJsonObject> FBridgePropertySerializer::SerializeProperty(
	FProperty* Property,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth,
	bool bIncludeMetadata)
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropJson = MakeShareable(new FJsonObject);
	PropJson->SetStringField(TEXT("name"), Property->GetName());
	PropJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	if (bIncludeMetadata)
	{
		FString Category = Property->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			PropJson->SetStringField(TEXT("category"), Category);
		}

		FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty())
		{
			PropJson->SetStringField(TEXT("tooltip"), Tooltip);
		}

		// Property flags
		if (Property->HasAnyPropertyFlags(CPF_Edit))
		{
			PropJson->SetBoolField(TEXT("editable"), true);
		}
		if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			PropJson->SetBoolField(TEXT("blueprint_visible"), true);
		}
	}

	TSharedPtr<FJsonValue> Value = SerializePropertyValue(Property, Container, Owner, Depth, MaxDepth);
	if (Value.IsValid())
	{
		PropJson->SetField(TEXT("value"), Value);
	}

	return PropJson;
}

TSharedPtr<FJsonObject> FBridgePropertySerializer::SerializeUObjectProperties(
	UObject* Object,
	int32 Depth,
	int32 MaxDepth,
	EPropertyFlags RequiredFlags,
	EPropertyFlags ExcludeFlags)
{
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;

		// Check required flags
		if (RequiredFlags != CPF_None && !Property->HasAllPropertyFlags(RequiredFlags))
		{
			continue;
		}

		// Check exclude flags
		if (ExcludeFlags != CPF_None && Property->HasAnyPropertyFlags(ExcludeFlags))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PropJson = SerializeProperty(Property, Object, Object, Depth, MaxDepth);
		if (PropJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return Result;
}

bool FBridgePropertySerializer::DeserializePropertyValue(
	FProperty* Property,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	if (!Property || !Container || !Value.IsValid())
	{
		OutError = TEXT("Invalid parameters");
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

	// Boolean
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool BoolValue = false;
		if (!Value->TryGetBool(BoolValue))
		{
			OutError = TEXT("Expected boolean value");
			return false;
		}
		BoolProp->SetPropertyValue(ValuePtr, BoolValue);
		return true;
	}

	// Numeric
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (NumericProp->IsFloatingPoint())
		{
			double NumValue = 0.0;
			if (!Value->TryGetNumber(NumValue))
			{
				OutError = TEXT("Expected numeric value");
				return false;
			}
			NumericProp->SetFloatingPointPropertyValue(ValuePtr, NumValue);
		}
		else
		{
			int64 NumValue = 0;
			if (!Value->TryGetNumber(NumValue))
			{
				OutError = TEXT("Expected integer value");
				return false;
			}
			NumericProp->SetIntPropertyValue(ValuePtr, NumValue);
		}
		return true;
	}

	// String
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString StrValue;
		if (!Value->TryGetString(StrValue))
		{
			OutError = TEXT("Expected string value");
			return false;
		}
		StrProp->SetPropertyValue(ValuePtr, StrValue);
		return true;
	}

	// Name
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString StrValue;
		if (!Value->TryGetString(StrValue))
		{
			OutError = TEXT("Expected string value for FName");
			return false;
		}
		NameProp->SetPropertyValue(ValuePtr, FName(*StrValue));
		return true;
	}

	// Text
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString StrValue;
		if (!Value->TryGetString(StrValue))
		{
			OutError = TEXT("Expected string value for FText");
			return false;
		}
		TextProp->SetPropertyValue(ValuePtr, FText::FromString(StrValue));
		return true;
	}

	// Enum
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString StrValue;
		int64 IntValue;

		if (Value->TryGetString(StrValue))
		{
			int64 EnumValue = EnumProp->GetEnum()->GetValueByNameString(StrValue);
			if (EnumValue == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Invalid enum value: %s"), *StrValue);
				return false;
			}
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
		}
		else if (Value->TryGetNumber(IntValue))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, IntValue);
		}
		else
		{
			OutError = TEXT("Expected string or integer for enum");
			return false;
		}
		return true;
	}

	// Struct
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* JsonObject = nullptr;
		if (Value->TryGetObject(JsonObject) && JsonObject->IsValid())
		{
			if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject->ToSharedRef(), StructProp->Struct, ValuePtr))
			{
				OutError = TEXT("Failed to convert JSON to struct");
				return false;
			}
			return true;
		}

		// Try array for vector types
		const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
		if (Value->TryGetArray(JsonArray))
		{
			// FVector
			if (StructProp->Struct == TBaseStructure<FVector>::Get() && JsonArray->Num() >= 3)
			{
				FVector* Vec = static_cast<FVector*>(ValuePtr);
				Vec->X = (*JsonArray)[0]->AsNumber();
				Vec->Y = (*JsonArray)[1]->AsNumber();
				Vec->Z = (*JsonArray)[2]->AsNumber();
				return true;
			}
			// FRotator
			if (StructProp->Struct == TBaseStructure<FRotator>::Get() && JsonArray->Num() >= 3)
			{
				FRotator* Rot = static_cast<FRotator*>(ValuePtr);
				Rot->Pitch = (*JsonArray)[0]->AsNumber();
				Rot->Yaw = (*JsonArray)[1]->AsNumber();
				Rot->Roll = (*JsonArray)[2]->AsNumber();
				return true;
			}
			// FLinearColor
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get() && JsonArray->Num() >= 3)
			{
				FLinearColor* Color = static_cast<FLinearColor*>(ValuePtr);
				Color->R = (*JsonArray)[0]->AsNumber();
				Color->G = (*JsonArray)[1]->AsNumber();
				Color->B = (*JsonArray)[2]->AsNumber();
				Color->A = JsonArray->Num() >= 4 ? (*JsonArray)[3]->AsNumber() : 1.0f;
				return true;
			}
			// FVector2D
			if (StructProp->Struct == TBaseStructure<FVector2D>::Get() && JsonArray->Num() >= 2)
			{
				FVector2D* Vec2D = static_cast<FVector2D*>(ValuePtr);
				Vec2D->X = (*JsonArray)[0]->AsNumber();
				Vec2D->Y = (*JsonArray)[1]->AsNumber();
				return true;
			}
		}

		OutError = TEXT("Expected object or array value for struct");
		return false;
	}

	// Array
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return DeserializeArrayProperty(ArrayProp, Container, Value, OutError);
	}

	// Map
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return DeserializeMapProperty(MapProp, Container, Value, OutError);
	}

	// Set
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return DeserializeSetProperty(SetProp, Container, Value, OutError);
	}

	// Object reference
	if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
	{
		return DeserializeObjectProperty(ObjectProp, Container, Value, OutError);
	}

	// Soft object reference
	if (FSoftObjectProperty* SoftObjectProp = CastField<FSoftObjectProperty>(Property))
	{
		FString StrValue;
		if (!Value->TryGetString(StrValue))
		{
			OutError = TEXT("Expected string path for soft object reference");
			return false;
		}
		FSoftObjectPath SoftPath(StrValue);
		SoftObjectProp->SetPropertyValue(ValuePtr, FSoftObjectPtr(SoftPath));
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported property type: %s"), *Property->GetClass()->GetName());
	return false;
}

FString FBridgePropertySerializer::GetPropertyTypeString(FProperty* Property)
{
	if (!Property) return TEXT("unknown");

	if (Property->IsA<FBoolProperty>()) return TEXT("bool");
	if (Property->IsA<FIntProperty>()) return TEXT("int32");
	if (Property->IsA<FInt64Property>()) return TEXT("int64");
	if (Property->IsA<FFloatProperty>()) return TEXT("float");
	if (Property->IsA<FDoubleProperty>()) return TEXT("double");
	if (Property->IsA<FStrProperty>()) return TEXT("FString");
	if (Property->IsA<FNameProperty>()) return TEXT("FName");
	if (Property->IsA<FTextProperty>()) return TEXT("FText");

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			return ByteProp->Enum->GetName();
		}
		return TEXT("uint8");
	}

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		return EnumProp->GetEnum()->GetName();
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return StructProp->Struct ? StructProp->Struct->GetName() : TEXT("struct");
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return ObjProp->PropertyClass ? FString::Printf(TEXT("TObjectPtr<%s>"), *ObjProp->PropertyClass->GetName()) : TEXT("UObject*");
	}

	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		return SoftObjProp->PropertyClass ? FString::Printf(TEXT("TSoftObjectPtr<%s>"), *SoftObjProp->PropertyClass->GetName()) : TEXT("TSoftObjectPtr<>");
	}

	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		return ClassProp->MetaClass ? FString::Printf(TEXT("TSubclassOf<%s>"), *ClassProp->MetaClass->GetName()) : TEXT("UClass*");
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProp->Inner));
	}

	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return FString::Printf(TEXT("TMap<%s, %s>"),
			*GetPropertyTypeString(MapProp->KeyProp),
			*GetPropertyTypeString(MapProp->ValueProp));
	}

	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return FString::Printf(TEXT("TSet<%s>"), *GetPropertyTypeString(SetProp->ElementProp));
	}

	return Property->GetClass()->GetName();
}

UClass* FBridgePropertySerializer::ResolveClass(const FString& ClassName, FString& OutError)
{
	if (ClassName.IsEmpty())
	{
		OutError = TEXT("Class name is empty");
		return nullptr;
	}

	UClass* ResolvedClass = nullptr;

	// Try as Blueprint class path (e.g., "/Game/BP_MyClass.BP_MyClass_C")
	if (ClassName.StartsWith(TEXT("/")))
	{
		ResolvedClass = LoadClass<UObject>(nullptr, *ClassName);
		if (ResolvedClass)
		{
			return ResolvedClass;
		}

		// Maybe it's an object path, try loading
		UObject* LoadedObj = StaticLoadObject(UClass::StaticClass(), nullptr, *ClassName);
		if (UClass* LoadedClass = Cast<UClass>(LoadedObj))
		{
			return LoadedClass;
		}

		// Try appending _C for Blueprint classes
		if (!ClassName.EndsWith(TEXT("_C")))
		{
			FString BlueprintClassName = ClassName + TEXT("_C");
			ResolvedClass = LoadClass<UObject>(nullptr, *BlueprintClassName);
			if (ResolvedClass)
			{
				return ResolvedClass;
			}
		}
	}

	// Try as short class name (e.g., "Actor", "MaterialExpressionAdd")
	ResolvedClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (ResolvedClass)
	{
		return ResolvedClass;
	}

	// Try with common prefixes
	TArray<FString> Prefixes = { TEXT("U"), TEXT("A"), TEXT("F") };
	for (const FString& Prefix : Prefixes)
	{
		FString PrefixedName = Prefix + ClassName;
		ResolvedClass = FindFirstObject<UClass>(*PrefixedName, EFindFirstObjectOptions::ExactClass);
		if (ResolvedClass)
		{
			return ResolvedClass;
		}
	}

	OutError = FString::Printf(TEXT("Class not found: %s"), *ClassName);
	return nullptr;
}

// === Private Helpers ===

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializeArrayProperty(
	FArrayProperty* ArrayProp,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth)
{
	const void* ValuePtr = ArrayProp->ContainerPtrToValuePtr<void>(Container);
	FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);

	TArray<TSharedPtr<FJsonValue>> JsonArray;

	if (Depth >= MaxDepth)
	{
		// Return count only at max depth
		return MakeShareable(new FJsonValueString(FString::Printf(TEXT("[Array: %d items]"), ArrayHelper.Num())));
	}

	for (int32 i = 0; i < ArrayHelper.Num(); i++)
	{
		const void* ElementPtr = ArrayHelper.GetRawPtr(i);

		// Create a fake container for the inner property
		TSharedPtr<FJsonValue> ElementValue;
		if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
		{
			TSharedPtr<FJsonObject> StructJson = MakeShareable(new FJsonObject);
			if (FJsonObjectConverter::UStructToJsonObject(InnerStruct->Struct, ElementPtr, StructJson.ToSharedRef(), 0, 0))
			{
				ElementValue = MakeShareable(new FJsonValueObject(StructJson));
			}
		}
		else
		{
			// For simple types, serialize directly
			FString ExportedValue;
			ArrayProp->Inner->ExportText_Direct(ExportedValue, ElementPtr, ElementPtr, Owner, PPF_None);
			ElementValue = MakeShareable(new FJsonValueString(ExportedValue));
		}

		if (ElementValue.IsValid())
		{
			JsonArray.Add(ElementValue);
		}
	}

	return MakeShareable(new FJsonValueArray(JsonArray));
}

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializeMapProperty(
	FMapProperty* MapProp,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth)
{
	const void* ValuePtr = MapProp->ContainerPtrToValuePtr<void>(Container);
	FScriptMapHelper MapHelper(MapProp, ValuePtr);

	if (Depth >= MaxDepth)
	{
		return MakeShareable(new FJsonValueString(FString::Printf(TEXT("[Map: %d items]"), MapHelper.Num())));
	}

	TSharedPtr<FJsonObject> MapJson = MakeShareable(new FJsonObject);

	for (int32 i = 0; i < MapHelper.GetMaxIndex(); i++)
	{
		if (!MapHelper.IsValidIndex(i))
		{
			continue;
		}

		// Get key as string
		FString KeyStr;
		const void* KeyPtr = MapHelper.GetKeyPtr(i);
		MapProp->KeyProp->ExportText_Direct(KeyStr, KeyPtr, KeyPtr, Owner, PPF_None);

		// Get value
		const void* ValPtr = MapHelper.GetValuePtr(i);
		FString ValStr;
		MapProp->ValueProp->ExportText_Direct(ValStr, ValPtr, ValPtr, Owner, PPF_None);

		MapJson->SetStringField(KeyStr, ValStr);
	}

	return MakeShareable(new FJsonValueObject(MapJson));
}

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializeSetProperty(
	FSetProperty* SetProp,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth)
{
	const void* ValuePtr = SetProp->ContainerPtrToValuePtr<void>(Container);
	FScriptSetHelper SetHelper(SetProp, ValuePtr);

	if (Depth >= MaxDepth)
	{
		return MakeShareable(new FJsonValueString(FString::Printf(TEXT("[Set: %d items]"), SetHelper.Num())));
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;

	for (int32 i = 0; i < SetHelper.GetMaxIndex(); i++)
	{
		if (!SetHelper.IsValidIndex(i))
		{
			continue;
		}

		const void* ElementPtr = SetHelper.GetElementPtr(i);
		FString ElementStr;
		SetProp->ElementProp->ExportText_Direct(ElementStr, ElementPtr, ElementPtr, Owner, PPF_None);
		JsonArray.Add(MakeShareable(new FJsonValueString(ElementStr)));
	}

	return MakeShareable(new FJsonValueArray(JsonArray));
}

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializeStructProperty(
	FStructProperty* StructProp,
	const void* Container,
	UObject* Owner,
	int32 Depth,
	int32 MaxDepth)
{
	const void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Container);

	// Common structs as arrays for convenience
	if (StructProp->Struct == TBaseStructure<FVector>::Get())
	{
		const FVector* Vec = static_cast<const FVector*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShareable(new FJsonValueNumber(Vec->X)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Vec->Y)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Vec->Z)));
		return MakeShareable(new FJsonValueArray(Arr));
	}

	if (StructProp->Struct == TBaseStructure<FRotator>::Get())
	{
		const FRotator* Rot = static_cast<const FRotator*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShareable(new FJsonValueNumber(Rot->Pitch)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Rot->Yaw)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Rot->Roll)));
		return MakeShareable(new FJsonValueArray(Arr));
	}

	if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
	{
		const FLinearColor* Color = static_cast<const FLinearColor*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShareable(new FJsonValueNumber(Color->R)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Color->G)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Color->B)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Color->A)));
		return MakeShareable(new FJsonValueArray(Arr));
	}

	if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
	{
		const FVector2D* Vec2D = static_cast<const FVector2D*>(ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShareable(new FJsonValueNumber(Vec2D->X)));
		Arr.Add(MakeShareable(new FJsonValueNumber(Vec2D->Y)));
		return MakeShareable(new FJsonValueArray(Arr));
	}

	// Use FJsonObjectConverter for complex structs
	TSharedPtr<FJsonObject> StructJson = MakeShareable(new FJsonObject);
	if (FJsonObjectConverter::UStructToJsonObject(StructProp->Struct, ValuePtr, StructJson.ToSharedRef(), 0, 0))
	{
		return MakeShareable(new FJsonValueObject(StructJson));
	}

	// Fallback to ExportText
	FString ExportedValue;
	StructProp->ExportText_Direct(ExportedValue, ValuePtr, ValuePtr, Owner, PPF_None);
	return MakeShareable(new FJsonValueString(ExportedValue));
}

TSharedPtr<FJsonValue> FBridgePropertySerializer::SerializeObjectProperty(
	FObjectProperty* ObjectProp,
	const void* Container,
	int32 Depth,
	int32 MaxDepth)
{
	const void* ValuePtr = ObjectProp->ContainerPtrToValuePtr<void>(Container);
	UObject* Object = ObjectProp->GetObjectPropertyValue(ValuePtr);

	if (!Object)
	{
		return MakeShareable(new FJsonValueNull());
	}

	// Return path for object references
	return MakeShareable(new FJsonValueString(Object->GetPathName()));
}

bool FBridgePropertySerializer::DeserializeArrayProperty(
	FArrayProperty* ArrayProp,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!Value->TryGetArray(JsonArray))
	{
		OutError = TEXT("Expected array value");
		return false;
	}

	void* ValuePtr = ArrayProp->ContainerPtrToValuePtr<void>(Container);
	FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
	ArrayHelper.EmptyValues();

	for (const TSharedPtr<FJsonValue>& ElementValue : *JsonArray)
	{
		int32 NewIndex = ArrayHelper.AddValue();
		void* ElementPtr = ArrayHelper.GetRawPtr(NewIndex);

		if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
		{
			const TSharedPtr<FJsonObject>* ElementObject = nullptr;
			if (ElementValue->TryGetObject(ElementObject) && ElementObject->IsValid())
			{
				if (!FJsonObjectConverter::JsonObjectToUStruct(ElementObject->ToSharedRef(), InnerStruct->Struct, ElementPtr))
				{
					OutError = FString::Printf(TEXT("Failed to deserialize array element %d"), NewIndex);
					return false;
				}
			}
		}
		else
		{
			// For simple types, use ImportText
			FString ElementStr = ElementValue->AsString();
			ArrayProp->Inner->ImportText_Direct(*ElementStr, ElementPtr, nullptr, PPF_None);
		}
	}

	return true;
}

bool FBridgePropertySerializer::DeserializeMapProperty(
	FMapProperty* MapProp,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	const TSharedPtr<FJsonObject>* JsonObject = nullptr;
	if (!Value->TryGetObject(JsonObject) || !JsonObject->IsValid())
	{
		OutError = TEXT("Expected object value for TMap");
		return false;
	}

	void* ValuePtr = MapProp->ContainerPtrToValuePtr<void>(Container);
	FScriptMapHelper MapHelper(MapProp, ValuePtr);
	MapHelper.EmptyValues();

	for (const auto& Pair : (*JsonObject)->Values)
	{
		// Add a new pair
		int32 NewIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();

		// Set key
		void* KeyPtr = MapHelper.GetKeyPtr(NewIndex);
		MapProp->KeyProp->ImportText_Direct(*Pair.Key, KeyPtr, nullptr, PPF_None);

		// Set value
		void* ValPtr = MapHelper.GetValuePtr(NewIndex);
		FString ValueStr = Pair.Value->AsString();
		MapProp->ValueProp->ImportText_Direct(*ValueStr, ValPtr, nullptr, PPF_None);
	}

	MapHelper.Rehash();
	return true;
}

bool FBridgePropertySerializer::DeserializeSetProperty(
	FSetProperty* SetProp,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!Value->TryGetArray(JsonArray))
	{
		OutError = TEXT("Expected array value for TSet");
		return false;
	}

	void* ValuePtr = SetProp->ContainerPtrToValuePtr<void>(Container);
	FScriptSetHelper SetHelper(SetProp, ValuePtr);
	SetHelper.EmptyElements();

	for (const TSharedPtr<FJsonValue>& ElementValue : *JsonArray)
	{
		int32 NewIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
		void* ElementPtr = SetHelper.GetElementPtr(NewIndex);

		FString ElementStr = ElementValue->AsString();
		SetProp->ElementProp->ImportText_Direct(*ElementStr, ElementPtr, nullptr, PPF_None);
	}

	SetHelper.Rehash();
	return true;
}

bool FBridgePropertySerializer::DeserializeObjectProperty(
	FObjectProperty* ObjectProp,
	void* Container,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	void* ValuePtr = ObjectProp->ContainerPtrToValuePtr<void>(Container);

	// Handle null
	if (Value->IsNull())
	{
		ObjectProp->SetObjectPropertyValue(ValuePtr, nullptr);
		return true;
	}

	// Expect a path string
	FString PathStr;
	if (!Value->TryGetString(PathStr))
	{
		OutError = TEXT("Expected string path for object reference");
		return false;
	}

	if (PathStr.IsEmpty())
	{
		ObjectProp->SetObjectPropertyValue(ValuePtr, nullptr);
		return true;
	}

	// Try to load the object
	UObject* LoadedObject = StaticLoadObject(ObjectProp->PropertyClass, nullptr, *PathStr);
	if (!LoadedObject)
	{
		// Try FindObject as fallback
		LoadedObject = FindObject<UObject>(nullptr, *PathStr);
	}

	if (!LoadedObject)
	{
		OutError = FString::Printf(TEXT("Object not found: %s"), *PathStr);
		return false;
	}

	ObjectProp->SetObjectPropertyValue(ValuePtr, LoadedObject);
	return true;
}
