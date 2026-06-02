// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindHelper.h"
#include "SoftUEBridgeEditorModule.h"
#include "IRewindDebugger.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Dom/JsonValue.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"

// ── Static state ───────────────────────────────────────────────────────────
bool FRewindHelper::bRecordingActive = false;
bool FRewindHelper::bLoadedFromFile = false;
FString FRewindHelper::ActiveTraceFile;
TArray<FString> FRewindHelper::ActiveChannels;
TArray<FString> FRewindHelper::ActiveActorFilters;
double FRewindHelper::RecordingStartTime = 0.0;

// ── Channel mapping ─────────────────────────────────────────────────────────
namespace
{
	const FName GAnimationProviderName(TEXT("AnimationProvider"));
	const FName GGameplayProviderName(TEXT("GameplayProvider"));

	struct FChannelMapEntry
	{
		const TCHAR* CliName;
		const TCHAR* TraceName;
	};

	static const FChannelMapEntry GChannelMap[] =
	{
		{ TEXT("skeletal-mesh"), TEXT("SkeletalMeshTrace") },
		{ TEXT("montage"),      TEXT("MontageTrace")      },
		{ TEXT("anim-state"),   TEXT("AnimationTrace")     },
		{ TEXT("notify"),       TEXT("AnimNotifyTrace")    },
		{ TEXT("object"),       TEXT("ObjectTrace")        },
	};

	FString SafeName(const TCHAR* Name)
	{
		return Name ? FString(Name) : FString();
	}

