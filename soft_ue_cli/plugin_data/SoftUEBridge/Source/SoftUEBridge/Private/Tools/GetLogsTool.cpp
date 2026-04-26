// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/GetLogsTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/DateTime.h"
#include "Algo/AllOf.h"
#include "Algo/Reverse.h"

// ── FBridgeLogCapture ─────────────────────────────────────────────────────────

FBridgeLogCapture& FBridgeLogCapture::Get()
{
	static FBridgeLogCapture Instance;
	return Instance;
}

void FBridgeLogCapture::Start()
{
	if (!bStarted)
	{
		GLog->AddOutputDevice(this);
		bStarted = true;
	}
}

void FBridgeLogCapture::Stop()
{
	if (bStarted)
	{
		GLog->RemoveOutputDevice(this);
		bStarted = false;
	}
}

void FBridgeLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FString VerbStr;
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal:   VerbStr = TEXT("Fatal");   break;
	case ELogVerbosity::Error:   VerbStr = TEXT("Error");   break;
	case ELogVerbosity::Warning: VerbStr = TEXT("Warning"); break;
	case ELogVerbosity::Display: VerbStr = TEXT("Display"); break;
	case ELogVerbosity::Log:     VerbStr = TEXT("Log");     break;
	case ELogVerbosity::Verbose: VerbStr = TEXT("Verbose"); break;
	default:                     VerbStr = TEXT("Log");     break;
	}

	FBridgeCapturedLogEntry Entry;
	Entry.Timestamp = FDateTime::UtcNow().ToIso8601();
	Entry.Category = Category.ToString();
	Entry.Verbosity = VerbStr;
	Entry.Message = V;
	Entry.Line = FString::Printf(TEXT("[%s][%s] %s"), *Entry.Category, *Entry.Verbosity, V);

	FScopeLock ScopeLock(&Lock);
	Entry.Sequence = NextSequence++;
	Entries.Add(MoveTemp(Entry));
	if (Entries.Num() > MaxLines)
	{
		Entries.RemoveAt(0, Entries.Num() - MaxLines);
	}
}

TArray<FBridgeCapturedLogEntry> FBridgeLogCapture::GetEntries(
	int32 N,
	const FString& Filter,
	const FString& Category,
	const FString& Since) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FBridgeCapturedLogEntry> Result;
	const int32 RequestedCount = FMath::Max(N, 0);
	const bool bHasSince = !Since.IsEmpty();
	const bool bSinceIsCursor = bHasSince && Algo::AllOf(Since, [](TCHAR Ch) { return FChar::IsDigit(Ch); });
	const uint64 SinceCursor = bSinceIsCursor ? FCString::Strtoui64(*Since, nullptr, 10) : 0;

	const bool bHasFilter = !Filter.IsEmpty() || !Category.IsEmpty();
	const int32 Start = (bHasFilter || bHasSince || RequestedCount <= 0) ? 0 : FMath::Max(0, Entries.Num() - RequestedCount);
	Result.Reserve(FMath::Min(RequestedCount > 0 ? RequestedCount : Entries.Num(), Entries.Num()));
	for (int32 i = Entries.Num() - 1; i >= Start; --i)
	{
		const FBridgeCapturedLogEntry& Entry = Entries[i];
		if (!Filter.IsEmpty() && !Entry.Message.Contains(Filter, ESearchCase::IgnoreCase)) continue;
		if (!Category.IsEmpty() && !Entry.Category.Equals(Category, ESearchCase::IgnoreCase)) continue;
		if (bHasSince)
		{
			const bool bIsNewer = bSinceIsCursor ? Entry.Sequence > SinceCursor : Entry.Timestamp > Since;
			if (!bIsNewer)
			{
				continue;
			}
		}
		Result.Add(Entry);
		if (RequestedCount > 0 && Result.Num() >= RequestedCount) break;
	}
	Algo::Reverse(Result);
	return Result;
}

uint64 FBridgeLogCapture::GetLatestCursor() const
{
	FScopeLock ScopeLock(&Lock);
	return Entries.Num() > 0 ? Entries.Last().Sequence : 0;
}

FString FBridgeLogCapture::GetLatestTimestamp() const
{
	FScopeLock ScopeLock(&Lock);
	return Entries.Num() > 0 ? Entries.Last().Timestamp : FString();
}

// ── UGetLogsTool ──────────────────────────────────────────────────────────────

#if !WITH_EDITOR
REGISTER_BRIDGE_TOOL(UGetLogsTool)
#endif

FString UGetLogsTool::GetToolDescription() const
{
	return TEXT("Get recent output log entries. Optionally filter by text or log category.");
}

TMap<FString, FBridgeSchemaProperty> UGetLogsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> S;

	auto Prop = [](const FString& Type, const FString& Desc) {
		FBridgeSchemaProperty P; P.Type = Type; P.Description = Desc; return P;
	};

	S.Add(TEXT("lines"),    Prop(TEXT("integer"), TEXT("Number of recent lines to return (default: 100)")));
	S.Add(TEXT("filter"),   Prop(TEXT("string"),  TEXT("Filter lines containing this text (case-insensitive)")));
	S.Add(TEXT("category"), Prop(TEXT("string"),  TEXT("Filter by log category (e.g. 'LogBlueprintUserMessages')")));
	S.Add(TEXT("since"),    Prop(TEXT("string"),  TEXT("Only return entries after this cursor/timestamp")));

	return S;
}

FBridgeToolResult UGetLogsTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const int32 N = GetIntArgOrDefault(Args, TEXT("lines"), 100);
	const FString Filter = GetStringArgOrDefault(Args, TEXT("filter"));
	const FString Category = GetStringArgOrDefault(Args, TEXT("category"));
	const FString Since = GetStringArgOrDefault(Args, TEXT("since"));

	TArray<FBridgeCapturedLogEntry> LogEntries = FBridgeLogCapture::Get().GetEntries(N, Filter, Category, Since);

	TArray<TSharedPtr<FJsonValue>> LinesArr;
	TArray<TSharedPtr<FJsonValue>> EntriesArr;
	for (const FBridgeCapturedLogEntry& Entry : LogEntries)
	{
		LinesArr.Add(MakeShareable(new FJsonValueString(Entry.Line)));

		TSharedPtr<FJsonObject> EntryJson = MakeShareable(new FJsonObject);
		EntryJson->SetStringField(TEXT("timestamp"), Entry.Timestamp);
		EntryJson->SetNumberField(TEXT("cursor"), static_cast<double>(Entry.Sequence));
		EntryJson->SetStringField(TEXT("category"), Entry.Category);
		EntryJson->SetStringField(TEXT("verbosity"), Entry.Verbosity);
		EntryJson->SetStringField(TEXT("message"), Entry.Message);
		EntryJson->SetStringField(TEXT("line"), Entry.Line);
		EntriesArr.Add(MakeShareable(new FJsonValueObject(EntryJson)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("lines"), LinesArr);
	Result->SetArrayField(TEXT("entries"), EntriesArr);
	Result->SetNumberField(TEXT("count"), LinesArr.Num());
	Result->SetStringField(TEXT("last_timestamp"), FBridgeLogCapture::Get().GetLatestTimestamp());
	Result->SetStringField(
		TEXT("next_cursor"),
		FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(FBridgeLogCapture::Get().GetLatestCursor())));

	return FBridgeToolResult::Json(Result);
}
