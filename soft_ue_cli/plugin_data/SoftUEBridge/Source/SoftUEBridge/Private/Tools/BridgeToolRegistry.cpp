// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ModuleDescriptor.h"

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
		TArray<FString> ToolNames = GetRegisteredToolNames();
		TArray<FString> ToolNameLines;
		for (const FString& Name : ToolNames)
		{
			ToolNameLines.Add(FString::Printf(TEXT("  - %s"), *Name));
		}
		const FString AvailableTools = ToolNameLines.Num() > 0
			? FString::Join(ToolNameLines, TEXT("\n"))
			: TEXT("\n  - (no tools registered)");

		TArray<FString> ModulePaths = GetLoadedModulePaths();
		TArray<FString> ModulePathLines;
		for (const FString& ModulePath : ModulePaths)
		{
			ModulePathLines.Add(FString::Printf(TEXT("  - %s"), *ModulePath));
		}
		const FString AvailableModulePaths = ModulePathLines.Num() > 0
			? FString::Join(ModulePathLines, TEXT("\n"))
			: TEXT("\n  - (no loaded module paths available)");

		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Unknown tool: %s.\n")
			TEXT("Requested: %s\n")
			TEXT("Registered tool count: %d\n")
			TEXT("Bridge version: %s\n")
			TEXT("Available tools:\n%s\n")
			TEXT("Loaded module paths:\n%s\n")
			TEXT("Guidance: Restart the editor and ensure the SoftUEBridge plugin was rebuilt from the latest CLI-compatible source."),
			*ToolName,
			*ToolName,
			GetToolCount(),
			SOFTUEBRIDGE_VERSION,
			*AvailableTools,
			*AvailableModulePaths));
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

TArray<FString> FBridgeToolRegistry::GetRegisteredToolNames() const
{
	FScopeLock ScopeLock(&Lock);
	TArray<FString> ToolNames;
	ToolClasses.GetKeys(ToolNames);
	ToolNames.Sort();
	return ToolNames;
}

TArray<FString> FBridgeToolRegistry::GetLoadedModulePaths() const
{
	TArray<FString> ModulePaths;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SoftUEBridge"));
	if (!Plugin.IsValid())
	{
		return ModulePaths;
	}

	const FString BinariesDir = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Binaries"),
		FPlatformProcess::GetBinariesSubdirectory()
	);
	TArray<FString> Extensions;
	Extensions.Add(TEXT(".dll"));
	Extensions.Add(TEXT(".so"));
	Extensions.Add(TEXT(".dylib"));
	Extensions.Add(TEXT(".bundle"));
	Extensions.Add(TEXT(".app"));
	const FModuleManager& ModuleManager = FModuleManager::Get();

	for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
	{
		const FString ModuleName = Module.Name.ToString();
		if (ModuleName.IsEmpty())
		{
			continue;
		}
		const FName ModuleFName(*ModuleName);
		if (!ModuleManager.IsModuleLoaded(ModuleFName))
		{
			continue;
		}

		FString ModuleFilePath = ModuleManager.GetModuleFilename(ModuleFName);
		for (const FString& Extension : Extensions)
		{
			if (!ModuleFilePath.IsEmpty())
			{
				break;
			}
			const FString CandidatePath = FPaths::Combine(BinariesDir, ModuleName + Extension);
			if (FPaths::FileExists(CandidatePath))
			{
				ModuleFilePath = CandidatePath;
				break;
			}
		}

		if (ModuleFilePath.IsEmpty())
		{
			ModulePaths.Add(FString::Printf(TEXT("%s: loaded (path unavailable)"), *ModuleName));
		}
		else
		{
			ModulePaths.Add(FString::Printf(TEXT("%s: %s"), *ModuleName, *ModuleFilePath));
		}
	}

	ModulePaths.Sort();
	return ModulePaths;
}