	FString UInt64ToString(uint64 Value)
	{
		return FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Value));
	}

	FString AnimGraphPhaseToString(EAnimGraphPhase Phase)
	{
		switch (Phase)
		{
		case EAnimGraphPhase::Initialize:
			return TEXT("initialize");
		case EAnimGraphPhase::PreUpdate:
			return TEXT("pre_update");
		case EAnimGraphPhase::Update:
			return TEXT("update");
		case EAnimGraphPhase::CacheBones:
			return TEXT("cache_bones");
		case EAnimGraphPhase::Evaluate:
			return TEXT("evaluate");
		default:
			return TEXT("unknown");
		}
	}

	FString NotifyTypeToString(EAnimNotifyMessageType Type)
	{
		switch (Type)
		{
		case EAnimNotifyMessageType::Event:
			return TEXT("event");
		case EAnimNotifyMessageType::Begin:
			return TEXT("begin");
		case EAnimNotifyMessageType::End:
			return TEXT("end");
		case EAnimNotifyMessageType::Tick:
			return TEXT("tick");
		case EAnimNotifyMessageType::SyncMarker:
			return TEXT("sync_marker");
		default:
			return TEXT("unknown");
		}
	}

	bool MatchesToken(const FString& Value, const FString& A, const FString& B = FString())
	{
		return Value.Equals(A, ESearchCase::IgnoreCase) ||
			(!B.IsEmpty() && Value.Equals(B, ESearchCase::IgnoreCase));
	}

	bool IncludesSection(const TArray<FString>& Sections, const FString& A, const FString& B = FString())
	{
		if (Sections.Num() == 0)
		{
			return true;
		}
		for (const FString& Section : Sections)
		{
			if (MatchesToken(Section, A, B))
			{
				return true;
			}
		}
		return false;
	}

	void AddObjectInfoFields(TSharedPtr<FJsonObject> Json, const FString& Prefix, uint64 ObjectId, const IGameplayProvider* GameplayProvider)
	{
		Json->SetStringField(Prefix + TEXT("_id"), UInt64ToString(ObjectId));
		if (!GameplayProvider || ObjectId == 0)
		{
			return;
		}

		if (const FObjectInfo* ObjectInfo = GameplayProvider->FindObjectInfo(ObjectId))
		{
			Json->SetStringField(Prefix + TEXT("_name"), SafeName(ObjectInfo->Name));
			Json->SetStringField(Prefix + TEXT("_path"), SafeName(ObjectInfo->PathName));
			if (const FClassInfo* ClassInfo = GameplayProvider->FindClassInfo(ObjectInfo->ClassId))
			{
				Json->SetStringField(Prefix + TEXT("_class"), SafeName(ClassInfo->Name));
				Json->SetStringField(Prefix + TEXT("_class_path"), SafeName(ClassInfo->PathName));
			}
		}
	}

	void CollectCandidateObjectIdsRecursive(const TSharedPtr<FDebugObjectInfo>& Info, TSet<uint64>& OutObjectIds)
	{
		if (!Info.IsValid())
		{
			return;
		}

		if (Info->Id.IsSet())
		{
			OutObjectIds.Add(Info->Id.GetMainId());
		}

		for (const TSharedPtr<FDebugObjectInfo>& Child : Info->Children)
		{
			CollectCandidateObjectIdsRecursive(Child, OutObjectIds);
		}
	}

	void CollectGameplaySubobjectIds(const IGameplayProvider* GameplayProvider, uint64 ObjectId, TSet<uint64>& OutObjectIds)
	{
		if (!GameplayProvider || ObjectId == 0)
		{
			return;
		}

		GameplayProvider->EnumerateSubobjects(RewindDebugger::FObjectId(ObjectId),
			[&](const RewindDebugger::FObjectId& SubObjectId)
			{
				if (!SubObjectId.IsSet())
				{
					return;
				}

				const uint64 SubObjectMainId = SubObjectId.GetMainId();
				if (!OutObjectIds.Contains(SubObjectMainId))
				{
					OutObjectIds.Add(SubObjectMainId);
					CollectGameplaySubobjectIds(GameplayProvider, SubObjectMainId, OutObjectIds);
				}
			});
	}

	TArray<uint64> CollectCandidateObjectIds(const TSharedPtr<FDebugObjectInfo>& TargetObj, const IGameplayProvider* GameplayProvider)
	{
		TSet<uint64> ObjectIds;
		CollectCandidateObjectIdsRecursive(TargetObj, ObjectIds);

		TArray<uint64> InitialIds;
		for (uint64 ObjectId : ObjectIds)
		{
			InitialIds.Add(ObjectId);
		}
		for (uint64 ObjectId : InitialIds)
		{
			CollectGameplaySubobjectIds(GameplayProvider, ObjectId, ObjectIds);
		}

		TArray<uint64> Result;
		for (uint64 ObjectId : ObjectIds)
		{
			Result.Add(ObjectId);
		}
		Result.Sort();
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> ObjectIdsToJson(const TArray<uint64>& ObjectIds)
	{
		TArray<TSharedPtr<FJsonValue>> JsonIds;
		for (uint64 ObjectId : ObjectIds)
		{
			JsonIds.Add(MakeShareable(new FJsonValueString(UInt64ToString(ObjectId))));
		}
		return JsonIds;
	}

	bool ResolveFrameWindow(
		const TraceServices::IAnalysisSession& Session,
		double Time,
		double& OutStartTime,
		double& OutEndTime,
		uint64& OutFrameIndex)
	{
		OutStartTime = FMath::Max(0.0, Time - (1.0 / 120.0));
		OutEndTime = Time + (1.0 / 120.0);
		OutFrameIndex = 0;

		const TraceServices::IFrameProvider& FramesProvider = TraceServices::ReadFrameProvider(Session);
		TraceServices::FFrame Frame;
		if (FramesProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, Time, Frame))
		{
			OutStartTime = Frame.StartTime;
			OutEndTime = Frame.EndTime > Frame.StartTime ? Frame.EndTime : Frame.StartTime + (1.0 / 60.0);
			OutFrameIndex = Frame.Index;
			return true;
		}

		return false;
	}

	void AppendStateMachineEvents(
		const IAnimationProvider* AnimationProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadStateMachinesTimeline(ObjectId,
			[&](const IAnimationProvider::StateMachinesTimeline& Timeline)
			{
				Timeline.EnumerateEvents(StartTime, EndTime,
					[&](double EventStartTime, double EventEndTime, uint32 Depth, const FAnimStateMachineMessage& Message)
					{
						TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
						Item->SetStringField(TEXT("anim_instance_id"), UInt64ToString(ObjectId));
						Item->SetNumberField(TEXT("node_id"), Message.NodeId);
						Item->SetNumberField(TEXT("state_machine_index"), Message.StateMachineIndex);
						Item->SetNumberField(TEXT("state_index"), Message.StateIndex);
						Item->SetNumberField(TEXT("state_weight"), Message.StateWeight);
						Item->SetNumberField(TEXT("elapsed_time"), Message.ElapsedTime);
						Item->SetNumberField(TEXT("event_start_time"), EventStartTime);
						Item->SetNumberField(TEXT("event_end_time"), EventEndTime);
						Item->SetNumberField(TEXT("depth"), static_cast<int32>(Depth));
						OutEvents.Add(MakeShareable(new FJsonValueObject(Item)));
						return TraceServices::EEventEnumerate::Continue;
					});
			});
	}

	void AppendMontageEvents(
		const IAnimationProvider* AnimationProvider,
		const IGameplayProvider* GameplayProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadMontageTimeline(ObjectId,
			[&](const IAnimationProvider::AnimMontageTimeline& Timeline)
			{
				Timeline.EnumerateEvents(StartTime, EndTime,
					[&](double EventStartTime, double EventEndTime, uint32 Depth, const FAnimMontageMessage& Message)
					{
						TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
						Item->SetStringField(TEXT("anim_instance_id"), UInt64ToString(ObjectId));
						AddObjectInfoFields(Item, TEXT("montage"), Message.MontageId, GameplayProvider);
						Item->SetNumberField(TEXT("weight"), Message.Weight);
						Item->SetNumberField(TEXT("desired_weight"), Message.DesiredWeight);
						Item->SetNumberField(TEXT("position"), Message.Position);
						Item->SetNumberField(TEXT("frame_counter"), Message.FrameCounter);
						Item->SetStringField(TEXT("current_section"), SafeName(AnimationProvider->GetName(Message.CurrentSectionNameId)));
						Item->SetStringField(TEXT("next_section"), SafeName(AnimationProvider->GetName(Message.NextSectionNameId)));
						Item->SetNumberField(TEXT("event_start_time"), EventStartTime);
						Item->SetNumberField(TEXT("event_end_time"), EventEndTime);
						Item->SetNumberField(TEXT("depth"), static_cast<int32>(Depth));
						OutEvents.Add(MakeShareable(new FJsonValueObject(Item)));
						return TraceServices::EEventEnumerate::Continue;
					});
			});
	}

	void AppendNotifyEventsFromTimeline(
		const IAnimationProvider* AnimationProvider,
		const IGameplayProvider* GameplayProvider,
		uint64 ObjectId,
		const IAnimationProvider::AnimNotifyTimeline& Timeline,
		bool bStateTimeline,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		Timeline.EnumerateEvents(StartTime, EndTime,
			[&](double EventStartTime, double EventEndTime, uint32 Depth, const FAnimNotifyMessage& Message)
			{
				TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
				Item->SetStringField(TEXT("anim_instance_id"), UInt64ToString(ObjectId));
				Item->SetStringField(TEXT("name"), SafeName(AnimationProvider ? AnimationProvider->GetName(Message.NameId) : Message.Name));
				Item->SetStringField(TEXT("type"), NotifyTypeToString(Message.NotifyEventType));
				Item->SetBoolField(TEXT("state_timeline"), bStateTimeline);
				AddObjectInfoFields(Item, TEXT("asset"), Message.AssetId, GameplayProvider);
				AddObjectInfoFields(Item, TEXT("notify"), Message.NotifyId, GameplayProvider);
				Item->SetNumberField(TEXT("time"), Message.Time);
				Item->SetNumberField(TEXT("duration"), Message.Duration);
				Item->SetNumberField(TEXT("recording_time"), Message.RecordingTime);
				Item->SetNumberField(TEXT("event_start_time"), EventStartTime);
				Item->SetNumberField(TEXT("event_end_time"), EventEndTime);
				Item->SetNumberField(TEXT("depth"), static_cast<int32>(Depth));
				OutEvents.Add(MakeShareable(new FJsonValueObject(Item)));
				return TraceServices::EEventEnumerate::Continue;
			});
	}

	void AppendNotifyEvents(
		const IAnimationProvider* AnimationProvider,
		const IGameplayProvider* GameplayProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadNotifyTimeline(ObjectId,
			[&](const IAnimationProvider::AnimNotifyTimeline& Timeline)
			{
				AppendNotifyEventsFromTimeline(AnimationProvider, GameplayProvider, ObjectId, Timeline, false, StartTime, EndTime, OutEvents);
			});

		AnimationProvider->EnumerateNotifyStateTimelines(ObjectId,
			[&](uint64 NotifyNameId, const IAnimationProvider::AnimNotifyTimeline& Timeline)
			{
				AppendNotifyEventsFromTimeline(AnimationProvider, GameplayProvider, ObjectId, Timeline, true, StartTime, EndTime, OutEvents);
			});
	}

	void AppendBlendWeightEvents(
		const IAnimationProvider* AnimationProvider,
		const IGameplayProvider* GameplayProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutEvents)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadTickRecordTimeline(ObjectId,
			[&](const IAnimationProvider::TickRecordTimeline& Timeline)
			{
				Timeline.EnumerateEvents(StartTime, EndTime,
					[&](double EventStartTime, double EventEndTime, uint32 Depth, const FTickRecordMessage& Message)
					{
						TSharedPtr<FJsonObject> Item = MakeShareable(new FJsonObject);
						Item->SetStringField(TEXT("anim_instance_id"), UInt64ToString(Message.AnimInstanceId != 0 ? Message.AnimInstanceId : ObjectId));
						Item->SetStringField(TEXT("component_id"), UInt64ToString(Message.ComponentId));
						AddObjectInfoFields(Item, TEXT("asset"), Message.AssetId, GameplayProvider);
						Item->SetNumberField(TEXT("node_id"), Message.NodeId);
						Item->SetNumberField(TEXT("blend_weight"), Message.BlendWeight);
						Item->SetNumberField(TEXT("playback_time"), Message.PlaybackTime);
						Item->SetNumberField(TEXT("root_motion_weight"), Message.RootMotionWeight);
						Item->SetNumberField(TEXT("play_rate"), Message.PlayRate);
						Item->SetNumberField(TEXT("blend_space_position_x"), Message.BlendSpacePositionX);
						Item->SetNumberField(TEXT("blend_space_position_y"), Message.BlendSpacePositionY);
						Item->SetNumberField(TEXT("blend_space_filtered_position_x"), Message.BlendSpaceFilteredPositionX);
						Item->SetNumberField(TEXT("blend_space_filtered_position_y"), Message.BlendSpaceFilteredPositionY);
						Item->SetNumberField(TEXT("frame_counter"), Message.FrameCounter);
						Item->SetBoolField(TEXT("looping"), Message.bLooping);
						Item->SetBoolField(TEXT("is_blend_space"), Message.bIsBlendSpace);
						Item->SetNumberField(TEXT("event_start_time"), EventStartTime);
						Item->SetNumberField(TEXT("event_end_time"), EventEndTime);
						Item->SetNumberField(TEXT("depth"), static_cast<int32>(Depth));
						OutEvents.Add(MakeShareable(new FJsonValueObject(Item)));
						return TraceServices::EEventEnumerate::Continue;
					});
			});
	}

	TSharedPtr<FJsonObject> BuildAnimGraphNodeJson(
		int32 NodeId,
		const TMap<int32, TSharedPtr<FJsonObject>>& NodesById,
		const TMap<int32, TArray<int32>>& ChildrenByParent)
	{
		const TSharedPtr<FJsonObject>* FoundNode = NodesById.Find(NodeId);
		if (!FoundNode || !FoundNode->IsValid())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Node = *FoundNode;
		TArray<TSharedPtr<FJsonValue>> ChildrenJson;
		if (const TArray<int32>* ChildIds = ChildrenByParent.Find(NodeId))
		{
			for (int32 ChildId : *ChildIds)
			{
				TSharedPtr<FJsonObject> ChildJson = BuildAnimGraphNodeJson(ChildId, NodesById, ChildrenByParent);
				if (ChildJson.IsValid())
				{
					ChildrenJson.Add(MakeShareable(new FJsonValueObject(ChildJson)));
				}
			}
		}
		Node->SetArrayField(TEXT("children"), ChildrenJson);
		return Node;
	}

	void AppendAnimGraphEvents(
		const IAnimationProvider* AnimationProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TArray<TSharedPtr<FJsonValue>>& OutGraphs,
		TArray<TSharedPtr<FJsonValue>>& OutFlatNodes)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadAnimGraphTimeline(ObjectId,
			[&](const IAnimationProvider::AnimGraphTimeline& GraphTimeline)
			{
				GraphTimeline.EnumerateEvents(StartTime, EndTime,
					[&](double GraphStartTime, double GraphEndTime, uint32 GraphDepth, const FAnimGraphMessage& GraphMessage)
					{
						if (GraphMessage.Phase != EAnimGraphPhase::Update)
						{
							return TraceServices::EEventEnumerate::Continue;
						}

						TMap<int32, TSharedPtr<FJsonObject>> NodesById;
						TMap<int32, int32> ParentByNode;
						TMap<int32, TArray<int32>> ChildrenByParent;
						TMap<int32, TArray<TSharedPtr<FJsonValue>>> ValuesByNode;
						TArray<int32> NodeOrder;

						AnimationProvider->ReadAnimNodesTimeline(ObjectId,
							[&](const IAnimationProvider::AnimNodesTimeline& NodesTimeline)
							{
								NodesTimeline.EnumerateEvents(GraphStartTime, GraphEndTime,
									[&](double NodeStartTime, double NodeEndTime, uint32 NodeDepth, const FAnimNodeMessage& NodeMessage)
									{
										if (NodeMessage.NodeId == INDEX_NONE || NodeMessage.Phase != EAnimGraphPhase::Update)
										{
											return TraceServices::EEventEnumerate::Continue;
										}

										if (!NodesById.Contains(NodeMessage.NodeId))
										{
											TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);
											NodeJson->SetNumberField(TEXT("node_id"), NodeMessage.NodeId);
											NodeJson->SetNumberField(TEXT("parent_node_id"), NodeMessage.PreviousNodeId);
											NodeJson->SetStringField(TEXT("name"), SafeName(NodeMessage.NodeName));
											NodeJson->SetStringField(TEXT("type"), SafeName(NodeMessage.NodeTypeName));
											NodeJson->SetNumberField(TEXT("weight"), NodeMessage.Weight);
											NodeJson->SetNumberField(TEXT("root_motion_weight"), NodeMessage.RootMotionWeight);
											NodeJson->SetNumberField(TEXT("frame_counter"), NodeMessage.FrameCounter);
											NodeJson->SetNumberField(TEXT("event_start_time"), NodeStartTime);
											NodeJson->SetNumberField(TEXT("event_end_time"), NodeEndTime);
											NodeJson->SetNumberField(TEXT("depth"), static_cast<int32>(NodeDepth));
											NodesById.Add(NodeMessage.NodeId, NodeJson);
											NodeOrder.Add(NodeMessage.NodeId);
										}

										if (NodeMessage.PreviousNodeId != INDEX_NONE)
										{
											ParentByNode.Add(NodeMessage.NodeId, NodeMessage.PreviousNodeId);
											ChildrenByParent.FindOrAdd(NodeMessage.PreviousNodeId).Add(NodeMessage.NodeId);
										}
										return TraceServices::EEventEnumerate::Continue;
									});
							});

						AnimationProvider->ReadAnimNodeValuesTimeline(ObjectId,
							[&](const IAnimationProvider::AnimNodeValuesTimeline& ValuesTimeline)
							{
								ValuesTimeline.EnumerateEvents(GraphStartTime, GraphEndTime,
									[&](double ValueStartTime, double ValueEndTime, uint32 ValueDepth, const FAnimNodeValueMessage& ValueMessage)
									{
										if (NodesById.Contains(ValueMessage.NodeId))
										{
											TSharedPtr<FJsonObject> ValueJson = MakeShareable(new FJsonObject);
											ValueJson->SetStringField(TEXT("key"), SafeName(ValueMessage.Key));
											ValueJson->SetStringField(TEXT("value"), AnimationProvider->FormatNodeValue(ValueMessage).ToString());
											ValueJson->SetNumberField(TEXT("event_start_time"), ValueStartTime);
											ValueJson->SetNumberField(TEXT("event_end_time"), ValueEndTime);
											ValueJson->SetNumberField(TEXT("depth"), static_cast<int32>(ValueDepth));
											ValuesByNode.FindOrAdd(ValueMessage.NodeId).Add(MakeShareable(new FJsonValueObject(ValueJson)));
										}
										return TraceServices::EEventEnumerate::Continue;
									});
							});

						TArray<TSharedPtr<FJsonValue>> RootNodes;
						for (int32 NodeId : NodeOrder)
						{
							NodesById[NodeId]->SetArrayField(TEXT("values"), ValuesByNode.FindRef(NodeId));
							OutFlatNodes.Add(MakeShareable(new FJsonValueObject(NodesById[NodeId])));

							const int32* ParentNodeId = ParentByNode.Find(NodeId);
							if (!ParentNodeId || !NodesById.Contains(*ParentNodeId))
							{
								TSharedPtr<FJsonObject> RootNodeJson = BuildAnimGraphNodeJson(NodeId, NodesById, ChildrenByParent);
								if (RootNodeJson.IsValid())
								{
									RootNodes.Add(MakeShareable(new FJsonValueObject(RootNodeJson)));
								}
							}
						}

						TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);
						GraphJson->SetStringField(TEXT("anim_instance_id"), UInt64ToString(ObjectId));
						GraphJson->SetStringField(TEXT("phase"), AnimGraphPhaseToString(GraphMessage.Phase));
						GraphJson->SetNumberField(TEXT("node_count"), GraphMessage.NodeCount);
						GraphJson->SetNumberField(TEXT("frame_counter"), GraphMessage.FrameCounter);
						GraphJson->SetNumberField(TEXT("event_start_time"), GraphStartTime);
						GraphJson->SetNumberField(TEXT("event_end_time"), GraphEndTime);
						GraphJson->SetNumberField(TEXT("depth"), static_cast<int32>(GraphDepth));
						GraphJson->SetArrayField(TEXT("roots"), RootNodes);
						OutGraphs.Add(MakeShareable(new FJsonValueObject(GraphJson)));
						return TraceServices::EEventEnumerate::Continue;
					});
			});
	}

	void AppendCurveEvents(
		const IAnimationProvider* AnimationProvider,
		uint64 ObjectId,
		double StartTime,
		double EndTime,
		TSharedPtr<FJsonObject> CurvesJson)
	{
		if (!AnimationProvider)
		{
			return;
		}

		AnimationProvider->ReadSkeletalMeshPoseTimeline(ObjectId,
			[&](const IAnimationProvider::SkeletalMeshPoseTimeline& Timeline, bool bHasCurves)
			{
				if (!bHasCurves)
				{
					return;
				}

				Timeline.EnumerateEvents(StartTime, EndTime,
					[&](double EventStartTime, double EventEndTime, uint32 Depth, const FSkeletalMeshPoseMessage& Message)
					{
						AnimationProvider->EnumerateSkeletalMeshCurves(Message,
							[&](const FSkeletalMeshNamedCurve& Curve)
							{
								CurvesJson->SetNumberField(SafeName(AnimationProvider->GetName(Curve.Id)), Curve.Value);
							});
						return TraceServices::EEventEnumerate::Continue;
					});
			});
	}

	TSharedPtr<FDebugObjectInfo> FindObjectByTagRecursive(
		const TSharedPtr<FDebugObjectInfo>& Info,
		const FString& ActorTag)
	{
		if (!Info.IsValid())
		{
			return nullptr;
		}

		if (Info->ObjectName.Equals(ActorTag, ESearchCase::IgnoreCase) ||
			Info->ObjectName.Contains(ActorTag, ESearchCase::IgnoreCase))
		{
			return Info;
		}

		for (const TSharedPtr<FDebugObjectInfo>& Child : Info->Children)
		{
			TSharedPtr<FDebugObjectInfo> FoundChild = FindObjectByTagRecursive(Child, ActorTag);
			if (FoundChild.IsValid())
			{
				return FoundChild;
			}
		}

		return nullptr;
	}
}

