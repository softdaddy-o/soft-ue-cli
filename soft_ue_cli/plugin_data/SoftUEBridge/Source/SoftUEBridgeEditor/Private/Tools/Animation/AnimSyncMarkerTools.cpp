// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimSyncMarkerTools.h"

#include "Utils/BridgeAssetModifier.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
FBridgeSchemaProperty SyncMarkerSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

UAnimSequence* LoadAnimSequenceForMarkers(const FString& AssetPath, FString& OutError)
{
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, OutError);
	if (!Object)
	{
		return nullptr;
	}

	UAnimSequence* Sequence = Cast<UAnimSequence>(Object);
	if (!Sequence)
	{
		OutError = FString::Printf(TEXT("Asset is not an AnimSequence: %s"), *AssetPath);
		return nullptr;
	}
	return Sequence;
}

TSharedPtr<FJsonObject> MarkerToJson(const FAnimSyncMarker& Marker, int32 Index)
{
	TSharedPtr<FJsonObject> MarkerJson = MakeShared<FJsonObject>();
	MarkerJson->SetNumberField(TEXT("index"), Index);
	MarkerJson->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
	MarkerJson->SetStringField(TEXT("MarkerName"), Marker.MarkerName.ToString());
	MarkerJson->SetNumberField(TEXT("time"), Marker.Time);
	MarkerJson->SetNumberField(TEXT("Time"), Marker.Time);
#if WITH_EDITORONLY_DATA
	MarkerJson->SetNumberField(TEXT("track_index"), Marker.TrackIndex);
	MarkerJson->SetStringField(TEXT("guid"), Marker.Guid.ToString(EGuidFormats::DigitsWithHyphens));
#endif
	return MarkerJson;
}

TSharedPtr<FJsonObject> SequenceMarkersToJson(UAnimSequence* Sequence, const FString& AssetPath, const FString& MarkerFilter = TEXT(""))
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("sequence"), Sequence ? Sequence->GetPathName() : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> Markers;
	TMap<FName, int32> CountsByName;
	if (Sequence)
	{
		for (int32 Index = 0; Index < Sequence->AuthoredSyncMarkers.Num(); ++Index)
		{
			const FAnimSyncMarker& Marker = Sequence->AuthoredSyncMarkers[Index];
			if (!MarkerFilter.IsEmpty() && !Marker.MarkerName.ToString().Equals(MarkerFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			CountsByName.FindOrAdd(Marker.MarkerName)++;
			Markers.Add(MakeShared<FJsonValueObject>(MarkerToJson(Marker, Index)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Names;
	for (const TPair<FName, int32>& Pair : CountsByName)
	{
		TSharedPtr<FJsonObject> NameJson = MakeShared<FJsonObject>();
		NameJson->SetStringField(TEXT("name"), Pair.Key.ToString());
		NameJson->SetNumberField(TEXT("count"), Pair.Value);
		Names.Add(MakeShared<FJsonValueObject>(NameJson));
	}

	Result->SetArrayField(TEXT("markers"), Markers);
	Result->SetArrayField(TEXT("names"), Names);
	Result->SetNumberField(TEXT("marker_count"), Markers.Num());
	Result->SetNumberField(TEXT("unique_marker_count"), CountsByName.Num());
	Result->SetNumberField(TEXT("play_length"), Sequence ? Sequence->GetPlayLength() : 0.0);
	return Result;
}

bool CheckoutSequenceIfRequested(UAnimSequence* Sequence, bool bCheckout, FString& OutError)
{
	if (!bCheckout || !Sequence)
	{
		return true;
	}

	UPackage* Package = Sequence->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Sequence has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	return FBridgeAssetModifier::CheckoutFile(PackageFileName, OutError);
}

bool SaveSequenceIfRequested(UAnimSequence* Sequence, bool bSave, FString& OutError)
{
	if (!bSave)
	{
		return true;
	}
	return FBridgeAssetModifier::SaveAsset(Sequence, false, OutError);
}
}

FString UInspectSyncMarkersTool::GetToolDescription() const
{
	return TEXT("Inspect AuthoredSyncMarkers on an AnimSequence asset.");
}

TMap<FString, FBridgeSchemaProperty> UInspectSyncMarkersTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("AnimSequence asset path"), true));
	return Schema;
}

TArray<FString> UInspectSyncMarkersTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UInspectSyncMarkersTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("inspect-sync-markers: asset_path is required"));
	}

	FString Error;
	UAnimSequence* Sequence = LoadAnimSequenceForMarkers(AssetPath, Error);
	if (!Sequence)
	{
		return FBridgeToolResult::Error(Error);
	}

	return FBridgeToolResult::Json(SequenceMarkersToJson(Sequence, AssetPath));
}

