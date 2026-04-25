// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/GetPropertyTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UGetPropertyTool)
#endif

FString UGetPropertyTool::GetToolDescription() const
{
	return TEXT("Get a property value from an actor or its component using reflection. "
		"Use dot notation for component properties: 'ComponentName.PropertyName'.");
}

TMap<FString, FBridgeSchemaProperty> UGetPropertyTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	auto Prop = [](const FString& Type, const FString& Desc, bool bReq = false) {
		FBridgeSchemaProperty P;
		P.Type = Type; P.Description = Desc; P.bRequired = bReq;
		return P;
	};

	S.Add(TEXT("actor_name"),    Prop(TEXT("string"), TEXT("Actor name or label"), true));
	S.Add(TEXT("property_name"), Prop(TEXT("string"), TEXT("Property name, or 'Component.Property' for component properties"), true));
	S.Add(TEXT("world"),         Prop(TEXT("string"), TEXT("World context: 'editor' (editor scene), 'pie' (Play-In-Editor), 'game' (packaged build only). Omit to use the first available world.")));

	return S;
}

FBridgeToolResult UGetPropertyTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const FString ActorName    = GetStringArgOrDefault(Args, TEXT("actor_name"));
	const FString PropertyName = GetStringArgOrDefault(Args, TEXT("property_name"));

	UWorld* World = FindWorldByType(GetStringArgOrDefault(Args, TEXT("world")));
	if (!World)
	{
		return FBridgeToolResult::Error(TEXT("No world available. Specify 'world': 'editor', 'pie', or 'game'."));
	}

	// Find actor
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (MatchesWildcard(A->GetName(), ActorName) || MatchesWildcard(GetActorLabelSafe(A), ActorName))
		{
			Actor = A;
			break;
		}
	}
	if (!Actor)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));
	}

	// Resolve target object (actor or component via dot notation)
	UObject* TargetObject = Actor;
	FString PropKey = PropertyName;

	int32 DotIdx;
	if (PropertyName.FindChar(TEXT('.'), DotIdx))
	{
		const FString CompName = PropertyName.Left(DotIdx);
		PropKey = PropertyName.Mid(DotIdx + 1);

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* C : Components)
		{
			if (C && C->GetName().Equals(CompName, ESearchCase::IgnoreCase))
			{
				TargetObject = C;
				break;
			}
		}

		if (TargetObject == Actor)
		{
			return FBridgeToolResult::Error(
				FString::Printf(TEXT("Component '%s' not found on '%s'"), *CompName, *ActorName));
		}
	}

	// Find and read the property
	for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop) continue;
		if (!Prop->GetName().Equals(PropKey, ESearchCase::IgnoreCase)) continue;

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
		if (!ValuePtr)
		{
			return FBridgeToolResult::Error(TEXT("Failed to get property pointer"));
		}

		FString ExportedValue;
		Prop->ExportText_Direct(ExportedValue, ValuePtr, ValuePtr, TargetObject, PPF_None);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("actor"), Actor->GetName());
		Result->SetStringField(TEXT("property"), PropertyName);
		Result->SetStringField(TEXT("value"), ExportedValue);
		Result->SetStringField(TEXT("type"), Prop->GetCPPType());

		UE_LOG(LogSoftUEBridge, Log, TEXT("get-property: %s.%s = %s"), *ActorName, *PropertyName, *ExportedValue);
		return FBridgeToolResult::Json(Result);
	}

	return FBridgeToolResult::Error(
		FString::Printf(TEXT("Property '%s' not found on %s"), *PropKey, *TargetObject->GetClass()->GetName()));
}