FString FRewindHelper::MapChannelName(const FString& CliName)
{
	for (const FChannelMapEntry& Entry : GChannelMap)
	{
		if (CliName.Equals(Entry.CliName, ESearchCase::IgnoreCase))
		{
			return FString(Entry.TraceName);
		}
	}
	return CliName;
}

TArray<FString> FRewindHelper::GetAllChannelNames()
{
	TArray<FString> Names;
	for (const FChannelMapEntry& Entry : GChannelMap)
	{
		Names.Add(FString(Entry.CliName));
	}
	return Names;
}

// ── Rewind Debugger accessor ────────────────────────────────────────────────
IRewindDebugger* FRewindHelper::GetRewindDebugger()
{
	return IRewindDebugger::Instance();
}

// ── Convenience accessors using actual IRewindDebugger API ──────────────────
double FRewindHelper::GetRecordingStartTime()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return 0.0;
	}
	const TRange<double>& Range = Debugger->GetCurrentTraceRange();
	return Range.HasLowerBound() ? Range.GetLowerBoundValue() : 0.0;
}

double FRewindHelper::GetRecordingEndTime()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return 0.0;
	}
	const TRange<double>& Range = Debugger->GetCurrentTraceRange();
	if (Range.HasUpperBound())
	{
		return Range.GetUpperBoundValue();
	}
	// Fallback: use recording duration
	return Debugger->GetRecordingDuration();
}

