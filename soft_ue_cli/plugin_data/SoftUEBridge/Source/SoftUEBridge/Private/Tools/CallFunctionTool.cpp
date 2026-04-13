// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/CallFunctionTool.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "SoftUEBridgeModule.h"
#include "Tools/BridgeToolRegistry.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

// ------------------------------------------------------------
// Type marshaling - supported input/output types
// ------------------------------------------------------------
// Input args (JSON -> C++ via ImportText_Direct):
//   - bool, int32, int64, float, double, FString, FName
//   - FVector/FRotator/FTransform (pass JSON string "X=1,Y=2,Z=3")
//   - native enums (pass JSON string of the enum name, e.g. "CS_Aim")
//   - UserDefinedEnum (pass JSON string of the display name)
//   - UserDefinedStruct (pass JSON string "Field1=Value1,Field2=Value2")
//   - UObject refs (pass JSON string of the object path)
//
// Output values (C++ -> JSON via ExportText_Direct):
//   - All outputs come back as strings. Callers that want typed JSON must
//     parse the string.
//
// Not supported:
//   - TArray / TMap / TSet of complex types
//   - Delegates / function pointers
//   - Latent functions
// ------------------------------------------------------------

namespace
{
	bool IsLatentFunction(UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const FName LatentActionInfoName(TEXT("LatentActionInfo"));
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (const FStructProperty* StructProp = CastField<FStructProperty>(*It))
			{
				if (StructProp->Struct
					&& StructProp->Struct->GetFName() == LatentActionInfoName)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::String:
			OutText = Value->AsString();
			return true;
		case EJson::Boolean:
			OutText = Value->AsBool() ? TEXT("true") : TEXT("false");
			return true;
		case EJson::Number:
			OutText = FString::SanitizeFloat(Value->AsNumber());
			return true;
		default:
			return false;
		}
	}

	TSharedPtr<FJsonObject> InvokeWithArgs(
		UObject* TargetObject,
		UFunction* Function,
		const TSharedPtr<FJsonObject>& FuncArgs)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!TargetObject || !Function)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("internal: null target or function"));
			return Result;
		}

		TArray<uint8> ParamBuffer;
		ParamBuffer.SetNumZeroed(Function->ParmsSize);

		for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (Prop->PropertyFlags & CPF_ReturnParm)
			{
				continue;
			}

			void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData());
			Prop->InitializeValue(ParamPtr);

			if (FuncArgs.IsValid())
			{
				const TSharedPtr<FJsonValue>* ArgValue = FuncArgs->Values.Find(Prop->GetName());
				if (ArgValue && ArgValue->IsValid())
				{
					FString ImportText;
					if (JsonValueToImportText(*ArgValue, ImportText) && !ImportText.IsEmpty())
					{
						Prop->ImportText_Direct(*ImportText, ParamPtr, TargetObject, PPF_None);
					}
				}
			}
		}

		TargetObject->ProcessEvent(Function, ParamBuffer.GetData());
		Result->SetBoolField(TEXT("success"), true);

		for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_OutParm) && !(Prop->PropertyFlags & CPF_ReturnParm))
			{
				continue;
			}

			void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData());
			FString ExportedValue;
			Prop->ExportText_Direct(ExportedValue, ParamPtr, ParamPtr, TargetObject, PPF_None);
			Result->SetStringField(Prop->GetName(), ExportedValue);
		}

		for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			FProperty* Prop = *PropIt;
			Prop->DestroyValue(Prop->ContainerPtrToValuePtr<void>(ParamBuffer.GetData()));
		}

		return Result;
	}

	UClass* LoadCallableClass(const FString& ClassPath)
	{
		UClass* LoadedClass = LoadClass<UObject>(nullptr, *ClassPath);
		if (!LoadedClass && !ClassPath.EndsWith(TEXT("_C")))
		{
			LoadedClass = LoadClass<UObject>(nullptr, *(ClassPath + TEXT("_C")));
		}
		return LoadedClass;
	}
}

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UCallFunctionTool)
#endif

FString UCallFunctionTool::GetToolDescription() const
{
	return TEXT("Call a Blueprint or native function on an actor, class default object, or transient instance.");
}

