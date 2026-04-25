// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"

FBridgeToolRegistry* FBridgeToolRegistry::Instance = nullptr;

FBridgeToolRegistry& FBridgeToolRegistry::Get()
{
	if (!Instance)
	{
		Instance = new FBridgeToolRegistry();
	}
	return *Instance;
}

FBridgeToolRegistry::~FBridgeToolRegistry()
{
	ClearAllTools();
}

void FBridgeToolRegistry::RegisterToolClass(UClass* ToolClass)
{
	if (!ToolClass) return;

	// Temporarily instantiate to get the name
	UBridgeToolBase* TempInstance = NewObject<UBridgeToolBase>(GetTransientPackage(), ToolClass);
	if (!TempInstance)
	{
		UE_LOG(LogSoftUEBridge, Error, TEXT("Failed to instantiate tool class: %s"), *ToolClass->GetName());
		return;
	}

	const FString ToolName = TempInstance->GetToolName();
	if (ToolName.IsEmpty())
	{
		UE_LOG(LogSoftUEBridge, Error, TEXT("Tool class %s returned empty name"), *ToolClass->GetName());
		return;
	}

	// AddToRoot prevents UE GC from collecting the instance (registry is a plain C++ singleton,
	// so TObjectPtr members are not scanned by the GC).
	TempInstance->AddToRoot();

	FScopeLock ScopeLock(&Lock);
	ToolClasses.Add(ToolName, ToolClass);
	ToolInstances.Add(ToolName, TempInstance);

	UE_LOG(LogSoftUEBridge, Log, TEXT("Registered tool: %s"), *ToolName);
}

void FBridgeToolRegistry::ClearAllTools()
{
	FScopeLock ScopeLock(&Lock);
	for (auto& Pair : ToolInstances)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromRoot();
		}
	}
	ToolInstances.Empty();
	ToolClasses.Empty();
}

TArray<FBridgeToolDefinition> FBridgeToolRegistry::GetAllToolDefinitions() const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FBridgeToolDefinition> Defs;
	for (const auto& Pair : ToolInstances)
	{
		if (Pair.Value)
		{
			Defs.Add(Pair.Value->GetDefinition());
		}
	}
	return Defs;
}

UBridgeToolBase* FBridgeToolRegistry::FindTool(const FString& ToolName)
{
	FScopeLock ScopeLock(&Lock);
	TObjectPtr<UBridgeToolBase>* Found = ToolInstances.Find(ToolName);
	return Found ? Found->Get() : nullptr;
}

bool FBridgeToolRegistry::HasTool(const FString& ToolName) const
{
	FScopeLock ScopeLock(&Lock);
	return ToolClasses.Contains(ToolName);
}

int32 FBridgeToolRegistry::GetToolCount() const
{
	FScopeLock ScopeLock(&Lock);
	return ToolClasses.Num();
}

FBridgeToolResult FBridgeToolRegistry::ExecuteTool(
	const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	UBridgeToolBase* Tool = FindTool(ToolName);
	if (!Tool)
	{
		return FBridgeToolResult::Error(
			FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
	}

	// Sanitize asset_path: collapse double slashes that crash CreatePackage/LoadObject
	if (Arguments.IsValid() && Arguments->HasField(TEXT("asset_path")))
	{
		FString Path = Arguments->GetStringField(TEXT("asset_path"));
		const FString OriginalPath = Path;
		while (Path.ReplaceInline(TEXT("//"), TEXT("/")) > 0) {}
		if (Path != OriginalPath)
		{
			UE_LOG(LogSoftUEBridge, Warning, TEXT("Sanitized asset_path: '%s' -> '%s'"), *OriginalPath, *Path);
			Arguments->SetStringField(TEXT("asset_path"), Path);
		}
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("Executing tool: %s"), *ToolName);
	return Tool->Execute(Arguments, Context);
}
