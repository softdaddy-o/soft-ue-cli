// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Scripting/BridgePythonCallHelper.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/BridgeToolRegistry.h"

namespace
{
	FString JsonToString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		if (Object.IsValid())
		{
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DecodeToolPayload(const FBridgeToolResult& Result)
	{
		const TSharedPtr<FJsonObject> Envelope = Result.ToJson();
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
		if (!(*ContentItem)->TryGetStringField(TEXT("text"), TextPayload))
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TextPayload);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			return Parsed;
		}

		TSharedPtr<FJsonObject> TextOnly = MakeShared<FJsonObject>();
		TextOnly->SetStringField(TEXT("text"), TextPayload);
		return TextOnly;
	}

	FString ErrorResponse(const FString& Message, const TSharedPtr<FJsonObject>& Payload = nullptr)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("is_error"), true);
		Object->SetStringField(TEXT("error_message"), Message);
		if (Payload.IsValid())
		{
			Object->SetObjectField(TEXT("payload"), Payload);
		}
		return JsonToString(Object);
	}
}

FString UBridgePythonCallHelper::CallTool(const FString& ToolName, const FString& ArgsJson)
{
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	if (!ArgsJson.IsEmpty())
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
		if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
		{
			return ErrorResponse(FString::Printf(TEXT("soft_ue_bridge.call: failed to parse args JSON for tool '%s'"), *ToolName));
		}
	}

	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();
	if (!Registry.HasTool(ToolName))
	{
		return ErrorResponse(FString::Printf(TEXT("soft_ue_bridge.call: tool '%s' not registered"), *ToolName));
	}

	FBridgeToolContext Context;
	Context.RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	const FBridgeToolResult Result = Registry.ExecuteTool(ToolName, Args, Context);
	const TSharedPtr<FJsonObject> Payload = DecodeToolPayload(Result);

	if (Result.bIsError)
	{
		FString ErrorMessage = TEXT("unknown error");
		if (Payload.IsValid())
		{
			Payload->TryGetStringField(TEXT("error"), ErrorMessage);
			if (ErrorMessage.IsEmpty())
			{
				Payload->TryGetStringField(TEXT("text"), ErrorMessage);
			}
		}
		return ErrorResponse(ErrorMessage, Payload);
	}

	return JsonToString(Payload.IsValid() ? Payload : MakeShared<FJsonObject>());
}
