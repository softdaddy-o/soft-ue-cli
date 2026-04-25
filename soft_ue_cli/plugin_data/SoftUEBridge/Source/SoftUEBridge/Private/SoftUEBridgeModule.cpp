// Copyright soft-ue-expert. All Rights Reserved.

#include "SoftUEBridgeModule.h"
#include "Modules/ModuleManager.h"
#include "Tools/BridgeToolRegistry.h"
#include "Tools/QueryLevelTool.h"
#include "Tools/CallFunctionTool.h"
#include "Tools/GetLogsTool.h"
#include "Tools/ConsoleVarTool.h"
#include "Tools/SpawnActorTool.h"
#include "Tools/SetPropertyTool.h"

DEFINE_LOG_CATEGORY(LogSoftUEBridge);

void FSoftUEBridgeModule::StartupModule()
{
	UE_LOG(LogSoftUEBridge, Log, TEXT("SoftUE Bridge module started (v%s)"), SOFTUEBRIDGE_VERSION);

	FBridgeToolRegistry& Registry = FBridgeToolRegistry::Get();
	Registry.RegisterToolClass<UQueryLevelTool>();
	Registry.RegisterToolClass<UCallFunctionTool>();
	Registry.RegisterToolClass<UGetLogsTool>();
	Registry.RegisterToolClass<UGetConsoleVarTool>();
	Registry.RegisterToolClass<USetConsoleVarTool>();
	Registry.RegisterToolClass<USpawnActorTool>();
	Registry.RegisterToolClass<USetPropertyTool>();

	UE_LOG(LogSoftUEBridge, Log, TEXT("Registered %d runtime bridge tools"), Registry.GetToolCount());
}

void FSoftUEBridgeModule::ShutdownModule()
{
	UE_LOG(LogSoftUEBridge, Log, TEXT("SoftUE Bridge module shutdown"));
}

IMPLEMENT_MODULE(FSoftUEBridgeModule, SoftUEBridge)