TMap<FString, FBridgeSchemaProperty> UCallFunctionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	auto Prop = [](const FString& Type, const FString& Description, bool bRequired = false) {
		FBridgeSchemaProperty Property;
		Property.Type = Type;
		Property.Description = Description;
		Property.bRequired = bRequired;
		return Property;
	};

	Schema.Add(TEXT("actor_name"), Prop(TEXT("string"), TEXT("Actor name or label (wildcards supported). Mutually exclusive with class_path.")));
	Schema.Add(TEXT("class_path"), Prop(TEXT("string"), TEXT("Blueprint/class path for CDO or transient modes. Mutually exclusive with actor_name.")));
	Schema.Add(TEXT("function_name"), Prop(TEXT("string"), TEXT("Function or event name to call"), true));
	Schema.Add(TEXT("args"), Prop(TEXT("object"), TEXT("Named arguments object. Mutually exclusive with batch.")));
	Schema.Add(TEXT("batch"), Prop(TEXT("array"), TEXT("Array of args objects. Mutually exclusive with args.")));
	Schema.Add(TEXT("spawn_transient"), Prop(TEXT("boolean"), TEXT("Instantiate a transient object and call the function on it.")));
	Schema.Add(TEXT("use_cdo"), Prop(TEXT("boolean"), TEXT("Call the function on the class default object.")));
	Schema.Add(TEXT("seed"), Prop(TEXT("integer"), TEXT("Seed FMath::RandInit(seed) before each call.")));
	Schema.Add(TEXT("world"), Prop(TEXT("string"), TEXT("World context for actor lookup: editor, pie, or game.")));

	return Schema;
}

