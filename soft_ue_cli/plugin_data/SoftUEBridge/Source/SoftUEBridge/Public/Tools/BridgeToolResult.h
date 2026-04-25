// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "BridgeToolResult.generated.h"

/** Tool execution result (MCP tools/call content format) */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeToolResult
{
	GENERATED_BODY()

	bool bIsError = false;
	TArray<TSharedPtr<FJsonObject>> Content;

	/** Success result with text */
	static FBridgeToolResult Text(const FString& InText);

	/** Success result with JSON (serialised to text for MCP) */
	static FBridgeToolResult Json(TSharedPtr<FJsonObject> JsonContent);

	/** Error result */
	static FBridgeToolResult Error(const FString& Message);

	/** Serialise to tools/call response JSON */
	TSharedPtr<FJsonObject> ToJson() const;
};
