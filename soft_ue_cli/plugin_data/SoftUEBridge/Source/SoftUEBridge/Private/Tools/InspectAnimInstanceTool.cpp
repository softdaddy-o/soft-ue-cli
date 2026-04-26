// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/InspectAnimInstanceTool.h"
#include "SoftUEBridgeModule.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

FString UInspectAnimInstanceTool::GetToolDescription() const
{
	return TEXT("One-shot snapshot of a target actor's UAnimInstance (state machines, montages, slots, notifies, blend weights).");
}

TMap<FString, FBridgeSchemaProperty> UInspectAnimInstanceTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty ActorTag;
	ActorTag.Type = TEXT("string");
	ActorTag.Description = TEXT("Actor tag to search for in the PIE or game world");
	ActorTag.bRequired = true;
	Schema.Add(TEXT("actor_tag"), ActorTag);

	FBridgeSchemaProperty MeshComponent;
	MeshComponent.Type = TEXT("string");
	MeshComponent.Description = TEXT("Optional SkeletalMeshComponent name (default: first found)");
	Schema.Add(TEXT("mesh_component"), MeshComponent);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("array");
	Include.Description = TEXT("Sections to include: state_machines, montages, notifies, blend_weights");
	Schema.Add(TEXT("include"), Include);

	FBridgeSchemaProperty BlendWeights;
	BlendWeights.Type = TEXT("array");
	BlendWeights.Description = TEXT("Named float properties on the anim instance to read");
	Schema.Add(TEXT("blend_weights"), BlendWeights);

	return Schema;
}

FBridgeToolResult UInspectAnimInstanceTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString ActorTag;
	if (!GetStringArg(Arguments, TEXT("actor_tag"), ActorTag) || ActorTag.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("inspect-anim-instance: 'actor_tag' is required"));
	}

	const FString MeshComponentName = GetStringArgOrDefault(Arguments, TEXT("mesh_component"));

	TSet<FString> IncludeSet = {
		TEXT("state_machines"),
		TEXT("montages"),
		TEXT("notifies"),
		TEXT("blend_weights"),
	};
	const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr;
	if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("include"), IncludeArray) && IncludeArray)
	{
		IncludeSet.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *IncludeArray)
		{
			if (Value.IsValid())
			{
				IncludeSet.Add(Value->AsString());
			}
		}
	}

	TArray<FString> BlendWeightProps;
	const TArray<TSharedPtr<FJsonValue>>* BlendWeightsArray = nullptr;
	if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("blend_weights"), BlendWeightsArray) && BlendWeightsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *BlendWeightsArray)
		{
			if (Value.IsValid())
			{
				BlendWeightProps.Add(Value->AsString());
			}
		}
	}

	FString ResolveError;
	UAnimInstance* AnimInstance = ResolveAnimInstance(ActorTag, MeshComponentName, ResolveError);
	if (!AnimInstance)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("inspect-anim-instance: %s"), *ResolveError));
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("actor_tag"), ActorTag);
	Response->SetStringField(TEXT("anim_instance_class"), AnimInstance->GetClass()->GetPathName());

	if (IncludeSet.Contains(TEXT("state_machines")))
	{
		Response->SetArrayField(TEXT("state_machines"), ReadStateMachines(AnimInstance));
	}
	if (IncludeSet.Contains(TEXT("montages")))
	{
		Response->SetArrayField(TEXT("active_montages"), ReadActiveMontages(AnimInstance));
	}
	if (IncludeSet.Contains(TEXT("notifies")))
	{
		Response->SetArrayField(TEXT("notifies"), ReadNotifies(AnimInstance));
	}
	if (IncludeSet.Contains(TEXT("blend_weights")))
	{
		Response->SetObjectField(TEXT("blend_weights"), ReadBlendWeights(AnimInstance, BlendWeightProps));
	}

	Response->SetArrayField(TEXT("slots"), ReadSlots(AnimInstance));
	return FBridgeToolResult::Json(Response);
}

UAnimInstance* UInspectAnimInstanceTool::ResolveAnimInstance(
	const FString& ActorTag,
	const FString& MeshComponentName,
	FString& OutError)
{
	UWorld* World = FindWorldByType(TEXT("pie"));
	if (!World)
	{
		World = FindWorldByType(TEXT("game"));
	}
	if (!World)
	{
		OutError = TEXT("no PIE or game world available");
		return nullptr;
	}

	const FName TagName(*ActorTag);
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (AActor* Actor = *It; Actor && Actor->Tags.Contains(TagName))
		{
			TargetActor = Actor;
			break;
		}
	}

	if (!TargetActor)
	{
		OutError = FString::Printf(TEXT("actor with tag '%s' not found"), *ActorTag);
		return nullptr;
	}

	USkeletalMeshComponent* SkeletalMesh = nullptr;
	TArray<USkeletalMeshComponent*> Components;
	TargetActor->GetComponents<USkeletalMeshComponent>(Components);
	for (USkeletalMeshComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}
		if (MeshComponentName.IsEmpty()
			|| Component->GetName().Equals(MeshComponentName, ESearchCase::IgnoreCase))
		{
			SkeletalMesh = Component;
			break;
		}
	}

	if (!SkeletalMesh)
	{
		if (MeshComponentName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("actor '%s' has no SkeletalMeshComponent"), *TargetActor->GetName());
		}
		else
		{
			OutError = FString::Printf(TEXT("SkeletalMeshComponent '%s' not found on actor '%s'"), *MeshComponentName, *TargetActor->GetName());
		}
		return nullptr;
	}

	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		OutError = FString::Printf(TEXT("SkeletalMeshComponent '%s' has no active UAnimInstance"), *SkeletalMesh->GetName());
		return nullptr;
	}

	return AnimInstance;
}

