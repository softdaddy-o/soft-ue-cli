// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/AssetIntrospectionUtils.h"

#include "EdGraph/EdGraphPin.h"
#include "Engine/UserDefinedEnum.h"
#include "Internationalization/Text.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	FString GetPropertyKind(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("Unknown");
		}

		if (Property->IsA<FBoolProperty>()) return TEXT("Bool");
		if (Property->IsA<FEnumProperty>() || (CastField<const FByteProperty>(Property) && CastField<const FByteProperty>(Property)->Enum)) return TEXT("Enum");
		if (Property->IsA<FStructProperty>()) return TEXT("Struct");
		if (Property->IsA<FObjectProperty>()) return TEXT("Object");
		if (Property->IsA<FClassProperty>()) return TEXT("Class");
		if (Property->IsA<FArrayProperty>()) return TEXT("Array");
		if (Property->IsA<FSetProperty>()) return TEXT("Set");
		if (Property->IsA<FMapProperty>()) return TEXT("Map");
		if (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>()) return TEXT("Float");
		if (Property->IsA<FIntProperty>() || Property->IsA<FInt64Property>() || Property->IsA<FByteProperty>()) return TEXT("Int");
		if (Property->IsA<FNameProperty>()) return TEXT("Name");
		if (Property->IsA<FStrProperty>()) return TEXT("String");
		if (Property->IsA<FTextProperty>()) return TEXT("Text");
		return Property->GetClass()->GetName();
	}

	FString GetPropertyPinType(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("unknown");
		}

		if (Property->IsA<FBoolProperty>()) return TEXT("bool");
		if (Property->IsA<FEnumProperty>()) return TEXT("byte");
		if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
		{
			return ByteProperty->Enum ? TEXT("byte") : TEXT("byte");
		}
		if (Property->IsA<FIntProperty>()) return TEXT("int");
		if (Property->IsA<FInt64Property>()) return TEXT("int64");
		if (Property->IsA<FFloatProperty>()) return TEXT("float");
		if (Property->IsA<FDoubleProperty>()) return TEXT("real");
		if (Property->IsA<FNameProperty>()) return TEXT("name");
		if (Property->IsA<FStrProperty>()) return TEXT("string");
		if (Property->IsA<FTextProperty>()) return TEXT("text");
		if (Property->IsA<FStructProperty>()) return TEXT("struct");
		if (Property->IsA<FObjectProperty>()) return TEXT("object");
		if (Property->IsA<FClassProperty>()) return TEXT("class");
		if (Property->IsA<FSoftObjectProperty>()) return TEXT("softobject");
		if (Property->IsA<FSoftClassProperty>()) return TEXT("softclass");
		return Property->GetClass()->GetName();
	}

	FString GetPropertySubType(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("");
		}

		if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetName() : TEXT("");
		}

		if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
		{
			return ByteProperty->Enum ? ByteProperty->Enum->GetName() : TEXT("");
		}

		if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
		{
			return StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("");
		}

		if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(Property))
		{
			return ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetName() : TEXT("");
		}

		if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(Property))
		{
			return ClassProperty->MetaClass ? ClassProperty->MetaClass->GetName() : TEXT("");
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
		{
			return SoftObjectProperty->PropertyClass ? SoftObjectProperty->PropertyClass->GetName() : TEXT("");
		}

		if (const FSoftClassProperty* SoftClassProperty = CastField<const FSoftClassProperty>(Property))
		{
			return SoftClassProperty->MetaClass ? SoftClassProperty->MetaClass->GetName() : TEXT("");
		}

		if (Property->IsA<FDoubleProperty>()) return TEXT("double");
		if (Property->IsA<FFloatProperty>()) return TEXT("float");

		return TEXT("");
	}

	FString ExportStructDefaultValue(const UUserDefinedStruct* UserStruct, const FProperty* Property)
	{
		if (!UserStruct || !Property)
		{
			return TEXT("");
		}

		const uint8* DefaultInstance = UserStruct->GetDefaultInstance();
		if (!DefaultInstance)
		{
			return TEXT("");
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultInstance);
		if (!ValuePtr)
		{
			return TEXT("");
		}

		FString Exported;
		Property->ExportText_Direct(Exported, ValuePtr, ValuePtr, UserStruct, PPF_None);
		return Exported;
	}
}