double FRewindHelper::FrameToTime(int32 Frame, double FrameRate)
{
	double Start = GetRecordingStartTime();
	return Start + (Frame / FrameRate);
}

// ── Recording state queries ─────────────────────────────────────────────────
bool FRewindHelper::IsRecording()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger && Debugger->IsRecording())
	{
		return true;
	}
	return bRecordingActive;
}

double FRewindHelper::GetRecordingDuration()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger)
	{
		return Debugger->GetRecordingDuration();
	}
	if (bRecordingActive)
	{
		return FPlatformTime::Seconds() - RecordingStartTime;
	}
	return 0.0;
}

bool FRewindHelper::HasData()
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (Debugger)
	{
		// Check if debugger has objects or is recording/loaded
		TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();
		if (Objects.Num() > 0)
		{
			return true;
		}
		if (Debugger->IsRecording() || Debugger->IsTraceFileLoaded())
		{
			return true;
		}
	}
	return bRecordingActive || bLoadedFromFile;
}

// ── Recording control ───────────────────────────────────────────────────────
FString FRewindHelper::StartRecording(
	const TArray<FString>& Channels,
	const TArray<FString>& ActorTags,
	const FString& FilePath)
{
	// Check both our flag and the debugger's state
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (bRecordingActive || (Debugger && Debugger->IsRecording()))
	{
		return TEXT("Already recording. Stop current recording first.");
	}

	// Try using the IRewindDebugger's own recording API first
	if (Debugger && Debugger->CanStartRecording())
	{
		Debugger->StartRecording();
		bRecordingActive = true;
		bLoadedFromFile = false;
		ActiveChannels = Channels;
		ActiveActorFilters = ActorTags;
		ActiveTraceFile = FilePath;
		RecordingStartTime = FPlatformTime::Seconds();
		return FString();
	}

	// Fallback: start via console command
	TArray<FString> ResolvedChannels;
	if (Channels.Num() == 0)
	{
		ResolvedChannels = { TEXT("SkeletalMeshTrace"), TEXT("AnimationTrace"), TEXT("ObjectTrace") };
	}
	else
	{
		for (const FString& Ch : Channels)
		{
			ResolvedChannels.Add(MapChannelName(Ch));
		}
	}

	FString TraceFilePath = FilePath;
	if (TraceFilePath.IsEmpty())
	{
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		FString TraceDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Rewind"));
		IFileManager::Get().MakeDirectory(*TraceDir, true);
		TraceFilePath = FPaths::Combine(TraceDir, FString::Printf(TEXT("Rewind_%s.utrace"), *Timestamp));
	}

	FString ChannelString = FString::Join(ResolvedChannels, TEXT(","));
	FString Command = FString::Printf(TEXT("Trace.Start \"%s\" %s"), *TraceFilePath, *ChannelString);

	UE_LOG(LogSoftUEBridgeEditor, Log,
		TEXT("RewindHelper: Starting recording – channels: %s, file: %s"),
		*ChannelString, *TraceFilePath);

	if (GEngine && GEngine->Exec(nullptr, *Command))
	{
		bRecordingActive = true;
		bLoadedFromFile = false;
		ActiveTraceFile = TraceFilePath;
		ActiveChannels = ResolvedChannels;
		ActiveActorFilters = ActorTags;
		RecordingStartTime = FPlatformTime::Seconds();
		return FString();
	}

	return TEXT("Failed to start trace.");
}

