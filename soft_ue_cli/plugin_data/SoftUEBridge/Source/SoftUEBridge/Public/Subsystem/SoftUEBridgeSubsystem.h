// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Subsystems/EngineSubsystem.h"
#include "Server/BridgeServer.h"
#include "SoftUEBridgeSubsystem.generated.h"

/** Engine subsystem that hosts the bridge HTTP server.
 *  Active in the editor, PIE, and development builds by default. */
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

#if WITH_EDITOR
	/** Editor lifecycle hooks used to revive HTTP listeners after PIE disrupts them. */
	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle ShutdownPIEHandle;

	void RegisterPIERecoveryHooks();
	void UnregisterPIERecoveryHooks();
	void HandlePostPIEStarted(bool bIsSimulating);
	void HandleShutdownPIE(bool bIsSimulating);
#endif

	void ReviveHttpListeners(const TCHAR* Reason);

	/** Port read from env var SOFT_UE_BRIDGE_PORT, default 8080 */
	int32 ResolvePort() const;

	/** Write ~/.soft-ue-bridge/instance.json so the CLI can auto-discover the port */
	void WriteInstanceRegistry(int32 Port) const;
	void DeleteInstanceRegistry() const;
};
