// Copyright soft-ue-expert. All Rights Reserved.

#include "Server/BridgeServer.h"
#include "SoftUEBridgeModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "HttpServerModule.h"
#include "HttpPath.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"

FBridgeServer::FBridgeServer() = default;

FBridgeServer::~FBridgeServer()
{
	Stop();
}

bool FBridgeServer::Start(int32 Port, const FString& BindAddress)
{
	if (bIsRunning)
	{
		return true;
	}

	ServerPort = Port;

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	HttpRouter = HttpServerModule.GetHttpRouter(ServerPort);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogSoftUEBridge, Error, TEXT("Failed to get HTTP router for port %d"), ServerPort);
		Status = EBridgeServerStatus::Error;
		return false;
	}

	RouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/bridge")),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FBridgeServer::HandleRequest)
	);

	if (!RouteHandle.IsValid())
	{
		UE_LOG(LogSoftUEBridge, Error, TEXT("Failed to bind /bridge route"));
		Status = EBridgeServerStatus::Error;
		return false;
	}

	HttpServerModule.StartAllListeners();
	bIsRunning = true;
	Status = EBridgeServerStatus::Running;

	UE_LOG(LogSoftUEBridge, Log, TEXT("Bridge server started on http://%s:%d/bridge"), *BindAddress, ServerPort);
	return true;
}

void FBridgeServer::Stop()
{
	if (!bIsRunning) return;

	if (HttpRouter.IsValid() && RouteHandle.IsValid())
	{
		HttpRouter->UnbindRoute(RouteHandle);
	}

	bIsRunning = false;
	Status = EBridgeServerStatus::Stopped;
	UE_LOG(LogSoftUEBridge, Log, TEXT("Bridge server stopped"));
}

bool FBridgeServer::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// CORS preflight
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		TUniquePtr<FHttpServerResponse> Resp = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
		Resp->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
		Resp->Headers.Add(TEXT("Access-Control-Allow-Methods"), {TEXT("GET, POST, OPTIONS")});
		Resp->Headers.Add(TEXT("Access-Control-Allow-Headers"), {TEXT("Content-Type")});
		Resp->Code = EHttpServerResponseCodes::NoContent;
		OnComplete(MoveTemp(Resp));
		return true;
	}

	// Health check GET
	if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
	{
		TSharedPtr<FJsonObject> Info = MakeShareable(new FJsonObject);
		Info->SetStringField(TEXT("name"), TEXT("soft-ue-bridge"));
		Info->SetStringField(TEXT("version"), SOFTUEBRIDGE_VERSION);
		Info->SetBoolField(TEXT("running"), true);
		Info->SetNumberField(TEXT("tools"), FBridgeToolRegistry::Get().GetToolCount());

		FString JsonStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
		FJsonSerializer::Serialize(Info.ToSharedRef(), Writer);

		TUniquePtr<FHttpServerResponse> Resp = FHttpServerResponse::Create(JsonStr, TEXT("application/json"));
		Resp->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
		Resp->Code = EHttpServerResponseCodes::Ok;
		OnComplete(MoveTemp(Resp));
		return true;
	}

	if (Request.Verb != EHttpServerRequestVerbs::VERB_POST)
	{
		SendError(OnComplete, 405, EBridgeErrorCode::InvalidRequest, TEXT("Method not allowed"));
		return true;
	}

	// Parse body
	FString Body;
	if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR Convert(
			reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()),
			Request.Body.Num());
		Body = FString(Convert.Length(), Convert.Get());
	}

	if (Body.IsEmpty())
	{
		SendError(OnComplete, 400, EBridgeErrorCode::ParseError, TEXT("Empty request body"));
		return true;
	}

	TOptional<FBridgeRequest> Parsed = FBridgeRequest::FromJsonString(Body);
	if (!Parsed.IsSet())
	{
		SendError(OnComplete, 400, EBridgeErrorCode::ParseError, TEXT("Invalid JSON-RPC"));
		return true;
	}

	FBridgeRequest BridgeReq = Parsed.GetValue();

	// Execute on game thread (UE API requires it)
	AsyncTask(ENamedThreads::GameThread, [this, BridgeReq, OnComplete]()
	{
		FBridgeResponse Response;
#if PLATFORM_EXCEPTIONS_DISABLED
		Response = ProcessRequest(BridgeReq);
#else
		try
		{
			Response = ProcessRequest(BridgeReq);
		}
		catch (const std::exception& e)
		{
			Response = FBridgeResponse::Error(BridgeReq.Id, EBridgeErrorCode::InternalError,
				FString::Printf(TEXT("Exception: %s"), ANSI_TO_TCHAR(e.what())));
		}
		catch (...)
		{
			Response = FBridgeResponse::Error(BridgeReq.Id, EBridgeErrorCode::InternalError,
				TEXT("Unknown exception"));
		}
#endif

		if (BridgeReq.IsNotification())
		{
			TUniquePtr<FHttpServerResponse> Resp = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
			Resp->Code = EHttpServerResponseCodes::Accepted;
			OnComplete(MoveTemp(Resp));
			return;
		}

		SendResponse(OnComplete, Response);
	});

	return true;
}

