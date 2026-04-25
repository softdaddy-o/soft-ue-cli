// Copyright soft-ue-expert. All Rights Reserved.

#include "Protocol/BridgeTypes.h"
#include "SoftUEBridgeModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ── EBridgeMethod parse helper ───────────────────────────────────────────────

static EBridgeMethod ParseMethod(const FString& Method)
{
	if (Method == TEXT("initialize"))       return EBridgeMethod::Initialize;
	if (Method == TEXT("notifications/initialized")) return EBridgeMethod::Initialized;
	if (Method == TEXT("shutdown"))         return EBridgeMethod::Shutdown;
	if (Method == TEXT("tools/list"))       return EBridgeMethod::ToolsList;
	if (Method == TEXT("tools/call"))       return EBridgeMethod::ToolsCall;
	return EBridgeMethod::Unknown;
}

// ── FBridgeRequest ────────────────────────────────────────────────────────────

TOptional<FBridgeRequest> FBridgeRequest::FromJsonString(const FString& JsonString)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return {};
	}

	FBridgeRequest Req;

	Root->TryGetStringField(TEXT("jsonrpc"), Req.JsonRpc);

	// ID can be string or number; store as string
	const TSharedPtr<FJsonValue>* IdField = Root->Values.Find(TEXT("id"));
	if (IdField && IdField->IsValid())
	{
		if ((*IdField)->Type == EJson::String)
		{
			Req.Id = (*IdField)->AsString();
		}
		else if ((*IdField)->Type == EJson::Number)
		{
			Req.Id = FString::Printf(TEXT("%g"), (*IdField)->AsNumber());
		}
	}

	Root->TryGetStringField(TEXT("method"), Req.Method);
	Req.ParsedMethod = ParseMethod(Req.Method);

	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Root->TryGetObjectField(TEXT("params"), ParamsObj))
	{
		Req.Params = *ParamsObj;
	}

	return Req;
}

// ── FBridgeResponse ───────────────────────────────────────────────────────────

FBridgeResponse FBridgeResponse::Success(const FString& InId, TSharedPtr<FJsonObject> InResult)
{
	FBridgeResponse Resp;
	Resp.Id = InId;
	Resp.Result = InResult ? InResult : MakeShareable(new FJsonObject);
	return Resp;
}

FBridgeResponse FBridgeResponse::Error(const FString& InId, int32 Code, const FString& Message)
{
	FBridgeResponse Resp;
	Resp.Id = InId;
	TSharedPtr<FJsonObject> ErrObj = MakeShareable(new FJsonObject);
	ErrObj->SetNumberField(TEXT("code"), Code);
	ErrObj->SetStringField(TEXT("message"), Message);
	Resp.ErrorData = ErrObj;
	return Resp;
}

FString FBridgeResponse::ToJsonString() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetStringField(TEXT("jsonrpc"), JsonRpc);

	if (!Id.IsEmpty())
	{
		Root->SetStringField(TEXT("id"), Id);
	}

	if (ErrorData.IsValid())
	{
		Root->SetObjectField(TEXT("error"), ErrorData);
	}
	else if (Result.IsValid())
	{
		Root->SetObjectField(TEXT("result"), Result);
	}
	else
	{
		Root->SetObjectField(TEXT("result"), MakeShareable(new FJsonObject));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

// ── FBridgeSchemaProperty ─────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FBridgeSchemaProperty::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("type"), Type);
	Obj->SetStringField(TEXT("description"), Description);

	if (Enum.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> EnumArr;
		for (const FString& Val : Enum)
		{
			EnumArr.Add(MakeShareable(new FJsonValueString(Val)));
		}
		Obj->SetArrayField(TEXT("enum"), EnumArr);
	}

	if (!ItemsType.IsEmpty())
	{
		TSharedPtr<FJsonObject> Items = MakeShareable(new FJsonObject);
		Items->SetStringField(TEXT("type"), ItemsType);
		Obj->SetObjectField(TEXT("items"), Items);
	}

	return Obj;
}

// ── FBridgeToolDefinition ─────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FBridgeToolDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("name"), Name);
	Obj->SetStringField(TEXT("description"), Description);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject);
	for (const auto& Pair : InputSchema)
	{
		Props->SetObjectField(Pair.Key, Pair.Value.ToJson());
	}
	Schema->SetObjectField(TEXT("properties"), Props);

	if (Required.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ReqArr;
		for (const FString& R : Required)
		{
			ReqArr.Add(MakeShareable(new FJsonValueString(R)));
		}
		Schema->SetArrayField(TEXT("required"), ReqArr);
	}

	Obj->SetObjectField(TEXT("inputSchema"), Schema);
	return Obj;
}
