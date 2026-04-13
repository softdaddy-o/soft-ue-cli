// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Rewind/RewindStartTool.h"
#include "Tools/Rewind/RewindHelper.h"

FString URewindStartTool::GetToolDescription() const
{
	return TEXT("Start a Rewind Debugger recording session, or load an existing .utrace file. "
		"Use --channels to select which animation channels to record, "
		"--actors to filter by actor tag, --file to auto-save on stop, "
		"--load to open an existing trace file for analysis.");
}

TMap<FString, FBridgeSchemaProperty> URewindStartTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Channels;
	Channels.Type = TEXT("array");
	Channels.ItemsType = TEXT("string");
	Channels.Description = TEXT("Channels to record: 'skeletal-mesh', 'montage', 'anim-state', 'notify', 'object', 'all'. Default: all");
	Channels.bRequired = false;
	Schema.Add(TEXT("channels"), Channels);

	FBridgeSchemaProperty Actors;
	Actors.Type = TEXT("array");
	Actors.ItemsType = TEXT("string");
	Actors.Description = TEXT("Actor tags to filter. Default: record all actors");
	Actors.bRequired = false;
	Schema.Add(TEXT("actors"), Actors);

	FBridgeSchemaProperty File;
	File.Type = TEXT("string");
	File.Description = TEXT("Save path — auto-saves to this .utrace on stop (new recording only)");
	File.bRequired = false;
	Schema.Add(TEXT("file"), File);

	FBridgeSchemaProperty Load;
	Load.Type = TEXT("string");
	Load.Description = TEXT("Load an existing .utrace file for analysis (no new recording). Mutually exclusive with --file");
	Load.bRequired = false;
	Schema.Add(TEXT("load"), Load);

	return Schema;
}

FBridgeToolResult URewindStartTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString LoadPath = GetStringArgOrDefault(Arguments, TEXT("load"));
	FString FilePath = GetStringArgOrDefault(Arguments, TEXT("file"));

	// Mutual exclusion check
	if (!LoadPath.IsEmpty() && !FilePath.IsEmpty())
	{
		return FBridgeToolResult::Error(
			TEXT("Specify either 'load' or 'file', not both."));
	}

	// Load existing trace
	if (!LoadPath.IsEmpty())
	{
		FString Err = FRewindHelper::LoadTraceFile(LoadPath);
		if (!Err.IsEmpty())
		{
			return FBridgeToolResult::Error(Err);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("loaded"));
		Result->SetStringField(TEXT("file"), LoadPath);
		return FBridgeToolResult::Json(Result);
	}

	// Start new recording
	TArray<FString> Channels;
	const TArray<TSharedPtr<FJsonValue>>* ChannelArr;
	if (Arguments->TryGetArrayField(TEXT("channels"), ChannelArr))
	{
		for (const auto& Val : *ChannelArr)
		{
			Channels.Add(Val->AsString());
		}
	}

	// "all" or empty -> all channels
	if (Channels.IsEmpty() || Channels.Contains(TEXT("all")))
	{
		Channels = FRewindHelper::GetAllChannelNames();
	}

	TArray<FString> Actors;
	const TArray<TSharedPtr<FJsonValue>>* ActorArr;
	if (Arguments->TryGetArrayField(TEXT("actors"), ActorArr))
	{
		for (const auto& Val : *ActorArr)
		{
			Actors.Add(Val->AsString());
		}
	}

	FString Err = FRewindHelper::StartRecording(Channels, Actors, FilePath);
	if (!Err.IsEmpty())
	{
		return FBridgeToolResult::Error(Err);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("recording"));

	TArray<TSharedPtr<FJsonValue>> ChArr;
	for (const FString& Ch : Channels)
	{
		ChArr.Add(MakeShareable(new FJsonValueString(Ch)));
	}
	Result->SetArrayField(TEXT("channels"), ChArr);

	TArray<TSharedPtr<FJsonValue>> ActArr;
	for (const FString& Tag : Actors)
	{
		ActArr.Add(MakeShareable(new FJsonValueString(Tag)));
	}
	Result->SetArrayField(TEXT("actors"), ActArr);

	if (!FilePath.IsEmpty())
	{
		Result->SetStringField(TEXT("file"), FilePath);
	}
	else
	{
		Result->SetField(TEXT("file"), MakeShareable(new FJsonValueNull()));
	}

	return FBridgeToolResult::Json(Result);
}
