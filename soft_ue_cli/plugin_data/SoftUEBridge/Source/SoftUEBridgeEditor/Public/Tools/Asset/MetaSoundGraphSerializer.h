// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMetasoundFrontendDocument;
class FJsonObject;

/**
 * Pure serialization of a MetaSound Frontend document to JSON.
 */
namespace MetaSoundGraphSerializer
{
	TSharedPtr<FJsonObject> SerializeDocument(const FMetasoundFrontendDocument& Document, const FString& AssetType);
}
