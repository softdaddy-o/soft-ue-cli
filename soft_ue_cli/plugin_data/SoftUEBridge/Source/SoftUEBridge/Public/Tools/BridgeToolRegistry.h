// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"

/** Central registry for bridge tools (singleton) */
class SOFTUEBRIDGE_API FBridgeToolRegistry
{
public:
	static FBridgeToolRegistry& Get();
	~FBridgeToolRegistry();

	template<typename T>
	void RegisterToolClass()
	{
		static_assert(TIsDerivedFrom<T, UBridgeToolBase>::Value,
			"Must derive from UBridgeToolBase");
		RegisterToolClass(T::StaticClass());
	}

	void RegisterToolClass(UClass* ToolClass);
	int32 RemoveToolsForModule(const FString& ModuleName);
	void ClearAllTools();

	TArray<FBridgeToolDefinition> GetAllToolDefinitions() const;
	UBridgeToolBase* FindTool(const FString& ToolName);
	bool HasTool(const FString& ToolName) const;
	int32 GetToolCount() const;
	EBridgeToolExecutionContext GetToolExecutionContext(const FString& ToolName);

	FBridgeToolResult ExecuteTool(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context);
	TArray<FString> GetRegisteredToolNames() const;
	TArray<FString> GetLoadedModulePaths() const;

private:
	FBridgeToolRegistry() = default;
	FBridgeToolRegistry(const FBridgeToolRegistry&) = delete;
	FBridgeToolRegistry& operator=(const FBridgeToolRegistry&) = delete;

	TMap<FString, UClass*> ToolClasses;
	TMap<FString, TObjectPtr<UBridgeToolBase>> ToolInstances; // instances are AddToRoot'd to prevent GC
	TMap<FString, FString> ToolModuleNames;
	mutable FCriticalSection Lock;

	static FBridgeToolRegistry* Instance;
};
