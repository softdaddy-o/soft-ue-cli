// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimMontageInspectTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Dom/JsonObject.h"

namespace
{
FBridgeSchemaProperty MontageInspectSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

TSet<FString> ParseIncludeSet(const FString& IncludeText)
{
	TSet<FString> IncludeSet;
	TArray<FString> Parts;
	IncludeText.ParseIntoArray(Parts, TEXT(","), true);
	for (FString Part : Parts)
	{
		Part = Part.TrimStartAndEnd().ToLower();
		if (!Part.IsEmpty())
		{
			IncludeSet.Add(Part);
		}
	}
	return IncludeSet;
}

bool ShouldInclude(const TSet<FString>& IncludeSet, const FString& Section)
{
	return IncludeSet.Num() == 0 || IncludeSet.Contains(Section) || IncludeSet.Contains(TEXT("all"));
}

FString ObjectPathOrEmpty(const UObject* Object)
{
	return Object ? Object->GetPathName() : TEXT("");
}

FString ClassPathOrEmpty(const UObject* Object)
{
	return Object && Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT("");
}

TSharedPtr<FJsonObject> MontageInspectSegmentToJson(const FAnimSegment& Segment)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	const UAnimSequenceBase* Animation = Segment.GetAnimReference();
	Json->SetStringField(TEXT("anim_path"), ObjectPathOrEmpty(Animation));
	Json->SetNumberField(TEXT("start_pos"), Segment.StartPos);
	Json->SetNumberField(TEXT("end_pos"), Segment.GetEndPos());
	Json->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
	Json->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
	Json->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
	Json->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
	Json->SetNumberField(TEXT("length"), Segment.GetLength());
	Json->SetBoolField(TEXT("valid"), Segment.IsValid());
	return Json;
}

TSharedPtr<FJsonObject> NotifyToJson(const FAnimNotifyEvent& NotifyEvent)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());
	Json->SetStringField(TEXT("event_name"), NotifyEvent.GetNotifyEventName().ToString());
	Json->SetStringField(TEXT("notify_class"), ClassPathOrEmpty(NotifyEvent.Notify.Get()));
	Json->SetStringField(TEXT("notify_state_class"), ClassPathOrEmpty(NotifyEvent.NotifyStateClass.Get()));
	Json->SetStringField(TEXT("kind"), NotifyEvent.NotifyStateClass ? TEXT("state") : TEXT("notify"));
	Json->SetNumberField(TEXT("time"), NotifyEvent.GetTriggerTime());
	Json->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());
	Json->SetNumberField(TEXT("end_time"), NotifyEvent.GetEndTriggerTime());
	Json->SetNumberField(TEXT("track_index"), NotifyEvent.TrackIndex);
	Json->SetNumberField(TEXT("trigger_weight_threshold"), NotifyEvent.TriggerWeightThreshold);
	Json->SetNumberField(TEXT("trigger_chance"), NotifyEvent.NotifyTriggerChance);
	Json->SetBoolField(TEXT("branching_point"), NotifyEvent.IsBranchingPoint());
	Json->SetBoolField(TEXT("trigger_on_dedicated_server"), NotifyEvent.bTriggerOnDedicatedServer);
	Json->SetBoolField(TEXT("trigger_on_follower"), NotifyEvent.bTriggerOnFollower);
	return Json;
}
}

FString UAnimMontageInspectTool::GetToolDescription() const
{
	return TEXT("Inspect AnimMontage notifies, notify states, composite sections, next-section links, and slot tracks.");
}

TMap<FString, FBridgeSchemaProperty> UAnimMontageInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), MontageInspectSchemaProperty(TEXT("string"), TEXT("AnimMontage asset path"), true));
	Schema.Add(TEXT("include"), MontageInspectSchemaProperty(TEXT("string"), TEXT("Comma-separated sections: notifies, sections, slots")));
	return Schema;
}

TArray<FString> UAnimMontageInspectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UAnimMontageInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-inspect: asset_path is required"));
	}

	FString Error;
	UAnimMontage* Montage = FBridgeAssetModifier::LoadAssetByPath<UAnimMontage>(AssetPath, Error);
	if (!Montage)
	{
		return FBridgeToolResult::Error(Error);
	}

	const TSet<FString> IncludeSet = ParseIncludeSet(GetStringArgOrDefault(Arguments, TEXT("include"), TEXT("")));
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Montage->GetPathName());
	Result->SetNumberField(TEXT("length"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("notify_count"), Montage->Notifies.Num());
	Result->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
	Result->SetNumberField(TEXT("slot_count"), Montage->SlotAnimTracks.Num());

	if (ShouldInclude(IncludeSet, TEXT("notifies")))
	{
		TArray<TSharedPtr<FJsonValue>> NotifiesJson;
		for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
		{
			NotifiesJson.Add(MakeShared<FJsonValueObject>(NotifyToJson(NotifyEvent)));
		}
		Result->SetArrayField(TEXT("notifies"), NotifiesJson);
	}

	if (ShouldInclude(IncludeSet, TEXT("sections")))
	{
		TArray<TSharedPtr<FJsonValue>> SectionsJson;
		for (int32 Index = 0; Index < Montage->CompositeSections.Num(); ++Index)
		{
			const FCompositeSection& Section = Montage->CompositeSections[Index];
			float StartTime = 0.0f;
			float EndTime = 0.0f;
			Montage->GetSectionStartAndEndTime(Index, StartTime, EndTime);

			TSharedPtr<FJsonObject> SectionJson = MakeShared<FJsonObject>();
			SectionJson->SetNumberField(TEXT("index"), Index);
			SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
			SectionJson->SetNumberField(TEXT("start_time"), StartTime);
			SectionJson->SetNumberField(TEXT("end_time"), EndTime);
			SectionJson->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
			SectionsJson.Add(MakeShared<FJsonValueObject>(SectionJson));
		}
		Result->SetArrayField(TEXT("sections"), SectionsJson);
	}

	if (ShouldInclude(IncludeSet, TEXT("slots")))
	{
		TArray<TSharedPtr<FJsonValue>> SlotsJson;
		for (int32 SlotIndex = 0; SlotIndex < Montage->SlotAnimTracks.Num(); ++SlotIndex)
		{
			const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIndex];
			TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
			SlotJson->SetNumberField(TEXT("index"), SlotIndex);
			SlotJson->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());
			SlotJson->SetNumberField(TEXT("segment_count"), SlotTrack.AnimTrack.AnimSegments.Num());

			TArray<TSharedPtr<FJsonValue>> SegmentsJson;
			for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
			{
				SegmentsJson.Add(MakeShared<FJsonValueObject>(MontageInspectSegmentToJson(Segment)));
			}
			SlotJson->SetArrayField(TEXT("segments"), SegmentsJson);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotJson));
		}
		Result->SetArrayField(TEXT("slots"), SlotsJson);
	}

	return FBridgeToolResult::Json(Result);
}
