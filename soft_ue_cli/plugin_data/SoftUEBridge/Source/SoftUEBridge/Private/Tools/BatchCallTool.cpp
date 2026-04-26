// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/BatchCallTool.h"
#include "SoftUEBridgeModule.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/BridgeToolRegistry.h"

namespace
{
	TSharedPtr<FJsonObject> DecodeResultPayload(const FBridgeToolResult& ToolResult)
	{
		const TSharedPtr<FJsonObject> Envelope = ToolResult.ToJson();
		if (!Envelope.IsValid())
		{
			return nullptr;
		}

		const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
		if (!Envelope->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray || ContentArray->Num() == 0)
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* ContentItem = nullptr;
		if (!(*ContentArray)[0].IsValid() || !(*ContentArray)[0]->TryGetObject(ContentItem) || !ContentItem)
		{
			return nullptr;
		}

		FString TextPayload;
		if (!(*ContentItem)->TryGetStringField(TEXT("text"), TextPayload) || TextPayload.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TextPayload);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			return Parsed;
		}

		TSharedPtr<FJsonObject> Fallback = MakeShared<FJsonObject>();
		Fallback->SetStringField(TEXT("text"), TextPayload);
		return Fallback;
	}

	FString DecodeErrorText(const FBridgeToolResult& ToolResult)
	{
		const TSharedPtr<FJsonObject> Payload = DecodeResultPayload(ToolResult);
		if (!Payload.IsValid())
		{
			return TEXT("unknown error");
		}

		FString ErrorText;
		if (Payload->TryGetStringField(TEXT("error"), ErrorText) && !ErrorText.IsEmpty())
		{
			return ErrorText;
		}
		if (Payload->TryGetStringField(TEXT("error_message"), ErrorText) && !ErrorText.IsEmpty())
		{
			return ErrorText;
		}
		if (Payload->TryGetStringField(TEXT("text"), ErrorText) && !ErrorText.IsEmpty())
		{
			return ErrorText;
		}
		return TEXT("unknown error");
	}
}

FString UBatchCallTool::GetToolDescription() const
{
	return TEXT("Dispatch a sequence of bridge tool calls in-process. Each entry is {tool, args}.");
}

TMap<FString, FBridgeSchemaProperty> UBatchCallTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Calls;
	Calls.Type = TEXT("array");
	Calls.Description = TEXT("Array of {tool: string, args: object} entries");
	Calls.bRequired = true;
	Schema.Add(TEXT("calls"), Calls);

	FBridgeSchemaProperty ContinueOnError;
	ContinueOnError.Type = TEXT("boolean");
	ContinueOnError.Description = TEXT("Continue dispatching after a failed entry");
	Schema.Add(TEXT("continue_on_error"), ContinueOnError);

	return Schema;
}

FBridgeToolResult UBatchCallTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* CallsArray = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("calls"), CallsArray) || !CallsArray)
	{
		return FBridgeToolResult::Error(TEXT("batch-call: 'calls' must be an array"));
	}

	const bool bContinueOnError = GetBoolArgOrDefault(Arguments, TEXT("continue_on_error"), false);

	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();
	TArray<TSharedPtr<FJsonValue>> WrappedResults;
	bool bAnyError = false;
	int32 StoppedAtIndex = INDEX_NONE;

	for (int32 Index = 0; Index < CallsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*CallsArray)[Index];
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!EntryValue.IsValid() || !EntryValue->TryGetObject(EntryObject) || !EntryObject)
		{
			TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
			EntryResult->SetStringField(TEXT("tool"), TEXT("<invalid-entry>"));
			EntryResult->SetStringField(TEXT("status"), TEXT("error"));
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("entry %d is not an object"), Index));
			WrappedResults.Add(MakeShared<FJsonValueObject>(EntryResult));
			bAnyError = true;
			if (!bContinueOnError)
			{
				StoppedAtIndex = Index;
				break;
			}
			continue;
		}

		FString ToolName;
		if (!(*EntryObject)->TryGetStringField(TEXT("tool"), ToolName) || ToolName.IsEmpty())
		{
			TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
			EntryResult->SetStringField(TEXT("tool"), TEXT("<missing>"));
			EntryResult->SetStringField(TEXT("status"), TEXT("error"));
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("entry %d missing 'tool'"), Index));
			WrappedResults.Add(MakeShared<FJsonValueObject>(EntryResult));
			bAnyError = true;
			if (!bContinueOnError)
			{
				StoppedAtIndex = Index;
				break;
			}
			continue;
		}

		TSharedPtr<FJsonObject> ToolArgs = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
		if ((*EntryObject)->TryGetObjectField(TEXT("args"), ArgsObject) && ArgsObject)
		{
			ToolArgs = *ArgsObject;
		}

		if (!Registry.HasTool(ToolName))
		{
			TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
			EntryResult->SetStringField(TEXT("tool"), ToolName);
			EntryResult->SetStringField(TEXT("status"), TEXT("error"));
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("tool '%s' not registered"), *ToolName));
			WrappedResults.Add(MakeShared<FJsonValueObject>(EntryResult));
			bAnyError = true;
			if (!bContinueOnError)
			{
				StoppedAtIndex = Index;
				break;
			}
			continue;
		}

		const FBridgeToolResult ToolResult = Registry.ExecuteTool(ToolName, ToolArgs, Context);
		TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
		EntryResult->SetStringField(TEXT("tool"), ToolName);
		EntryResult->SetStringField(TEXT("status"), ToolResult.bIsError ? TEXT("error") : TEXT("ok"));

		if (const TSharedPtr<FJsonObject> Payload = DecodeResultPayload(ToolResult); Payload.IsValid())
		{
			EntryResult->SetObjectField(TEXT("result"), Payload);
		}
		if (ToolResult.bIsError)
		{
			EntryResult->SetStringField(TEXT("error"), DecodeErrorText(ToolResult));
			bAnyError = true;
			if (!bContinueOnError)
			{
				WrappedResults.Add(MakeShared<FJsonValueObject>(EntryResult));
				StoppedAtIndex = Index;
				break;
			}
		}

		WrappedResults.Add(MakeShared<FJsonValueObject>(EntryResult));
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), bAnyError ? TEXT("error") : TEXT("ok"));
	Response->SetArrayField(TEXT("results"), WrappedResults);
	if (StoppedAtIndex != INDEX_NONE)
	{
		Response->SetNumberField(TEXT("stopped_at_index"), StoppedAtIndex);
	}
	return FBridgeToolResult::Json(Response);
}
