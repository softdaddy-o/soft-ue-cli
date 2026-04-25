// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/GetLogsTool.h"
#include "Tools/BridgeToolRegistry.h"
#include "SoftUEBridgeModule.h"
#include "Misc/OutputDeviceRedirector.h"
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

	FString Line = FString::Printf(TEXT("[%s][%s] %s"), *Category.ToString(), *VerbStr, V);

	FScopeLock ScopeLock(&Lock);
	Lines.Add(MoveTemp(Line));
	if (Lines.Num() > MaxLines)
	{
		Lines.RemoveAt(0, Lines.Num() - MaxLines);
	}
}

TArray<FString> FBridgeLogCapture::GetLines(int32 N, const FString& Filter, const FString& Category) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FString> Result;
	if (N <= 0) return Result;

	const bool bHasFilter = !Filter.IsEmpty() || !Category.IsEmpty();
	const FString CategoryBracket = Category.IsEmpty() ? FString() : (TEXT("[") + Category + TEXT("]"));

	// When filtering, scan all lines (newest first) and stop after N matches.
	// When not filtering, return the last N lines directly.
	const int32 Start = bHasFilter ? 0 : FMath::Max(0, Lines.Num() - N);
	Result.Reserve(FMath::Min(N, Lines.Num()));
	for (int32 i = Lines.Num() - 1; i >= Start; --i)
	{
		const FString& Line = Lines[i];
		if (!Filter.IsEmpty() && !Line.Contains(Filter, ESearchCase::IgnoreCase)) continue;
		if (!CategoryBracket.IsEmpty() && !Line.Contains(CategoryBracket, ESearchCase::IgnoreCase)) continue;
		Result.Add(Line);
		if (Result.Num() >= N) break;
	}
	// Reverse so results are in chronological order (oldest first)
	Algo::Reverse(Result);
	return Result;
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

	return S;
}

FBridgeToolResult UGetLogsTool::Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx)
{
	const int32 N       = GetIntArgOrDefault(Args, TEXT("lines"), 100);
	const FString Filter    = GetStringArgOrDefault(Args, TEXT("filter"));
	const FString Category  = GetStringArgOrDefault(Args, TEXT("category"));

	TArray<FString> LogLines = FBridgeLogCapture::Get().GetLines(N, Filter, Category);

	TArray<TSharedPtr<FJsonValue>> LinesArr;
	for (const FString& Line : LogLines)
	{
		LinesArr.Add(MakeShareable(new FJsonValueString(Line)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("lines"), LinesArr);
	Result->SetNumberField(TEXT("count"), LinesArr.Num());

	return FBridgeToolResult::Json(Result);
}
