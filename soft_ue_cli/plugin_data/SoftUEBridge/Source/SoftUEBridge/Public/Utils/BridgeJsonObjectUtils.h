// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SoftUE::JsonObjectUtils
{
	inline FString KeyToString(const FJsonObject::FStringType& Key)
	{
		return FString(Key.Len(), *Key);
	}

	inline TSharedPtr<FJsonValue> FindField(const TSharedPtr<FJsonObject>& Object, FStringView FieldName)
	{
		return Object.IsValid() ? Object->TryGetField(FieldName) : nullptr;
	}

	inline TSharedPtr<FJsonValue> FindField(const FJsonObject& Object, FStringView FieldName)
	{
		return Object.TryGetField(FieldName);
	}

	inline bool HasField(const TSharedPtr<FJsonObject>& Object, FStringView FieldName)
	{
		return Object.IsValid() && Object->HasField(FieldName);
	}

	inline void GetFieldNames(const TSharedPtr<FJsonObject>& Object, TArray<FString>& OutFieldNames)
	{
		OutFieldNames.Reset();
		if (!Object.IsValid())
		{
			return;
		}

		for (const auto& Pair : Object->Values)
		{
			OutFieldNames.Add(KeyToString(Pair.Key));
		}
	}
}
