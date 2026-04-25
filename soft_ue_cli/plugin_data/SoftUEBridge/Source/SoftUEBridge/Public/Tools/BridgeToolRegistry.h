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
	void ClearAllTools();

	TArray<FBridgeToolDefinition> GetAllToolDefinitions() const;
	UBridgeToolBase* FindTool(const FString& ToolName);
	bool HasTool(const FString& ToolName) const;
	int32 GetToolCount() const;

	FBridgeToolResult ExecuteTool(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context);

private:
	FBridgeToolRegistry() = default;
	FBridgeToolRegistry(const FBridgeToolRegistry&) = delete;
	FBridgeToolRegistry& operator=(const FBridgeToolRegistry&) = delete;

	TMap<FString, UClass*> ToolClasses;
	TMap<FString, TObjectPtr<UBridgeToolBase>> ToolInstances; // instances are AddToRoot'd to prevent GC
	mutable FCriticalSection Lock;

	static FBridgeToolRegistry* Instance;
};

/** Auto-registration macro -- include BridgeToolRegistry.h in your .cpp to use this */
#define REGISTER_BRIDGE_TOOL(ToolClass) \
	static struct F##ToolClass##Registrar \
	{ \
		F##ToolClass##Registrar() \
		{ \
			FBridgeToolRegistry::Get().RegisterToolClass(ToolClass::StaticClass()); \
		} \
	} G##ToolClass##Registrar;