FString UCompareSyncMarkersTool::GetToolDescription() const
{
	return TEXT("Compare AuthoredSyncMarkers across AnimSequence assets.");
}

TMap<FString, FBridgeSchemaProperty> UCompareSyncMarkersTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_paths"), SyncMarkerSchemaProperty(TEXT("array"), TEXT("AnimSequence asset paths to compare"), true));
	Schema.Add(TEXT("marker"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("Optional marker name filter")));
	return Schema;
}

TArray<FString> UCompareSyncMarkersTool::GetRequiredParams() const
{
	return { TEXT("asset_paths") };
}

FBridgeToolResult UCompareSyncMarkersTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* AssetPathValues = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("asset_paths"), AssetPathValues) || !AssetPathValues || AssetPathValues->Num() < 2)
	{
		return FBridgeToolResult::Error(TEXT("compare-sync-markers: provide at least two asset_paths"));
	}

	const FString MarkerFilter = GetStringArgOrDefault(Arguments, TEXT("marker"), TEXT(""));
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Sequences;
	TMap<FString, int32> GlobalCounts;

	for (const TSharedPtr<FJsonValue>& Value : *AssetPathValues)
	{
		const FString AssetPath = Value.IsValid() ? Value->AsString() : TEXT("");
		FString Error;
		UAnimSequence* Sequence = LoadAnimSequenceForMarkers(AssetPath, Error);
		if (!Sequence)
		{
			return FBridgeToolResult::Error(Error);
		}

		for (const FAnimSyncMarker& Marker : Sequence->AuthoredSyncMarkers)
		{
			if (MarkerFilter.IsEmpty() || Marker.MarkerName.ToString().Equals(MarkerFilter, ESearchCase::IgnoreCase))
			{
				GlobalCounts.FindOrAdd(Marker.MarkerName.ToString())++;
			}
		}
		Sequences.Add(MakeShared<FJsonValueObject>(SequenceMarkersToJson(Sequence, AssetPath, MarkerFilter)));
	}

	TArray<TSharedPtr<FJsonValue>> Summary;
	for (const TPair<FString, int32>& Pair : GlobalCounts)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Pair.Key);
		Entry->SetNumberField(TEXT("total_count"), Pair.Value);
		Summary.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Result->SetArrayField(TEXT("sequences"), Sequences);
	Result->SetArrayField(TEXT("summary"), Summary);
	Result->SetStringField(TEXT("marker_filter"), MarkerFilter);
	return FBridgeToolResult::Json(Result);
}

FString UAddSyncMarkerTool::GetToolDescription() const
{
	return TEXT("Add an AuthoredSyncMarker to an AnimSequence asset.");
}

TMap<FString, FBridgeSchemaProperty> UAddSyncMarkerTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("AnimSequence asset path"), true));
	Schema.Add(TEXT("marker"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("Marker name"), true));
	Schema.Add(TEXT("time"), SyncMarkerSchemaProperty(TEXT("number"), TEXT("Marker time in seconds"), true));
	Schema.Add(TEXT("save"), SyncMarkerSchemaProperty(TEXT("boolean"), TEXT("Save the asset after mutation")));
	Schema.Add(TEXT("checkout"), SyncMarkerSchemaProperty(TEXT("boolean"), TEXT("Checkout the asset before mutation")));
	return Schema;
}

TArray<FString> UAddSyncMarkerTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("marker"), TEXT("time") };
}

