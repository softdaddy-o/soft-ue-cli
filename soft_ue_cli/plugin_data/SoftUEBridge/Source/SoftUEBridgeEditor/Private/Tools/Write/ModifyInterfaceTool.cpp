// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Write/ModifyInterfaceTool.h"
#include "Utils/BridgeAssetModifier.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString UModifyInterfaceTool::GetToolDescription() const
{
	return TEXT("Add or remove an implemented interface on a Blueprint or AnimBlueprint. "
		"Use action 'add' to implement a new interface, 'remove' to remove an existing one. "
		"Interface class can be specified as a short name (e.g., 'ALI_Locomotion') or full path "
		"(e.g., '/Game/Animation/ALI_Locomotion.ALI_Locomotion_C').");
}

TMap<FString, FBridgeSchemaProperty> UModifyInterfaceTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Blueprint or AnimBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action to perform: 'add' or 'remove'");
	Action.bRequired = true;
	Schema.Add(TEXT("action"), Action);

	FBridgeSchemaProperty InterfaceClass;
	InterfaceClass.Type = TEXT("string");
	InterfaceClass.Description = TEXT("Interface class name or full path (e.g., 'ALI_Locomotion' or '/Game/Animation/ALI_Locomotion.ALI_Locomotion_C')");
	InterfaceClass.bRequired = true;
	Schema.Add(TEXT("interface_class"), InterfaceClass);

	return Schema;
}

TArray<FString> UModifyInterfaceTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("action"), TEXT("interface_class") };
}

UClass* UModifyInterfaceTool::ResolveInterfaceClass(const FString& InterfaceClassStr, FString& OutError) const
{
	UClass* InterfaceClass = nullptr;

	// Try as full path first
	if (InterfaceClassStr.StartsWith(TEXT("/")))
	{
		UObject* Loaded = StaticLoadObject(UClass::StaticClass(), nullptr, *InterfaceClassStr);
		InterfaceClass = Cast<UClass>(Loaded);
	}

	// Try as short class name — search with _C suffix if needed
	if (!InterfaceClass)
	{
		FString SearchName = InterfaceClassStr;
		if (!SearchName.EndsWith(TEXT("_C")))
		{
			SearchName += TEXT("_C");
		}

		// Search in memory first
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == SearchName || It->GetName() == InterfaceClassStr)
			{
				if (It->HasAnyClassFlags(CLASS_Interface) || It->IsChildOf(UInterface::StaticClass()))
				{
					InterfaceClass = *It;
					break;
				}
			}
		}
	}

	// Try loading by guessing the package path via asset registry
	if (!InterfaceClass && !InterfaceClassStr.StartsWith(TEXT("/")))
	{
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		FString BaseName = InterfaceClassStr;
		BaseName.RemoveFromEnd(TEXT("_C"));

		AssetRegistry.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")), Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == BaseName)
			{
				UBlueprint* InterfaceBP = Cast<UBlueprint>(Asset.GetAsset());
				if (InterfaceBP && InterfaceBP->GeneratedClass)
				{
					InterfaceClass = InterfaceBP->GeneratedClass;
					break;
				}
			}
		}
	}

	if (!InterfaceClass)
	{
		OutError = FString::Printf(TEXT("Could not resolve interface class: %s"), *InterfaceClassStr);
	}

	return InterfaceClass;
}

FBridgeToolResult UModifyInterfaceTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString Action = GetStringArgOrDefault(Arguments, TEXT("action"));
	FString InterfaceClassStr = GetStringArgOrDefault(Arguments, TEXT("interface_class"));

	if (AssetPath.IsEmpty() || Action.IsEmpty() || InterfaceClassStr.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path, action, and interface_class are required"));
	}

	if (Action != TEXT("add") && Action != TEXT("remove"))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Invalid action: '%s'. Must be 'add' or 'remove'."), *Action));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("modify-interface: %s interface %s on %s"), *Action, *InterfaceClassStr, *AssetPath);

	// Load Blueprint
	FString LoadError;
	UBlueprint* Blueprint = FBridgeAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	// Resolve interface class
	FString ResolveError;
	UClass* InterfaceClass = ResolveInterfaceClass(InterfaceClassStr, ResolveError);
	if (!InterfaceClass)
	{
		return FBridgeToolResult::Error(ResolveError);
	}

	// Begin transaction for undo support
	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "ModifyInterface", "{0} interface {1} on {2}"),
			FText::FromString(Action),
			FText::FromString(InterfaceClass->GetName()),
			FText::FromString(AssetPath)));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("interface_class"), InterfaceClass->GetName());
	Result->SetStringField(TEXT("interface_path"), InterfaceClass->GetPathName());

	if (Action == TEXT("add"))
	{
		// Check if already implemented
		for (const FBPInterfaceDescription& Existing : Blueprint->ImplementedInterfaces)
		{
			if (Existing.Interface == InterfaceClass)
			{
				return FBridgeToolResult::Error(FString::Printf(
					TEXT("Interface '%s' is already implemented on '%s'"),
					*InterfaceClass->GetName(), *AssetPath));
			}
		}

		FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()));
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("modify-interface: Added interface %s to %s"), *InterfaceClass->GetName(), *AssetPath);
	}
	else // remove
	{
		// Check if actually implemented
		bool bFound = false;
		for (const FBPInterfaceDescription& Existing : Blueprint->ImplementedInterfaces)
		{
			if (Existing.Interface == InterfaceClass)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Interface '%s' is not implemented on '%s'"),
				*InterfaceClass->GetName(), *AssetPath));
		}

		FBlueprintEditorUtils::RemoveInterface(Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()));
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("modify-interface: Removed interface %s from %s"), *InterfaceClass->GetName(), *AssetPath);
	}

	// Compile Blueprint
	FString CompileError;
	FBridgeAssetModifier::CompileBlueprint(Blueprint, CompileError);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("needs_save"), true);
	if (!CompileError.IsEmpty())
	{
		Result->SetStringField(TEXT("compile_warning"), CompileError);
	}

	return FBridgeToolResult::Json(Result);
}
