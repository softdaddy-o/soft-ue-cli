// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimMontageSlotTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty MontageSlotSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

bool CheckoutMontageIfRequested(UAnimMontage* Montage, bool bCheckout, FString& OutError)
{
	if (!bCheckout || !Montage)
	{
		return true;
	}

	UPackage* Package = Montage->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("anim-montage-set-slot-animation: montage has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	return FBridgeAssetModifier::CheckoutFile(PackageFileName, OutError);
}

double GetNumberArgOrDefaultLocal(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, double DefaultValue)
{
	double Value = DefaultValue;
	if (Arguments.IsValid() && Arguments->TryGetNumberField(FieldName, Value))
	{
		return Value;
	}
	return DefaultValue;
}

FName ResolveSlotName(UAnimMontage* Montage, const FString& SlotNameText)
{
	const FString TrimmedSlotName = SlotNameText.TrimStartAndEnd();
	if (!TrimmedSlotName.IsEmpty())
	{
		return FName(*TrimmedSlotName);
	}

	if (Montage)
	{
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			if (!SlotTrack.SlotName.IsNone())
			{
				return SlotTrack.SlotName;
			}
		}
	}
	return FName(TEXT("DefaultSlot"));
}

FSlotAnimationTrack* FindOrAddSlotTrack(UAnimMontage* Montage, FName SlotName, int32& OutSlotIndex, bool& bOutCreated)
{
	OutSlotIndex = INDEX_NONE;
	bOutCreated = false;
	if (!Montage || SlotName.IsNone())
	{
		return nullptr;
	}

	for (int32 Index = 0; Index < Montage->SlotAnimTracks.Num(); ++Index)
	{
		FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[Index];
		if (SlotTrack.SlotName == SlotName)
		{
			OutSlotIndex = Index;
			return &SlotTrack;
		}
	}

	FSlotAnimationTrack& NewTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	NewTrack.SlotName = SlotName;
	OutSlotIndex = Montage->SlotAnimTracks.Num() - 1;
	bOutCreated = true;
	return &NewTrack;
}

bool ResolveSectionStart(UAnimMontage* Montage, const FString& SectionText, float& OutStartTime, FString& OutError)
{
	OutStartTime = 0.0f;
	const FString TrimmedSection = SectionText.TrimStartAndEnd();
	if (TrimmedSection.IsEmpty())
	{
		return true;
	}
	if (!Montage)
	{
		OutError = TEXT("anim-montage-set-slot-animation: montage is required to resolve a section");
		return false;
	}

	const FName SectionName(*TrimmedSection);
	const int32 SectionIndex = Montage->GetSectionIndex(SectionName);
	if (SectionIndex == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("anim-montage-set-slot-animation: section '%s' not found"),
			*SectionName.ToString());
		return false;
	}

	float SectionEnd = 0.0f;
	Montage->GetSectionStartAndEndTime(SectionIndex, OutStartTime, SectionEnd);
	return true;
}

FAnimSegment BuildSegment(
	UAnimSequenceBase* Animation,
	float SegmentStartPos,
	float AnimStartTime,
	float AnimEndTime,
	float PlayRate,
	int32 LoopingCount)
{
	FAnimSegment Segment;
	Segment.SetAnimReference(Animation, true);
	Segment.StartPos = SegmentStartPos;
	Segment.AnimStartTime = AnimStartTime;
	Segment.AnimEndTime = AnimEndTime;
	Segment.AnimPlayRate = PlayRate;
	Segment.LoopingCount = LoopingCount;
	return Segment;
}

TSharedPtr<FJsonObject> MontageSlotSegmentToJson(const FAnimSegment& Segment)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(
		TEXT("anim_path"),
		Segment.GetAnimReference() ? Segment.GetAnimReference()->GetPathName() : TEXT(""));
	Json->SetNumberField(TEXT("start_pos"), Segment.StartPos);
	Json->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
	Json->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
	Json->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
	Json->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
	Json->SetNumberField(TEXT("length"), Segment.GetLength());
	return Json;
}
}

FString UAnimMontageSetSlotAnimationTool::GetToolDescription() const
{
	return TEXT("Set or add a single AnimSequenceBase segment on an AnimMontage slot track.");
}

TMap<FString, FBridgeSchemaProperty> UAnimMontageSetSlotAnimationTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), MontageSlotSchemaProperty(TEXT("string"), TEXT("AnimMontage asset path"), true));
	Schema.Add(TEXT("anim_path"), MontageSlotSchemaProperty(TEXT("string"), TEXT("AnimSequenceBase asset path to assign"), true));
	Schema.Add(TEXT("slot_name"), MontageSlotSchemaProperty(TEXT("string"), TEXT("Slot name to update; defaults to first existing slot or DefaultSlot")));
	Schema.Add(TEXT("section"), MontageSlotSchemaProperty(TEXT("string"), TEXT("Optional existing montage section whose start time anchors the segment")));
	Schema.Add(TEXT("start_time"), MontageSlotSchemaProperty(TEXT("number"), TEXT("Start time inside the animation asset")));
	Schema.Add(TEXT("end_time"), MontageSlotSchemaProperty(TEXT("number"), TEXT("End time inside the animation asset")));
	Schema.Add(TEXT("play_rate"), MontageSlotSchemaProperty(TEXT("number"), TEXT("Segment play rate")));
	Schema.Add(TEXT("looping_count"), MontageSlotSchemaProperty(TEXT("integer"), TEXT("Segment loop count")));
	Schema.Add(TEXT("save"), MontageSlotSchemaProperty(TEXT("boolean"), TEXT("Save the montage after mutation")));
	Schema.Add(TEXT("checkout"), MontageSlotSchemaProperty(TEXT("boolean"), TEXT("Checkout the montage before mutation")));
	return Schema;
}

