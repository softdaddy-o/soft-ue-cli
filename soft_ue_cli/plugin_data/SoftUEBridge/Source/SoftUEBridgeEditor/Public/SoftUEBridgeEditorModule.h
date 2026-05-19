// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSoftUEBridgeEditor, Log, All);

class FSoftUEBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle PostEngineInitHandle;
	FTSTicker::FDelegateHandle DeferredAnimationRegistrationHandle;

	void RegisterAnimationTools();
	bool RegisterAnimationToolsOnTicker(float DeltaTime);
};
