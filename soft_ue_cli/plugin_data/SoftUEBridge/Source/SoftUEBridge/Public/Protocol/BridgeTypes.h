// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "BridgeTypes.generated.h"

/** JSON-RPC version */
#define JSONRPC_VERSION TEXT("2.0")

/** JSON-RPC error codes */
namespace EBridgeErrorCode
{
	constexpr int32 ParseError     = -32700;
	constexpr int32 InvalidRequest = -32600;
	constexpr int32 MethodNotFound = -32601;
	constexpr int32 InvalidParams  = -32602;
	constexpr int32 InternalError  = -32603;
	constexpr int32 ServerError    = -32000;
}

/** MCP request methods used for dispatch */
UENUM()
enum class EBridgeMethod : uint8
{
	Initialize,
	Initialized,
	Shutdown,
	ToolsList,
	ToolsCall,
	Unknown
};

/** Tool content type */
UENUM()
enum class EBridgeContentType : uint8
{
	Text,
	Image
};

/** JSON-RPC request */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeRequest
{
	GENERATED_BODY()

	UPROPERTY() FString JsonRpc = JSONRPC_VERSION;
	UPROPERTY() FString Id;
	UPROPERTY() FString Method;
	TSharedPtr<FJsonObject> Params;
	EBridgeMethod ParsedMethod = EBridgeMethod::Unknown;

	bool IsNotification() const { return Id.IsEmpty(); }

	static TOptional<FBridgeRequest> FromJsonString(const FString& JsonString);
};

/** JSON-RPC response */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeResponse
{
	GENERATED_BODY()

	UPROPERTY() FString JsonRpc = JSONRPC_VERSION;
	UPROPERTY() FString Id;
	TSharedPtr<FJsonObject> Result;
	TSharedPtr<FJsonObject> ErrorData;

	static FBridgeResponse Success(const FString& InId, TSharedPtr<FJsonObject> InResult);
	static FBridgeResponse Error(const FString& InId, int32 Code, const FString& Message);

	FString ToJsonString() const;
};

/** Tool input schema property */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeSchemaProperty
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString Description;
	UPROPERTY() bool bRequired = false;
	UPROPERTY() TArray<FString> Enum;
	UPROPERTY() FString ItemsType;

	TSharedPtr<FJsonObject> ToJson() const;
};

/** Tool definition for tools/list */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeToolDefinition
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FString Description;
	TMap<FString, FBridgeSchemaProperty> InputSchema;
	UPROPERTY() TArray<FString> Required;

	TSharedPtr<FJsonObject> ToJson() const;
};