TSharedPtr<FJsonObject> AssetIntrospectionUtils::InspectUserDefinedEnum(UUserDefinedEnum* UserEnum, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), TEXT("UserDefinedEnum"));
	Result->SetStringField(TEXT("name"), UserEnum ? UserEnum->GetName() : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> Enumerators;

	if (UserEnum)
	{
		const int32 NumEnums = UserEnum->NumEnums();
		for (int32 Index = 0; Index < NumEnums; ++Index)
		{
			const FString InternalName = UserEnum->GetNameStringByIndex(Index);
			if (InternalName.EndsWith(TEXT("_MAX")))
			{
				continue;
			}

			TSharedPtr<FJsonObject> Enumerator = MakeShareable(new FJsonObject);
			Enumerator->SetNumberField(TEXT("index"), Index);
			Enumerator->SetStringField(TEXT("internal_name"), InternalName);
			Enumerator->SetStringField(TEXT("authored_name"), UserEnum->GetAuthoredNameStringByIndex(Index));
			Enumerator->SetStringField(TEXT("display_name"), UserEnum->GetDisplayNameTextByIndex(Index).ToString());
			Enumerator->SetStringField(TEXT("tooltip"), UserEnum->GetMetaData(TEXT("Tooltip"), Index));
			Enumerator->SetNumberField(TEXT("numeric_value"), static_cast<double>(UserEnum->GetValueByIndex(Index)));
			Enumerators.Add(MakeShareable(new FJsonValueObject(Enumerator)));
		}
	}

	Result->SetArrayField(TEXT("enumerators"), Enumerators);
	Result->SetNumberField(TEXT("count"), Enumerators.Num());
	return Result;
}

TSharedPtr<FJsonObject> AssetIntrospectionUtils::InspectUserDefinedStruct(UUserDefinedStruct* UserStruct, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), TEXT("UserDefinedStruct"));
	Result->SetStringField(TEXT("name"), UserStruct ? UserStruct->GetName() : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> Members;

	if (UserStruct)
	{
		const TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(UserStruct);

		for (const FStructVariableDescription& Variable : Variables)
		{
			FProperty* Property = FStructureEditorUtils::GetPropertyByGuid(UserStruct, Variable.VarGuid);
			TSharedPtr<FJsonObject> Member = MakeShareable(new FJsonObject);

			const FString FriendlyName = Property
				? UserStruct->GetAuthoredNameForField(Property)
				: (!Variable.FriendlyName.IsEmpty() ? Variable.FriendlyName : Variable.VarName.ToString());
			const FString DefaultValue = !Variable.CurrentDefaultValue.IsEmpty()
				? Variable.CurrentDefaultValue
				: (!Variable.DefaultValue.IsEmpty() ? Variable.DefaultValue : ExportStructDefaultValue(UserStruct, Property));

			Member->SetStringField(TEXT("name"), FriendlyName);
			Member->SetStringField(TEXT("raw_name"), Property ? Property->GetName() : Variable.VarName.ToString());
			Member->SetStringField(TEXT("type"), GetPropertyPinType(Property));

			const FString SubType = GetPropertySubType(Property);
			if (!SubType.IsEmpty())
			{
				Member->SetStringField(TEXT("sub_type"), SubType);
			}

			Member->SetStringField(TEXT("category"), GetPropertyKind(Property));
			Member->SetStringField(TEXT("default_value"), DefaultValue);

			if (Property)
			{
				Member->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
				Member->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
			}

			Member->SetStringField(TEXT("tooltip"), Variable.ToolTip);
			Member->SetBoolField(TEXT("is_array"), Variable.ContainerType == EPinContainerType::Array);
			Member->SetBoolField(TEXT("is_set"), Variable.ContainerType == EPinContainerType::Set);
			Member->SetBoolField(TEXT("is_map"), Variable.ContainerType == EPinContainerType::Map);

			TSharedPtr<FJsonObject> Metadata = MakeShareable(new FJsonObject);
			for (const TPair<FName, FString>& Pair : Variable.MetaData)
			{
				Metadata->SetStringField(Pair.Key.ToString(), Pair.Value);
			}
			Member->SetObjectField(TEXT("metadata"), Metadata);

			Members.Add(MakeShareable(new FJsonValueObject(Member)));
		}
	}

	Result->SetArrayField(TEXT("members"), Members);
	Result->SetNumberField(TEXT("count"), Members.Num());
	return Result;
}
