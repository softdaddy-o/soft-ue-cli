// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/ConsoleVarTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "HAL/IConsoleManager.h"

// ── get-console-var ───────────────────────────────────────────────────────────

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UGetConsoleVarTool)
#endif

FString UGetConsoleVarTool::GetToolDescription() const
{
	return TEXT("Get the current value of a console variable (CVar).");
}

TMap<FString, FBridgeSchemaProperty> UGetConsoleVarTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;
	FBridgeSchemaProperty P;
	P.Type = TEXT("string");
	P.Description = TEXT("CVar name, e.g. 'r.ScreenPercentage'");
	P.bRequired = true;
	S.Add(TEXT("name"), P);
	return S;
}

FBridgeToolResult UGetConsoleVarTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString Name = GetStringArgOrDefault(Args, TEXT("name"));
	if (Name.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("name is required"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("value"), CVar->GetString());

	return FBridgeToolResult::Json(Result);
}

// ── set-console-var ───────────────────────────────────────────────────────────

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(USetConsoleVarTool)
#endif

FString USetConsoleVarTool::GetToolDescription() const
{
	return TEXT("Set a console variable (CVar) to a new value.");
}

TMap<FString, FBridgeSchemaProperty> USetConsoleVarTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	FBridgeSchemaProperty Name;
	Name.Type = TEXT("string");
	Name.Description = TEXT("CVar name, e.g. 'r.ScreenPercentage'");
	Name.bRequired = true;
	S.Add(TEXT("name"), Name);

	FBridgeSchemaProperty Val;
	Val.Type = TEXT("string");
	Val.Description = TEXT("New value as string");
	Val.bRequired = true;
	S.Add(TEXT("value"), Val);

	return S;
}

FBridgeToolResult USetConsoleVarTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString Name  = GetStringArgOrDefault(Args, TEXT("name"));
	FString Value;
	if (Name.IsEmpty() || !GetStringArg(Args, TEXT("value"), Value))
	{
		return FBridgeToolResult::Error(TEXT("name and value are required"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	CVar->Set(*Value, ECVF_SetByCode);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("value"), CVar->GetString());

	UE_LOG(LogSoftUEBridge, Log, TEXT("set-console-var: %s = %s"), *Name, *Value);
	return FBridgeToolResult::Json(Result);
}