TArray<FString> UAnimMontageSetSlotAnimationTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("anim_path") };
}

FBridgeToolResult UAnimMontageSetSlotAnimationTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	const FString AnimPath = GetStringArgOrDefault(Arguments, TEXT("anim_path"), TEXT(""));
	if (AssetPath.IsEmpty() || AnimPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-set-slot-animation: asset_path and anim_path are required"));
	}

	FString Error;
	UAnimMontage* Montage = FBridgeAssetModifier::LoadAssetByPath<UAnimMontage>(AssetPath, Error);
	if (!Montage)
	{
		return FBridgeToolResult::Error(Error);
	}

	UAnimSequenceBase* Animation = FBridgeAssetModifier::LoadAssetByPath<UAnimSequenceBase>(AnimPath, Error);
	if (!Animation)
	{
		return FBridgeToolResult::Error(Error);
	}
	if (!Animation->CanBeUsedInComposition())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("anim-montage-set-slot-animation: animation '%s' cannot be used in a montage composition"),
			*Animation->GetPathName()));
	}

	const FName SlotName = ResolveSlotName(Montage, GetStringArgOrDefault(Arguments, TEXT("slot_name"), TEXT("")));
	if (SlotName.IsNone())
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-set-slot-animation: slot_name resolved to None"));
	}

	float SegmentStartPos = 0.0f;
	const FString SectionText = GetStringArgOrDefault(Arguments, TEXT("section"), TEXT(""));
	if (!ResolveSectionStart(Montage, SectionText, SegmentStartPos, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const float AnimationLength = Animation->GetPlayLength();
	const float AnimStartTime = FMath::Clamp(
		static_cast<float>(GetNumberArgOrDefaultLocal(Arguments, TEXT("start_time"), 0.0)),
		0.0f,
		AnimationLength);
	const float AnimEndTime = FMath::Clamp(
		static_cast<float>(GetNumberArgOrDefaultLocal(Arguments, TEXT("end_time"), AnimationLength)),
		AnimStartTime,
		AnimationLength);
	const float PlayRate = static_cast<float>(GetNumberArgOrDefaultLocal(Arguments, TEXT("play_rate"), 1.0));
	const int32 LoopingCount = FMath::Max(1, static_cast<int32>(GetNumberArgOrDefaultLocal(Arguments, TEXT("looping_count"), 1.0)));
	if (FMath::IsNearlyZero(PlayRate))
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-set-slot-animation: play_rate must not be zero"));
	}
	if (AnimEndTime <= AnimStartTime)
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-set-slot-animation: end_time must be greater than start_time"));
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bCheckout = GetBoolArgOrDefault(Arguments, TEXT("checkout"), false);
	if (!CheckoutMontageIfRequested(Montage, bCheckout, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	int32 SlotIndex = INDEX_NONE;
	bool bCreatedSlot = false;
	FSlotAnimationTrack* SlotTrack = FindOrAddSlotTrack(Montage, SlotName, SlotIndex, bCreatedSlot);
	if (!SlotTrack)
	{
		return FBridgeToolResult::Error(TEXT("anim-montage-set-slot-animation: failed to resolve slot track"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(
		FText::FromString(TEXT("Set Anim Montage Slot Animation")));
	FBridgeAssetModifier::MarkModified(Montage);

	const int32 ReplacedSegmentCount = SlotTrack->AnimTrack.AnimSegments.Num();
	SlotTrack->AnimTrack.AnimSegments.Reset();
	SlotTrack->AnimTrack.AnimSegments.Add(BuildSegment(
		Animation,
		SegmentStartPos,
		AnimStartTime,
		AnimEndTime,
		PlayRate,
		LoopingCount));
	const FAnimSegment ResultSegment = SlotTrack->AnimTrack.AnimSegments[0];

	Montage->UpdateLinkableElements(SlotIndex, 0);
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	FBridgeAssetModifier::MarkPackageDirty(Montage);

	if (bSave && !FBridgeAssetModifier::SaveAsset(Montage, false, Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Montage->GetPathName());
	Result->SetStringField(TEXT("anim_path"), Animation->GetPathName());
	Result->SetStringField(TEXT("slot_name"), SlotName.ToString());
	Result->SetStringField(TEXT("section"), SectionText.TrimStartAndEnd());
	Result->SetBoolField(TEXT("created_slot"), bCreatedSlot);
	Result->SetNumberField(TEXT("replaced_segment_count"), ReplacedSegmentCount);
	Result->SetNumberField(TEXT("sequence_length"), Montage->GetPlayLength());
	Result->SetObjectField(TEXT("segment"), MontageSlotSegmentToJson(ResultSegment));
	Result->SetBoolField(TEXT("saved"), bSave);
	return FBridgeToolResult::Json(Result);
}