FBridgeToolResult UCallFunctionTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString FunctionName = GetStringArgOrDefault(Args, TEXT("function_name"));
	if (FunctionName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("call-function: function_name is required"));
	}

	const FString ActorName = GetStringArgOrDefault(Args, TEXT("actor_name"));
	const FString ClassPath = GetStringArgOrDefault(Args, TEXT("class_path"));
	const bool bSpawnTransient = GetBoolArgOrDefault(Args, TEXT("spawn_transient"), false);
	const bool bUseCdo = GetBoolArgOrDefault(Args, TEXT("use_cdo"), false);

	if (!Args.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("call-function: arguments are required"));
	}
	if (!ActorName.IsEmpty() && !ClassPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("call-function: provide actor_name OR class_path, not both"));
	}
	if (ActorName.IsEmpty() && ClassPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("call-function: actor_name or class_path is required"));
	}
	if (bSpawnTransient && bUseCdo)
	{
		return FBridgeToolResult::Error(TEXT("call-function: spawn_transient and use_cdo are mutually exclusive"));
	}
	if ((bSpawnTransient || bUseCdo) && ClassPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("call-function: spawn_transient and use_cdo require class_path"));
	}
	if (Args->HasField(TEXT("args")) && Args->HasField(TEXT("batch")))
	{
		return FBridgeToolResult::Error(TEXT("call-function: args and batch are mutually exclusive"));
	}

	int32 Seed = 0;
	const bool bHasSeed = GetIntArg(Args, TEXT("seed"), Seed);

	UObject* TargetObject = nullptr;
	UClass* TargetClass = nullptr;
	UObject* TransientObject = nullptr;
	AActor* TransientActor = nullptr;

	auto CleanupTransient = [&]() {
		if (TransientActor)
		{
			TransientActor->Destroy();
			TransientActor = nullptr;
		}
		if (TransientObject)
		{
			TransientObject->RemoveFromRoot();
			TransientObject = nullptr;
		}
	};

	if (!ActorName.IsEmpty())
	{
		UWorld* World = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
		if (!World)
		{
			return FBridgeToolResult::Error(TEXT("call-function: no world available (specify world: editor|pie|game)"));
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (MatchesWildcard(Actor->GetName(), ActorName) || MatchesWildcard(GetActorLabelSafe(Actor), ActorName))
			{
				TargetObject = Actor;
				break;
			}
		}

		if (!TargetObject)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("call-function: actor '%s' not found"), *ActorName));
		}
		TargetClass = TargetObject->GetClass();
	}
	else
	{
		TargetClass = LoadCallableClass(ClassPath);
		if (!TargetClass)
		{
			return FBridgeToolResult::Error(FString::Printf(TEXT("call-function: failed to load class '%s'"), *ClassPath));
		}

		if (bUseCdo)
		{
			TargetObject = TargetClass->GetDefaultObject();
		}
		else if (bSpawnTransient)
		{
			if (TargetClass->IsChildOf(AActor::StaticClass()))
			{
				UWorld* SpawnWorld = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
				if (!SpawnWorld)
				{
					return FBridgeToolResult::Error(TEXT("call-function: no world available for transient actor spawn"));
				}

				FActorSpawnParameters SpawnParams;
				SpawnParams.ObjectFlags |= RF_Transient;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				TransientActor = SpawnWorld->SpawnActor<AActor>(TargetClass, FTransform::Identity, SpawnParams);
				if (!TransientActor)
				{
					return FBridgeToolResult::Error(TEXT("call-function: failed to spawn transient actor target"));
				}
				TargetObject = TransientActor;
			}
			else
			{
				TransientObject = NewObject<UObject>(GetTransientPackage(), TargetClass);
				if (!TransientObject)
				{
					return FBridgeToolResult::Error(TEXT("call-function: failed to create transient target object"));
				}
				TransientObject->AddToRoot();
				TargetObject = TransientObject;
			}
		}
		else
		{
			return FBridgeToolResult::Error(TEXT("call-function: class_path requires either spawn_transient or use_cdo"));
		}
	}

	UFunction* Function = TargetClass ? TargetClass->FindFunctionByName(*FunctionName) : nullptr;
	if (!Function && TargetObject)
	{
		Function = TargetObject->FindFunction(*FunctionName);
	}
	if (!Function)
	{
		CleanupTransient();
		return FBridgeToolResult::Error(FString::Printf(TEXT("call-function: function '%s' not found on target"), *FunctionName));
	}

	if (IsLatentFunction(Function))
	{
		CleanupTransient();
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetBoolField(TEXT("success"), false);
		ErrorObject->SetStringField(TEXT("error_code"), TEXT("BP_FUNCTION_IS_LATENT"));
		ErrorObject->SetStringField(TEXT("error"), FString::Printf(TEXT("call-function: '%s' is a latent function; use run-python-script for composition"), *FunctionName));
		return FBridgeToolResult::Json(ErrorObject);
	}

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	if (Args->TryGetArrayField(TEXT("batch"), BatchArray) && BatchArray)
	{
		TArray<TSharedPtr<FJsonValue>> ResultsArray;
		for (int32 RowIndex = 0; RowIndex < BatchArray->Num(); ++RowIndex)
		{
			if (bHasSeed)
			{
				FMath::RandInit(Seed);
			}

			TSharedPtr<FJsonObject> RowArgs = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* RowObject = nullptr;
			if ((*BatchArray)[RowIndex].IsValid() && (*BatchArray)[RowIndex]->TryGetObject(RowObject) && RowObject)
			{
				RowArgs = *RowObject;
			}

			TSharedPtr<FJsonObject> RowResult = InvokeWithArgs(TargetObject, Function, RowArgs);
			RowResult->SetNumberField(TEXT("row_index"), RowIndex);
			ResultsArray.Add(MakeShared<FJsonValueObject>(RowResult));
		}

		CleanupTransient();

		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("function"), FunctionName);
		Response->SetNumberField(TEXT("count"), ResultsArray.Num());
		Response->SetArrayField(TEXT("results"), ResultsArray);
		return FBridgeToolResult::Json(Response);
	}

	if (bHasSeed)
	{
		FMath::RandInit(Seed);
	}

	const TSharedPtr<FJsonObject>* FuncArgsObject = nullptr;
	TSharedPtr<FJsonObject> FuncArgs;
	if (Args->TryGetObjectField(TEXT("args"), FuncArgsObject) && FuncArgsObject)
	{
		FuncArgs = *FuncArgsObject;
	}

	TSharedPtr<FJsonObject> Result = InvokeWithArgs(TargetObject, Function, FuncArgs);
	if (!ActorName.IsEmpty())
	{
		Result->SetStringField(TEXT("actor"), TargetObject->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("class_path"), TargetClass->GetPathName());
	}
	Result->SetStringField(TEXT("function"), FunctionName);
	const FString TargetName = TargetObject ? TargetObject->GetName() : TargetClass->GetName();

	CleanupTransient();
	UE_LOG(LogSoftUEBridge, Log, TEXT("call-function: %s::%s"), *TargetName, *FunctionName);
	return FBridgeToolResult::Json(Result);
}
