// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Protocol/BridgeTypes.h"

enum class EBridgeServerStatus : uint8
{
	Stopped,
	Running,
	Error
};

/** HTTP/JSON-RPC server that dispatches to registered tools */
class SOFTUEBRIDGE_API FBridgeServer
{
public:
	FBridgeServer();
	~FBridgeServer();

	bool Start(int32 Port = 8080, const FString& BindAddress = TEXT("127.0.0.1"));
	void Stop();

	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return ServerPort; }
	EBridgeServerStatus GetStatus() const { return Status; }

private:
	TSharedPtr<IHttpRouter> HttpRouter;
	FHttpRouteHandle RouteHandle;

	bool bIsRunning = false;
	int32 ServerPort = 8080;
	EBridgeServerStatus Status = EBridgeServerStatus::Stopped;

	bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	FBridgeResponse ProcessRequest(const FBridgeRequest& Request);

	FBridgeResponse HandleInitialize(const FBridgeRequest& Request);
	FBridgeResponse HandleToolsList(const FBridgeRequest& Request);
	FBridgeResponse HandleToolsCall(const FBridgeRequest& Request);

	void SendResponse(const FHttpResultCallback& OnComplete, const FBridgeResponse& Response, int32 StatusCode = 200);
	void SendError(const FHttpResultCallback& OnComplete, int32 HttpStatus, int32 RpcCode, const FString& Message);
};
