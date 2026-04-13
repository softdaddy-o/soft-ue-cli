// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BridgePythonCallHelper.generated.h"

/**
 * Exposes bridge tool dispatch to embedded Python scripts.
 */
UCLASS(BlueprintType)
class SOFTUEBRIDGEEDITOR_API UBridgePythonCallHelper : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="SoftUEBridge|Testing")
	static FString CallTool(const FString& ToolName, const FString& ArgsJson);
};