void FRewindHelper::StopRecording()
{
	if (GEngine)
	{
		GEngine->Exec(nullptr, TEXT("Trace.Stop"));
	}
	bRecordingActive = false;
}

// ── File management ─────────────────────────────────────────────────────────
FString FRewindHelper::LoadTraceFile(const FString& FilePath)
{
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return FString::Printf(TEXT("File not found: %s"), *FilePath);
	}

	ActiveTraceFile = FilePath;
	bLoadedFromFile = true;
	bRecordingActive = false;
	return FString();
}

FString FRewindHelper::SaveTraceFile(const FString& FilePath)
{
	if (!HasData())
	{
		return TEXT("No recording data in memory.");
	}

	FString DestDir = FPaths::GetPath(FilePath);
	if (!DestDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DestDir, true);
	}

	if (!ActiveTraceFile.IsEmpty() && IFileManager::Get().FileExists(*ActiveTraceFile))
	{
		uint32 CopyResult = IFileManager::Get().Copy(*FilePath, *ActiveTraceFile);
		if (CopyResult != COPY_OK)
		{
			return FString::Printf(TEXT("Failed to copy trace file to: %s"), *FilePath);
		}
		return FString();
	}

	return TEXT("No source trace file found to save.");
}

// ── Status ──────────────────────────────────────────────────────────────────
TSharedPtr<FJsonObject> FRewindHelper::GetStatus()
{
	TSharedPtr<FJsonObject> Status = MakeShareable(new FJsonObject);

	Status->SetBoolField(TEXT("recording"), IsRecording());
	Status->SetNumberField(TEXT("duration"), GetRecordingDuration());

	TArray<TSharedPtr<FJsonValue>> ChannelsJson;
	for (const FString& Ch : ActiveChannels)
	{
		ChannelsJson.Add(MakeShareable(new FJsonValueString(Ch)));
	}
	Status->SetArrayField(TEXT("channels"), ChannelsJson);

	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	for (const FString& Tag : ActiveActorFilters)
	{
		ActorsJson.Add(MakeShareable(new FJsonValueString(Tag)));
	}
	Status->SetArrayField(TEXT("actors"), ActorsJson);

	if (!ActiveTraceFile.IsEmpty())
	{
		Status->SetStringField(TEXT("file"), ActiveTraceFile);
	}
	else
	{
		Status->SetField(TEXT("file"), MakeShareable(new FJsonValueNull()));
	}

	return Status;
}