TArray<TSharedPtr<FJsonValue>> UInspectAnimInstanceTool::ReadStateMachines(UAnimInstance* AnimInstance)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AnimInstance)
	{
		return Out;
	}

	if (UAnimBlueprintGeneratedClass* AnimClass = Cast<UAnimBlueprintGeneratedClass>(AnimInstance->GetClass()))
	{
		for (int32 MachineIndex = 0; MachineIndex < AnimClass->AnimNodeProperties.Num(); ++MachineIndex)
		{
			const FAnimNode_StateMachine* StateMachine = AnimInstance->GetStateMachineInstance(MachineIndex);
			if (!StateMachine)
			{
				continue;
			}

			const FBakedAnimationStateMachine* MachineDesc =
				AnimClass->BakedStateMachines.IsValidIndex(MachineIndex) ? &AnimClass->BakedStateMachines[MachineIndex] : nullptr;
			if (!MachineDesc)
			{
				continue;
			}

			const int32 CurrentStateIndex = StateMachine->GetCurrentState();
			// FAnimNode_StateMachine transition-query APIs vary across UE versions.
			// Keep this snapshot portable and rely on state/blend data for detail.
			const bool bTransitionActive = false;
			const float TimeInState = StateMachine->GetCurrentStateElapsedTime();

			FString CurrentStateName;
			if (MachineDesc->States.IsValidIndex(CurrentStateIndex))
			{
				CurrentStateName = MachineDesc->States[CurrentStateIndex].StateName.ToString();
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), MachineDesc->MachineName.ToString());
			Entry->SetStringField(TEXT("current_state"), CurrentStateName);
			Entry->SetStringField(TEXT("previous_state"), TEXT(""));
			Entry->SetBoolField(TEXT("transition_active"), bTransitionActive);
			Entry->SetNumberField(TEXT("time_in_state"), TimeInState);
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	return Out;
}

TArray<TSharedPtr<FJsonValue>> UInspectAnimInstanceTool::ReadActiveMontages(UAnimInstance* AnimInstance)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AnimInstance)
	{
		return Out;
	}

	for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
	{
		if (!MontageInstance || !MontageInstance->Montage)
		{
			continue;
		}

		const UAnimMontage* Montage = MontageInstance->Montage;
		const float Length = Montage->GetPlayLength();
		const float Position = MontageInstance->GetPosition();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("montage"), Montage->GetPathName());
		Entry->SetNumberField(TEXT("position"), Position);
		Entry->SetNumberField(TEXT("length"), Length);
		Entry->SetNumberField(TEXT("play_rate"), MontageInstance->GetPlayRate());
		Entry->SetBoolField(TEXT("is_playing"), MontageInstance->IsPlaying());
		Entry->SetBoolField(TEXT("past_end"), Position >= Length - KINDA_SMALL_NUMBER);
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return Out;
}

TArray<TSharedPtr<FJsonValue>> UInspectAnimInstanceTool::ReadSlots(UAnimInstance* AnimInstance)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AnimInstance)
	{
		return Out;
	}

	for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
	{
		if (!MontageInstance || !MontageInstance->Montage)
		{
			continue;
		}

		const UAnimMontage* Montage = MontageInstance->Montage;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());
			Entry->SetStringField(TEXT("montage"), Montage->GetPathName());
			Entry->SetNumberField(TEXT("position"), MontageInstance->GetPosition());
			Entry->SetNumberField(TEXT("length"), Montage->GetPlayLength());
			Entry->SetNumberField(TEXT("play_rate"), MontageInstance->GetPlayRate());
			Entry->SetBoolField(TEXT("is_playing"), MontageInstance->IsPlaying());
			Entry->SetNumberField(TEXT("global_weight"), AnimInstance->GetSlotNodeGlobalWeight(SlotTrack.SlotName));
			Entry->SetNumberField(TEXT("local_weight"), AnimInstance->GetSlotMontageLocalWeight(SlotTrack.SlotName));

			if (SlotTrack.AnimTrack.AnimSegments.Num() > 0 && SlotTrack.AnimTrack.AnimSegments[0].GetAnimReference())
			{
				Entry->SetStringField(TEXT("asset"), SlotTrack.AnimTrack.AnimSegments[0].GetAnimReference()->GetPathName());
			}

			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	return Out;
}

TArray<TSharedPtr<FJsonValue>> UInspectAnimInstanceTool::ReadNotifies(UAnimInstance* AnimInstance)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!AnimInstance)
	{
		return Out;
	}

	for (const FAnimNotifyEventReference& NotifyRef : AnimInstance->NotifyQueue.AnimNotifies)
	{
		const FAnimNotifyEvent* Notify = NotifyRef.GetNotify();
		if (!Notify)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Notify->NotifyName.ToString());
		Entry->SetNumberField(TEXT("time"), Notify->GetTime());
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return Out;
}

TSharedPtr<FJsonObject> UInspectAnimInstanceTool::ReadBlendWeights(
	UAnimInstance* AnimInstance,
	const TArray<FString>& PropertyNames)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!AnimInstance)
	{
		return Out;
	}

	for (const FString& PropertyName : PropertyNames)
	{
		FProperty* Prop = AnimInstance->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop)
		{
			continue;
		}

		if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			Out->SetNumberField(PropertyName, FloatProp->GetPropertyValue_InContainer(AnimInstance));
		}
		else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			Out->SetNumberField(PropertyName, DoubleProp->GetPropertyValue_InContainer(AnimInstance));
		}
	}

	return Out;
}
