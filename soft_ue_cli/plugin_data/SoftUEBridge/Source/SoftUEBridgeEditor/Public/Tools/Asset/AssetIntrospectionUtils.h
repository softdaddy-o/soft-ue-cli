// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UUserDefinedEnum;
class UUserDefinedStruct;
class FJsonObject;

namespace AssetIntrospectionUtils
{
	TSharedPtr<FJsonObject> InspectUserDefinedEnum(UUserDefinedEnum* UserEnum, const FString& AssetPath);
	TSharedPtr<FJsonObject> InspectUserDefinedStruct(UUserDefinedStruct* UserStruct, const FString& AssetPath);
}
