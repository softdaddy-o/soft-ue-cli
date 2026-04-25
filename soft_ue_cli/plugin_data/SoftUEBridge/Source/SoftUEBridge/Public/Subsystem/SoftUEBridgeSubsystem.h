// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Server/BridgeServer.h"
#include "SoftUEBridgeSubsystem.generated.h"

/** Engine subsystem that hosts the bridge HTTP server.
 *  Active in the editor, PIE, and packaged builds. */
UCLASS()
class SOFTUEBRIDGE_API USoftUEBridgeSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Start the bridge HTTP server */
	UFUNCTION(BlueprintCallable, Category = "SoftUEBridge")
	bool StartServer(int32 Port = 8080);

	/** Stop the bridge HTTP server */
	UFUNCTION(BlueprintCallable, Category = "SoftUEBridge")
	void StopServer();

	/** Stop and restart the server on the configured port (respects SOFT_UE_BRIDGE_PORT env var) */
	UFUNCTION(BlueprintCallable, Category = "SoftUEBridge")
	void RestartServer();

	UFUNCTION(BlueprintCallable, Category = "SoftUEBridge")
	bool IsServerRunning() const;

	UFUNCTION(BlueprintCallable, Category = "SoftUEBridge")
	int32 GetServerPort() const;

private:
	TUniquePtr<FBridgeServer> Server;

	/** Port read from env var SOFT_UE_BRIDGE_PORT, default 8080 */
	int32 ResolvePort() const;

	/** Write ~/.soft-ue-bridge/instance.json so the CLI can auto-discover the port */
	void WriteInstanceRegistry(int32 Port) const;
	void DeleteInstanceRegistry() const;
};