FBridgeResponse FBridgeServer::ProcessRequest(const FBridgeRequest& Request)
{
	switch (Request.ParsedMethod)
	{
	case EBridgeMethod::Initialize:
		return HandleInitialize(Request);

	case EBridgeMethod::Initialized:
		return FBridgeResponse::Success(Request.Id, MakeShareable(new FJsonObject));

	case EBridgeMethod::Shutdown:
		return FBridgeResponse::Success(Request.Id, MakeShareable(new FJsonObject));

	case EBridgeMethod::ToolsList:
		return HandleToolsList(Request);

	case EBridgeMethod::ToolsCall:
		return HandleToolsCall(Request);

	default:
		return FBridgeResponse::Error(Request.Id, EBridgeErrorCode::MethodNotFound,
			FString::Printf(TEXT("Unknown method: %s"), *Request.Method));
	}
}

FBridgeResponse FBridgeServer::HandleInitialize(const FBridgeRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));

	TSharedPtr<FJsonObject> Caps = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> Tools = MakeShareable(new FJsonObject);
	Caps->SetObjectField(TEXT("tools"), Tools);
	Result->SetObjectField(TEXT("capabilities"), Caps);

	TSharedPtr<FJsonObject> ServerInfo = MakeShareable(new FJsonObject);
	ServerInfo->SetStringField(TEXT("name"), TEXT("soft-ue-bridge"));
	ServerInfo->SetStringField(TEXT("version"), SOFTUEBRIDGE_VERSION);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	return FBridgeResponse::Success(Request.Id, Result);
}

FBridgeResponse FBridgeServer::HandleToolsList(const FBridgeRequest& Request)
{
	TArray<FBridgeToolDefinition> Tools = FBridgeToolRegistry::Get().GetAllToolDefinitions();

	TArray<TSharedPtr<FJsonValue>> ToolsArr;
	for (const FBridgeToolDefinition& Tool : Tools)
	{
		ToolsArr.Add(MakeShareable(new FJsonValueObject(Tool.ToJson())));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("tools"), ToolsArr);
	return FBridgeResponse::Success(Request.Id, Result);
}

FBridgeResponse FBridgeServer::HandleToolsCall(const FBridgeRequest& Request)
{
	if (!Request.Params.IsValid())
	{
		return FBridgeResponse::Error(Request.Id, EBridgeErrorCode::InvalidParams, TEXT("Missing params"));
	}

	FString ToolName;
	if (!Request.Params->TryGetStringField(TEXT("name"), ToolName))
	{
		return FBridgeResponse::Error(Request.Id, EBridgeErrorCode::InvalidParams, TEXT("Missing tool name"));
	}

	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* ArgsObj;
	if (Request.Params->TryGetObjectField(TEXT("arguments"), ArgsObj))
	{
		Arguments = *ArgsObj;
	}
	else
	{
		Arguments = MakeShareable(new FJsonObject);
	}

	FBridgeToolContext Context;
	Context.RequestId = Request.Id;

	FBridgeToolResult ToolResult = FBridgeToolRegistry::Get().ExecuteTool(ToolName, Arguments, Context);
	return FBridgeResponse::Success(Request.Id, ToolResult.ToJson());
}

void FBridgeServer::SendResponse(const FHttpResultCallback& OnComplete, const FBridgeResponse& Response, int32 StatusCode)
{
	FString JsonStr = Response.ToJsonString();
	TUniquePtr<FHttpServerResponse> Resp = FHttpServerResponse::Create(JsonStr, TEXT("application/json"));
	Resp->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
	Resp->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	OnComplete(MoveTemp(Resp));
}

void FBridgeServer::SendError(const FHttpResultCallback& OnComplete, int32 HttpStatus, int32 RpcCode, const FString& Message)
{
	SendResponse(OnComplete, FBridgeResponse::Error(TEXT(""), RpcCode, Message), HttpStatus);
}
