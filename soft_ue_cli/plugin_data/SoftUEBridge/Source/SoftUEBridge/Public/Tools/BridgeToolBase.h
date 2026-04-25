// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Protocol/BridgeTypes.h"
#include "Tools/BridgeToolResult.h"
#include "BridgeToolBase.generated.h"

/** Context passed to every tool execution */
USTRUCT()
struct SOFTUEBRIDGE_API FBridgeToolContext
{
	GENERATED_BODY()

	FString RequestId;
};

/** Abstract base class for all bridge tools */
UCLASS(Abstract)
class SOFTUEBRIDGE_API UBridgeToolBase : public UObject
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const
		PURE_VIRTUAL(UBridgeToolBase::GetToolName, return TEXT(""););

	virtual FString GetToolDescription() const
		PURE_VIRTUAL(UBridgeToolBase::GetToolDescription, return TEXT(""););

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const { return {}; }
	virtual TArray<FString> GetRequiredParams() const { return {}; }

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context)
		PURE_VIRTUAL(UBridgeToolBase::Execute,
			return FBridgeToolResult::Error(TEXT("Not implemented")););

	FBridgeToolDefinition GetDefinition() const;

protected:
	static bool GetStringArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, FString& Out);
	static FString GetStringArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default = TEXT(""));
	static bool GetBoolArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool& Out);
	static bool GetBoolArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool Default = false);
	static bool GetIntArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32& Out);
	static int32 GetIntArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32 Default = 0);
	static bool GetFloatArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, float& Out);
	static float GetFloatArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, float Default = 0.0f);

	/** Wildcard match: *suffix, prefix*, *substring*, or exact */
	static bool MatchesWildcard(const FString& Name, const FString& Pattern);

	/** Find a world by type: "editor", "pie", or "game". Empty string returns the first available. */
	static UWorld* FindWorldByType(const FString& WorldType);
};