// ── Object helpers ──────────────────────────────────────────────────────────
TSharedPtr<FDebugObjectInfo> FRewindHelper::FindObjectByTag(
	IRewindDebugger* Debugger, const FString& ActorTag)
{
	if (!Debugger)
	{
		return nullptr;
	}

	TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();
	for (const TSharedPtr<FDebugObjectInfo>& Info : Objects)
	{
		TSharedPtr<FDebugObjectInfo> Found = FindObjectByTagRecursive(Info, ActorTag);
		if (Found.IsValid())
		{
			return Found;
		}
	}
	return nullptr;
}

FString FRewindHelper::GetActorTagFromDebugObject(const FDebugObjectInfo& Info)
{
	// FDebugObjectInfo has ObjectName — use it as the tag identifier
	return Info.ObjectName;
}

// ── Analysis: CollectTrackList ──────────────────────────────────────────────
TSharedPtr<FJsonObject> FRewindHelper::CollectTrackList(const FString& ActorTag)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Recording range
	TSharedPtr<FJsonObject> Range = MakeShareable(new FJsonObject);
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	Range->SetNumberField(TEXT("start"), TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0);
	Range->SetNumberField(TEXT("end"), TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration());
	Result->SetObjectField(TEXT("recording_range"), Range);

	// Enumerate debugged objects
	TArray<TSharedPtr<FJsonValue>> ActorsArr;
	TArray<TSharedPtr<FDebugObjectInfo>>& Objects = Debugger->GetDebuggedObjects();

	for (const TSharedPtr<FDebugObjectInfo>& Info : Objects)
	{
		if (!Info.IsValid())
		{
			continue;
		}

		FString ObjTag = GetActorTagFromDebugObject(*Info);
		if (!ActorTag.IsEmpty() && ObjTag != ActorTag)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActorJson = MakeShareable(new FJsonObject);
		ActorJson->SetStringField(TEXT("actor_tag"), ObjTag);
		ActorJson->SetStringField(TEXT("object_name"), Info->ObjectName);

		// List child object types as available tracks
		TArray<TSharedPtr<FJsonValue>> TracksArr;
		for (const TSharedPtr<FDebugObjectInfo>& Child : Info->Children)
		{
			if (Child.IsValid())
			{
				TracksArr.Add(MakeShareable(new FJsonValueString(Child->ObjectName)));
			}
		}
		ActorJson->SetArrayField(TEXT("tracks"), TracksArr);

		ActorsArr.Add(MakeShareable(new FJsonValueObject(ActorJson)));
	}

	Result->SetArrayField(TEXT("actors"), ActorsArr);
	return Result;
}

