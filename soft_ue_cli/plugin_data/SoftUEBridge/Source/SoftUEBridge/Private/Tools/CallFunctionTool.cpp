// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/CallFunctionTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UCallFunctionTool)
#endif

FString UCallFunctionTool::GetToolDescription() const
{
	return TEXT("Call a Blueprint or native function on an actor in the game world. "
		"Supports simple argument types: bool, int, float, string.");
}

TMap<FString, FBridgeSchemaProperty> UCallFunctionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	auto Prop = [](const FString& Type, const FString& Desc, bool bReq = false) {
		FBridgeSchemaProperty P;
		P.Type = Type; P.Description = Desc; P.bRequired = bReq;
		return P;
	};

	S.Add(TEXT("actor_name"),    Prop(TEXT("string"), TEXT("Actor name or label (wildcards: *pattern*)"), true));
	S.Add(TEXT("function_name"), Prop(TEXT("string"), TEXT("Function/event name to call"), true));
	S.Add(TEXT("args"),          Prop(TEXT("object"), TEXT("Named arguments (bool, int, float, string values)")));
	S.Add(TEXT("world"),         Prop(TEXT("string"), TEXT("World context: 'editor' (editor scene), 'pie' (Play-In-Editor), 'game' (packaged build only). Omit to use the first available world.")));

	return S;
}

FBridgeToolResult UCallFunctionTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString ActorName    = GetStringArgOrDefault(Args, TEXT("actor_name"));
	const FString FunctionName = GetStringArgOrDefault(Args, TEXT("function_name"));

	if (ActorName.IsEmpty() || FunctionName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("actor_name and function_name are required"));
	}

	UWorld* World = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No world available. Specify 'world': 'editor', 'pie', or 'game'."));
	}

	// Find actor
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (MatchesWildcard(A->GetName(), ActorName) || MatchesWildcard(A->GetActorLabel(), ActorName))
		{
			TargetActor = A;
			break;
		}
	}

	if (!TargetActor)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));
	}

	// Find the UFunction
	UFunction* Function = TargetActor->FindFunction(*FunctionName);
	if (!Function)
	{
		return FBridgeToolResult::Error(
			FString::Printf(TEXT("Function '%s' not found on '%s'"), *FunctionName, *ActorName));
	}

	// Allocate parameter memory and fill from Args["args"]
	const TSharedPtr<FJsonObject>* FuncArgs = nullptr;
	bool bHasArgs = Args.IsValid() && Args->TryGetObjectField(TEXT("args"), FuncArgs);

	// Use a uint8 buffer for the parameters
	TArray<uint8> ParamBuffer;
	ParamBuffer.SetNumZeroed(Function->ParmsSize);

	// Initialize parameters to their defaults
	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (Prop->PropertyFlags & CPF_ReturnParm) continue;

		void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData());
		Prop->InitializeValue(ParamPtr);

		// Inject provided argument values
		if (bHasArgs && FuncArgs)
		{
			FString PropName = Prop->GetName();
			const TSharedPtr<FJsonValue>* ArgVal = (*FuncArgs)->Values.Find(PropName);
			if (ArgVal && ArgVal->IsValid())
			{
				FString StrVal;
				if ((*ArgVal)->Type == EJson::String)       StrVal = (*ArgVal)->AsString();
				else if ((*ArgVal)->Type == EJson::Boolean) StrVal = (*ArgVal)->AsBool() ? TEXT("true") : TEXT("false");
				else if ((*ArgVal)->Type == EJson::Number)  StrVal = FString::SanitizeFloat((*ArgVal)->AsNumber());

				if (!StrVal.IsEmpty())
				{
					Prop->ImportText_Direct(*StrVal, ParamPtr, TargetActor, PPF_None);
				}
			}
		}
	}

	// Call the function
	TargetActor->ProcessEvent(Function, ParamBuffer.GetData());

	// Collect return value if any
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), TargetActor->GetName());
	Result->SetStringField(TEXT("function"), FunctionName);

	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!(Prop->PropertyFlags & CPF_OutParm) && !(Prop->PropertyFlags & CPF_ReturnParm)) continue;

		void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData());
		FString ExportedVal;
		Prop->ExportText_Direct(ExportedVal, ParamPtr, ParamPtr, TargetActor, PPF_None);
		Result->SetStringField(Prop->GetName(), ExportedVal);
	}

	// Destroy parameter objects
	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		FProperty* Prop = *PropIt;
		Prop->DestroyValue(Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData()));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("call-function: %s::%s"), *TargetActor->GetName(), *FunctionName);
	return FBridgeToolResult::Json(Result);
}
