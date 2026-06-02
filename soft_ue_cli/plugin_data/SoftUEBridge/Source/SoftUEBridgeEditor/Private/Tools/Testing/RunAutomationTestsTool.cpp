// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Testing/RunAutomationTestsTool.h"
#include "Misc/AutomationTest.h"
#include "Misc/AutomationEvent.h"
#include "HAL/PlatformProcess.h"

namespace
{
	FString AutomationEventTypeToString(EAutomationEventType Type)
	{
		switch (Type)
		{
		case EAutomationEventType::Info:
			return TEXT("info");
		case EAutomationEventType::Warning:
			return TEXT("warning");
		case EAutomationEventType::Error:
			return TEXT("error");
		default:
			return TEXT("unknown");
		}
	}

	EAutomationTestFlags ParseAutomationFilterFlags(const FString& Flags)
	{
		if (Flags.IsEmpty() || Flags.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			return EAutomationTestFlags::SmokeFilter |
				EAutomationTestFlags::EngineFilter |
				EAutomationTestFlags::ProductFilter |
				EAutomationTestFlags::PerfFilter |
				EAutomationTestFlags::StressFilter |
				EAutomationTestFlags::NegativeFilter;
		}

		EAutomationTestFlags Result = EAutomationTestFlags::None;
		TArray<FString> Parts;
		Flags.ParseIntoArray(Parts, TEXT(","), true);
		for (FString Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (Part.Equals(TEXT("smoke"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::SmokeFilter;
			}
			else if (Part.Equals(TEXT("engine"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::EngineFilter;
			}
			else if (Part.Equals(TEXT("product"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::ProductFilter;
			}
			else if (Part.Equals(TEXT("perf"), ESearchCase::IgnoreCase) ||
				Part.Equals(TEXT("performance"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::PerfFilter;
			}
			else if (Part.Equals(TEXT("stress"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::StressFilter;
			}
			else if (Part.Equals(TEXT("negative"), ESearchCase::IgnoreCase))
			{
				Result |= EAutomationTestFlags::NegativeFilter;
			}
		}

		return Result == EAutomationTestFlags::None ? EAutomationTestFlags::ProductFilter : Result;
	}

	bool MatchesAutomationPattern(const FString& Value, const FString& Pattern)
	{
		if (Pattern.IsEmpty())
		{
			return true;
		}
		return Value.Equals(Pattern, ESearchCase::IgnoreCase) ||
			Value.Contains(Pattern, ESearchCase::IgnoreCase) ||
			Value.MatchesWildcard(*Pattern, ESearchCase::IgnoreCase);
	}

	bool MatchesAnyRequestedTest(const FAutomationTestInfo& TestInfo, const TArray<FString>& RequestedTests)
	{
		if (RequestedTests.Num() == 0)
		{
			return true;
		}

		const FString TestName = TestInfo.GetTestName();
		const FString FullTestPath = TestInfo.GetFullTestPath();
		const FString DisplayName = TestInfo.GetDisplayName();
		for (const FString& RequestedTest : RequestedTests)
		{
			if (MatchesAutomationPattern(TestName, RequestedTest) ||
				MatchesAutomationPattern(FullTestPath, RequestedTest) ||
				MatchesAutomationPattern(DisplayName, RequestedTest))
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> AutomationTestInfoToJson(const FAutomationTestInfo& TestInfo)
	{
		TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
		Json->SetStringField(TEXT("name"), TestInfo.GetTestName());
		Json->SetStringField(TEXT("full_test_path"), TestInfo.GetFullTestPath());
		Json->SetStringField(TEXT("display_name"), TestInfo.GetDisplayName());
		Json->SetStringField(TEXT("parameter"), TestInfo.GetTestParameter());
		Json->SetStringField(TEXT("asset_path"), TestInfo.GetAssetPath());
		Json->SetStringField(TEXT("source_file"), TestInfo.GetSourceFile());
		Json->SetNumberField(TEXT("source_line"), TestInfo.GetSourceFileLine());
		Json->SetStringField(TEXT("tags"), TestInfo.GetTestTags());
		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> AutomationEntriesToJson(const FAutomationTestExecutionInfo& ExecutionInfo)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FAutomationExecutionEntry& Entry : ExecutionInfo.GetEntries())
		{
			TSharedPtr<FJsonObject> EntryJson = MakeShareable(new FJsonObject);
			EntryJson->SetStringField(TEXT("severity"), AutomationEventTypeToString(Entry.Event.Type));
			EntryJson->SetStringField(TEXT("message"), Entry.Event.Message);
			EntryJson->SetStringField(TEXT("context"), Entry.Event.Context);
			EntryJson->SetStringField(TEXT("filename"), Entry.Filename);
			EntryJson->SetNumberField(TEXT("line"), Entry.LineNumber);
			EntryJson->SetStringField(TEXT("timestamp"), Entry.Timestamp.ToIso8601());
			Entries.Add(MakeShareable(new FJsonValueObject(EntryJson)));
		}
		return Entries;
	}

	TArray<TSharedPtr<FJsonValue>> AutomationErrorMessagesToJson(const FAutomationTestExecutionInfo& ExecutionInfo)
	{
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const FAutomationExecutionEntry& Entry : ExecutionInfo.GetEntries())
		{
			if (Entry.Event.Type == EAutomationEventType::Error)
			{
				Messages.Add(MakeShareable(new FJsonValueString(Entry.Event.Message)));
			}
		}
		return Messages;
	}

	FString ExtractBaseAutomationTestName(const FString& TestCommand)
	{
		FString BaseName;
		FString Params;
		return TestCommand.Split(TEXT(" "), &BaseName, &Params, ESearchCase::CaseSensitive)
			? BaseName
			: TestCommand;
	}
}

FString URunAutomationTestsTool::GetToolDescription() const
{
	return TEXT("Run Unreal Automation tests by exact name or filter and return structured per-test results.");
}

TMap<FString, FBridgeSchemaProperty> URunAutomationTestsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Test;
	Test.Type = TEXT("string");
	Test.Description = TEXT("Exact or wildcard test name/path to run");
	Test.bRequired = false;
	Schema.Add(TEXT("test"), Test);

	FBridgeSchemaProperty Tests;
	Tests.Type = TEXT("array");
	Tests.ItemsType = TEXT("string");
	Tests.Description = TEXT("Exact or wildcard test names/paths to run");
	Tests.bRequired = false;
	Schema.Add(TEXT("tests"), Tests);

	FBridgeSchemaProperty Filter;
	Filter.Type = TEXT("string");
	Filter.Description = TEXT("Substring or wildcard filter matched against test name, display name, and full path");
	Filter.bRequired = false;
	Schema.Add(TEXT("filter"), Filter);

	FBridgeSchemaProperty Flags;
	Flags.Type = TEXT("string");
	Flags.Description = TEXT("Comma-separated automation filter flags: all, smoke, engine, product, perf, stress, negative");
	Flags.bRequired = false;
	Schema.Add(TEXT("flags"), Flags);

	FBridgeSchemaProperty Timeout;
	Timeout.Type = TEXT("number");
	Timeout.Description = TEXT("Per-test timeout in seconds (default: 120)");
	Timeout.bRequired = false;
	Schema.Add(TEXT("timeout"), Timeout);

	FBridgeSchemaProperty IncludeDeveloper;
	IncludeDeveloper.Type = TEXT("boolean");
	IncludeDeveloper.Description = TEXT("Include tests from Developer directories");
	IncludeDeveloper.bRequired = false;
	Schema.Add(TEXT("include_developer"), IncludeDeveloper);

	FBridgeSchemaProperty ListOnly;
	ListOnly.Type = TEXT("boolean");
	ListOnly.Description = TEXT("Return matched tests without running them");
	ListOnly.bRequired = false;
	Schema.Add(TEXT("list_only"), ListOnly);

	FBridgeSchemaProperty MaxTests;
	MaxTests.Type = TEXT("integer");
	MaxTests.Description = TEXT("Maximum number of matched tests to run");
	MaxTests.bRequired = false;
	Schema.Add(TEXT("max_tests"), MaxTests);

	return Schema;
}

FBridgeToolResult URunAutomationTestsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Test = GetStringArgOrDefault(Arguments, TEXT("test"));
	const FString Filter = GetStringArgOrDefault(Arguments, TEXT("filter"));
	const FString FlagsArg = GetStringArgOrDefault(Arguments, TEXT("flags"), TEXT("all"));
	const float TimeoutSeconds = FMath::Max(0.1f, GetFloatArgOrDefault(Arguments, TEXT("timeout"), 120.0f));
	const bool bIncludeDeveloper = GetBoolArgOrDefault(Arguments, TEXT("include_developer"), false);
	const bool bListOnly = GetBoolArgOrDefault(Arguments, TEXT("list_only"), false);
	const int32 MaxTests = GetIntArgOrDefault(Arguments, TEXT("max_tests"), 0);

	TArray<FString> RequestedTests;
	if (!Test.IsEmpty())
	{
		RequestedTests.Add(Test);
	}
	const TArray<TSharedPtr<FJsonValue>>* TestsArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("tests"), TestsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *TestsArray)
		{
			RequestedTests.Add(Value->AsString());
		}
	}

	if (RequestedTests.Num() == 0 && Filter.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Provide 'test', 'tests', or 'filter'. Use list_only=true to inspect matches before running."));
	}

	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.LoadTestModules();
	Framework.SetDeveloperDirectoryIncluded(bIncludeDeveloper);
	Framework.SetRequestedTestFilter(ParseAutomationFilterFlags(FlagsArg));

	TArray<FAutomationTestInfo> AvailableTests;
	Framework.GetValidTestNames(AvailableTests);

	TArray<FAutomationTestInfo> MatchedTests;
	for (const FAutomationTestInfo& TestInfo : AvailableTests)
	{
		if (!MatchesAnyRequestedTest(TestInfo, RequestedTests))
		{
			continue;
		}
		if (!Filter.IsEmpty() &&
			!MatchesAutomationPattern(TestInfo.GetTestName(), Filter) &&
			!MatchesAutomationPattern(TestInfo.GetFullTestPath(), Filter) &&
			!MatchesAutomationPattern(TestInfo.GetDisplayName(), Filter))
		{
			continue;
		}

		MatchedTests.Add(TestInfo);
		if (MaxTests > 0 && MatchedTests.Num() >= MaxTests)
		{
			break;
		}
	}

	TArray<TSharedPtr<FJsonValue>> MatchedTestsJson;
	for (const FAutomationTestInfo& TestInfo : MatchedTests)
	{
		MatchedTestsJson.Add(MakeShareable(new FJsonValueObject(AutomationTestInfoToJson(TestInfo))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("filter"), Filter);
	Result->SetStringField(TEXT("flags"), FlagsArg);
	Result->SetNumberField(TEXT("available_count"), AvailableTests.Num());
	Result->SetNumberField(TEXT("matched_count"), MatchedTests.Num());
	Result->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
	Result->SetBoolField(TEXT("include_developer"), bIncludeDeveloper);
	Result->SetBoolField(TEXT("list_only"), bListOnly);
	Result->SetArrayField(TEXT("matched_tests"), MatchedTestsJson);

	if (MatchedTests.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("No automation tests matched the requested name or filter"));
		return FBridgeToolResult::Json(Result);
	}

	if (bListOnly)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("state"), TEXT("listed"));
		return FBridgeToolResult::Json(Result);
	}

	TArray<TSharedPtr<FJsonValue>> TestResults;
	int32 Passed = 0;
	int32 Failed = 0;
	int32 Skipped = 0;
	int32 TimedOut = 0;
	const double SuiteStartTime = FPlatformTime::Seconds();

	Framework.OnBeforeAllTestsEvent.Broadcast();
	for (const FAutomationTestInfo& TestInfo : MatchedTests)
	{
		const FString TestCommand = TestInfo.GetTestName();
		const FString FullTestPath = TestInfo.GetFullTestPath();

		TSharedPtr<FJsonObject> TestResult = AutomationTestInfoToJson(TestInfo);
		FString SkipReason;
		bool bWarn = false;
		if (!Framework.ContainsTest(ExtractBaseAutomationTestName(TestCommand)) ||
			!Framework.CanRunTestInEnvironment(TestCommand, &SkipReason, &bWarn))
		{
			TestResult->SetStringField(TEXT("state"), TEXT("skipped"));
			TestResult->SetBoolField(TEXT("success"), false);
			TestResult->SetStringField(TEXT("skip_reason"), SkipReason.IsEmpty() ? TEXT("test cannot run in current environment") : SkipReason);
			TestResults.Add(MakeShareable(new FJsonValueObject(TestResult)));
			Skipped++;
			continue;
		}

		const double TestStartTime = FPlatformTime::Seconds();
		Framework.StartTestByName(TestCommand, 0, FullTestPath);
		if (!GIsAutomationTesting)
		{
			TestResult->SetStringField(TEXT("state"), TEXT("failed_to_start"));
			TestResult->SetBoolField(TEXT("success"), false);
			TestResult->SetStringField(TEXT("error"), TEXT("Automation framework did not enter testing state"));
			TestResults.Add(MakeShareable(new FJsonValueObject(TestResult)));
			Failed++;
			continue;
		}

		bool bTimedOut = false;
		while (true)
		{
			const bool bLatentDone = Framework.ExecuteLatentCommands();
			const bool bNetworkDone = Framework.ExecuteNetworkCommands();
			if (bLatentDone && bNetworkDone)
			{
				break;
			}
			if (FPlatformTime::Seconds() - TestStartTime > TimeoutSeconds)
			{
				bTimedOut = true;
				Framework.DequeueAllCommands();
				break;
			}
			FPlatformProcess::Sleep(0.01f);
		}

		FAutomationTestExecutionInfo ExecutionInfo;
		const bool bTestSuccess = Framework.StopTest(ExecutionInfo) && !bTimedOut;
		TestResult->SetBoolField(TEXT("success"), bTestSuccess);
		TestResult->SetStringField(TEXT("state"), bTimedOut ? TEXT("timeout") : (bTestSuccess ? TEXT("passed") : TEXT("failed")));
		TestResult->SetNumberField(TEXT("duration"), ExecutionInfo.Duration);
		TestResult->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - TestStartTime);
		TestResult->SetNumberField(TEXT("error_count"), ExecutionInfo.GetErrorTotal());
		TestResult->SetNumberField(TEXT("warning_count"), ExecutionInfo.GetWarningTotal());
		TestResult->SetArrayField(TEXT("entries"), AutomationEntriesToJson(ExecutionInfo));
		TestResult->SetArrayField(TEXT("error_messages"), AutomationErrorMessagesToJson(ExecutionInfo));

		if (bTimedOut)
		{
			TestResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Timed out after %.1f seconds"), TimeoutSeconds));
			TimedOut++;
			Failed++;
		}
		else if (bTestSuccess)
		{
			Passed++;
		}
		else
		{
			Failed++;
		}

		TestResults.Add(MakeShareable(new FJsonValueObject(TestResult)));
	}
	Framework.OnAfterAllTestsEvent.Broadcast();

	Result->SetBoolField(TEXT("success"), Failed == 0);
	Result->SetStringField(TEXT("state"), Failed == 0 ? TEXT("passed") : TEXT("failed"));
	Result->SetNumberField(TEXT("passed"), Passed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("skipped"), Skipped);
	Result->SetNumberField(TEXT("timed_out"), TimedOut);
	Result->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - SuiteStartTime);
	Result->SetArrayField(TEXT("tests"), TestResults);

	return FBridgeToolResult::Json(Result);
}