// ── Analysis: CollectOverview (stub — data reads depend on track providers) ─
TSharedPtr<FJsonObject> FRewindHelper::CollectOverview(
	const FString& ActorTag,
	const TArray<FString>& TrackTypes)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	// Verify actor exists
	TSharedPtr<FDebugObjectInfo> TargetObj = FindObjectByTag(Debugger, ActorTag);
	if (!TargetObj.IsValid())
	{
		return nullptr;
	}

	const TraceServices::IAnalysisSession* AnalysisSession = Debugger->GetAnalysisSession();
	if (!AnalysisSession)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("actor_tag"), ActorTag);
	Result->SetStringField(TEXT("resolved_actor_tag"), GetActorTagFromDebugObject(*TargetObj));

	// Recording range
	TSharedPtr<FJsonObject> Range = MakeShareable(new FJsonObject);
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	const double StartTime = TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0;
	const double EndTime = TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration();
	Range->SetNumberField(TEXT("start"), StartTime);
	Range->SetNumberField(TEXT("end"), EndTime);
	Result->SetObjectField(TEXT("recording_range"), Range);

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);
	const IAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<IAnimationProvider>(GAnimationProviderName);
	const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>(GGameplayProviderName);
	if (!AnimationProvider)
	{
		Result->SetStringField(TEXT("data_status"), TEXT("animation_provider_missing"));
	}

	TArray<uint64> CandidateObjectIds = CollectCandidateObjectIds(TargetObj, GameplayProvider);
	Result->SetArrayField(TEXT("candidate_object_ids"), ObjectIdsToJson(CandidateObjectIds));

	TSharedPtr<FJsonObject> TracksJson = MakeShareable(new FJsonObject);
	if (IncludesSection(TrackTypes, TEXT("state_machines"), TEXT("state-machines")))
	{
		TArray<TSharedPtr<FJsonValue>> StateMachines;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendStateMachineEvents(AnimationProvider, ObjectId, StartTime, EndTime, StateMachines);
		}
		TracksJson->SetArrayField(TEXT("state_machines"), StateMachines);
	}
	if (IncludesSection(TrackTypes, TEXT("montages")))
	{
		TArray<TSharedPtr<FJsonValue>> Montages;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendMontageEvents(AnimationProvider, GameplayProvider, ObjectId, StartTime, EndTime, Montages);
		}
		TracksJson->SetArrayField(TEXT("montages"), Montages);
	}
	if (IncludesSection(TrackTypes, TEXT("notifies")))
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendNotifyEvents(AnimationProvider, GameplayProvider, ObjectId, StartTime, EndTime, Notifies);
		}
		TracksJson->SetArrayField(TEXT("notifies"), Notifies);
	}
	if (IncludesSection(TrackTypes, TEXT("blend_weights"), TEXT("blend-weights")))
	{
		TArray<TSharedPtr<FJsonValue>> BlendWeights;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendBlendWeightEvents(AnimationProvider, GameplayProvider, ObjectId, StartTime, EndTime, BlendWeights);
		}
		TracksJson->SetArrayField(TEXT("blend_weights"), BlendWeights);
	}
	Result->SetObjectField(TEXT("tracks"), TracksJson);

	return Result;
}

