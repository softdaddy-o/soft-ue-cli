// Copyright soft-ue-expert. All Rights Reserved.

#pragma once
#include "Tools/BridgeToolBase.h"
#include "Misc/OutputDevice.h"
#include "GetLogsTool.generated.h"

/** Thread-safe ring buffer capturing UE log output */
class FBridgeLogCapture : public FOutputDevice
{
public:
	static constexpr int32 MaxLines = 2000;

	static FBridgeLogCapture& Get();

	void Start();
	void Stop();

	/** Retrieve up to N recent lines, optionally filtered by category or text */
	TArray<FString> GetLines(int32 N, const FString& Filter = TEXT(""), const FString& Category = TEXT("")) const;

protected:
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

private:
	TArray<FString> Lines;
	mutable FCriticalSection Lock;
	bool bStarted = false;
};

UCLASS()
class UGetLogsTool : public UBridgeToolBase
{
	GENERATED_BODY()
public:
	virtual FString GetToolName() const override { return TEXT("get-logs"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