FBridgeToolResult UAddSyncMarkerTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	FString AssetPath;
	FString MarkerName;
	double Time = 0.0;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath) || !GetStringArg(Arguments, TEXT("marker"), MarkerName)
		|| !Arguments.IsValid() || !Arguments->TryGetNumberField(TEXT("time"), Time))
	{
		return FBridgeToolResult::Error(TEXT("add-sync-marker: asset_path, marker, and time are required"));
	}

	FString Error;
	UAnimSequence* Sequence = LoadAnimSequenceForMarkers(AssetPath, Error);
	if (!Sequence)
	{
		return FBridgeToolResult::Error(Error);
	}
	if (!CheckoutSequenceIfRequested(Sequence, GetBoolArgOrDefault(Arguments, TEXT("checkout"), false), Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(FText::FromString(TEXT("Add Anim Sync Marker")));
	FBridgeAssetModifier::MarkModified(Sequence);

	FAnimSyncMarker NewMarker;
	NewMarker.MarkerName = FName(*MarkerName);
	NewMarker.Time = static_cast<float>(Time);
#if WITH_EDITORONLY_DATA
	NewMarker.Guid = FGuid::NewGuid();
#endif
	Sequence->AuthoredSyncMarkers.Add(NewMarker);
	Sequence->AuthoredSyncMarkers.Sort();
	FBridgeAssetModifier::MarkPackageDirty(Sequence);

	if (!SaveSequenceIfRequested(Sequence, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = SequenceMarkersToJson(Sequence, AssetPath);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action"), TEXT("add"));
	Result->SetStringField(TEXT("marker"), MarkerName);
	Result->SetNumberField(TEXT("time"), Time);
	return FBridgeToolResult::Json(Result);
}

FString URemoveSyncMarkerTool::GetToolDescription() const
{
	return TEXT("Remove AuthoredSyncMarkers from an AnimSequence asset.");
}

TMap<FString, FBridgeSchemaProperty> URemoveSyncMarkerTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("AnimSequence asset path"), true));
	Schema.Add(TEXT("marker"), SyncMarkerSchemaProperty(TEXT("string"), TEXT("Marker name"), true));
	Schema.Add(TEXT("time"), SyncMarkerSchemaProperty(TEXT("number"), TEXT("Optional marker time in seconds")));
	Schema.Add(TEXT("tolerance"), SyncMarkerSchemaProperty(TEXT("number"), TEXT("Time tolerance when time is provided")));
	Schema.Add(TEXT("save"), SyncMarkerSchemaProperty(TEXT("boolean"), TEXT("Save the asset after mutation")));
	Schema.Add(TEXT("checkout"), SyncMarkerSchemaProperty(TEXT("boolean"), TEXT("Checkout the asset before mutation")));
	return Schema;
}

TArray<FString> URemoveSyncMarkerTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("marker") };
}

FBridgeToolResult URemoveSyncMarkerTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context)
{
	FString AssetPath;
	FString MarkerName;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath) || !GetStringArg(Arguments, TEXT("marker"), MarkerName))
	{
		return FBridgeToolResult::Error(TEXT("remove-sync-marker: asset_path and marker are required"));
	}

	double TargetTime = 0.0;
	const bool bHasTime = Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("time"), TargetTime);
	const double Tolerance = GetFloatArgOrDefault(Arguments, TEXT("tolerance"), 0.001f);

	FString Error;
	UAnimSequence* Sequence = LoadAnimSequenceForMarkers(AssetPath, Error);
	if (!Sequence)
	{
		return FBridgeToolResult::Error(Error);
	}
	if (!CheckoutSequenceIfRequested(Sequence, GetBoolArgOrDefault(Arguments, TEXT("checkout"), false), Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	const TSharedPtr<FScopedTransaction> Transaction = FBridgeAssetModifier::BeginTransaction(FText::FromString(TEXT("Remove Anim Sync Marker")));
	FBridgeAssetModifier::MarkModified(Sequence);

	const int32 BeforeCount = Sequence->AuthoredSyncMarkers.Num();
	Sequence->AuthoredSyncMarkers.RemoveAll([&](const FAnimSyncMarker& Marker)
	{
		if (!Marker.MarkerName.ToString().Equals(MarkerName, ESearchCase::IgnoreCase))
		{
			return false;
		}
		return !bHasTime || FMath::Abs(Marker.Time - static_cast<float>(TargetTime)) <= static_cast<float>(Tolerance);
	});
	const int32 RemovedCount = BeforeCount - Sequence->AuthoredSyncMarkers.Num();
	if (RemovedCount > 0)
	{
		FBridgeAssetModifier::MarkPackageDirty(Sequence);
	}

	if (!SaveSequenceIfRequested(Sequence, GetBoolArgOrDefault(Arguments, TEXT("save"), false), Error))
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = SequenceMarkersToJson(Sequence, AssetPath);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action"), TEXT("remove"));
	Result->SetStringField(TEXT("marker"), MarkerName);
	Result->SetNumberField(TEXT("removed_count"), RemovedCount);
	if (bHasTime)
	{
		Result->SetNumberField(TEXT("time"), TargetTime);
		Result->SetNumberField(TEXT("tolerance"), Tolerance);
	}
	return FBridgeToolResult::Json(Result);
}