// ── Analysis: CollectSnapshot (stub — data reads depend on track providers) ─
TSharedPtr<FJsonObject> FRewindHelper::CollectSnapshot(
	double Time,
	const FString& ActorTag,
	const TArray<FString>& IncludeSections)
{
	IRewindDebugger* Debugger = GetRewindDebugger();
	if (!Debugger)
	{
		return nullptr;
	}

	// Validate time range
	const TRange<double>& TraceRange = Debugger->GetCurrentTraceRange();
	double Start = TraceRange.HasLowerBound() ? TraceRange.GetLowerBoundValue() : 0.0;
	double End = TraceRange.HasUpperBound() ? TraceRange.GetUpperBoundValue() : Debugger->GetRecordingDuration();
	if (Time < Start || Time > End)
	{
		return nullptr;
	}

	// Verify actor exists
	TSharedPtr<FDebugObjectInfo> TargetObj = FindObjectByTag(Debugger, ActorTag);
	if (!TargetObj.IsValid())
	{
		return nullptr;
	}

	const TraceServices::IAnalysisSession* AnalysisSession = Debugger->GetAnalysisSession();
	if (!AnalysisSession)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetStringField(TEXT("actor_tag"), ActorTag);
	Result->SetStringField(TEXT("resolved_actor_tag"), GetActorTagFromDebugObject(*TargetObj));

	bool bIncludeAll = IncludeSections.IsEmpty();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);
	const IAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<IAnimationProvider>(GAnimationProviderName);
	const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>(GGameplayProviderName);
	if (!AnimationProvider)
	{
		Result->SetStringField(TEXT("data_status"), TEXT("animation_provider_missing"));
	}

	double FrameStartTime = 0.0;
	double FrameEndTime = 0.0;
	uint64 FrameIndex = 0;
	const bool bResolvedFrame = ResolveFrameWindow(*AnalysisSession, Time, FrameStartTime, FrameEndTime, FrameIndex);
	TSharedPtr<FJsonObject> FrameJson = MakeShareable(new FJsonObject);
	FrameJson->SetBoolField(TEXT("resolved"), bResolvedFrame);
	FrameJson->SetStringField(TEXT("index"), UInt64ToString(FrameIndex));
	FrameJson->SetNumberField(TEXT("start_time"), FrameStartTime);
	FrameJson->SetNumberField(TEXT("end_time"), FrameEndTime);
	Result->SetObjectField(TEXT("frame"), FrameJson);

	TArray<uint64> CandidateObjectIds = CollectCandidateObjectIds(TargetObj, GameplayProvider);
	Result->SetArrayField(TEXT("candidate_object_ids"), ObjectIdsToJson(CandidateObjectIds));

	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("state-machines"), TEXT("state_machines")))
	{
		TArray<TSharedPtr<FJsonValue>> StateMachines;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendStateMachineEvents(AnimationProvider, ObjectId, FrameStartTime, FrameEndTime, StateMachines);
		}
		Result->SetArrayField(TEXT("state_machines"), StateMachines);
	}
	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("montages")))
	{
		TArray<TSharedPtr<FJsonValue>> Montages;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendMontageEvents(AnimationProvider, GameplayProvider, ObjectId, FrameStartTime, FrameEndTime, Montages);
		}
		Result->SetArrayField(TEXT("active_montages"), Montages);
	}
	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("blend-weights"), TEXT("blend_weights")))
	{
		TArray<TSharedPtr<FJsonValue>> BlendWeights;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendBlendWeightEvents(AnimationProvider, GameplayProvider, ObjectId, FrameStartTime, FrameEndTime, BlendWeights);
		}
		TSharedPtr<FJsonObject> BlendWeightsJson = MakeShareable(new FJsonObject);
		BlendWeightsJson->SetArrayField(TEXT("records"), BlendWeights);
		Result->SetObjectField(TEXT("blend_weights"), BlendWeightsJson);
		Result->SetArrayField(TEXT("asset_players"), BlendWeights);
	}
	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("curves")))
	{
		TSharedPtr<FJsonObject> CurvesJson = MakeShareable(new FJsonObject);
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendCurveEvents(AnimationProvider, ObjectId, FrameStartTime, FrameEndTime, CurvesJson);
		}
		Result->SetObjectField(TEXT("curves"), CurvesJson);
	}
	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("notifies")))
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendNotifyEvents(AnimationProvider, GameplayProvider, ObjectId, FrameStartTime, FrameEndTime, Notifies);
		}
		Result->SetArrayField(TEXT("notifies"), Notifies);
	}
	if (bIncludeAll || IncludesSection(IncludeSections, TEXT("anim-graph"), TEXT("anim_graph")))
	{
		TArray<TSharedPtr<FJsonValue>> AnimGraphs;
		TArray<TSharedPtr<FJsonValue>> AnimGraphNodes;
		for (uint64 ObjectId : CandidateObjectIds)
		{
			AppendAnimGraphEvents(AnimationProvider, ObjectId, FrameStartTime, FrameEndTime, AnimGraphs, AnimGraphNodes);
		}
		TSharedPtr<FJsonObject> AnimGraphJson = MakeShareable(new FJsonObject);
		AnimGraphJson->SetArrayField(TEXT("graphs"), AnimGraphs);
		AnimGraphJson->SetArrayField(TEXT("flat_nodes"), AnimGraphNodes);
		Result->SetObjectField(TEXT("anim_graph"), AnimGraphJson);
	}

	return Result;
}
