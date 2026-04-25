// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Scripting/RunPythonScriptTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

/** Captures LogPython output during script execution */
class FPythonOutputCapture : public FOutputDevice
{
public:
	TArray<FString> Lines;
	FCriticalSection Lock;

	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category == TEXT("LogPython") || Category == TEXT("Python"))
		{
			FScopeLock SL(&Lock);
			Lines.Add(FString(V));
		}
	}
};

FString URunPythonScriptTool::GetToolDescription() const
{
	return TEXT("Execute a Python script in Unreal Editor's Python environment. Requires PythonScriptPlugin to be enabled.");
}

TMap<FString, FBridgeSchemaProperty> URunPythonScriptTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Script;
	Script.Type = TEXT("string");
	Script.Description = TEXT("Inline Python code to execute (mutually exclusive with script_path)");
	Script.bRequired = false;
	Schema.Add(TEXT("script"), Script);

	FBridgeSchemaProperty ScriptPath;
	ScriptPath.Type = TEXT("string");
	ScriptPath.Description = TEXT("Path to a Python script file (mutually exclusive with script)");
	ScriptPath.bRequired = false;
	Schema.Add(TEXT("script_path"), ScriptPath);

	FBridgeSchemaProperty PythonPaths;
	PythonPaths.Type = TEXT("array");
	PythonPaths.Description = TEXT("Additional directories to add to Python sys.path for module imports (array of strings)");
	PythonPaths.bRequired = false;
	Schema.Add(TEXT("python_paths"), PythonPaths);

	FBridgeSchemaProperty Arguments;
	Arguments.Type = TEXT("object");
	Arguments.Description = TEXT("Arguments to pass to the script (accessible via unreal.get_mcp_args())");
	Arguments.bRequired = false;
	Schema.Add(TEXT("arguments"), Arguments);

	return Schema;
}

FBridgeToolResult URunPythonScriptTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	// Check if PythonScriptPlugin is available
	if (!FModuleManager::Get().IsModuleLoaded("PythonScriptPlugin"))
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("run-python-script: PythonScriptPlugin is not loaded"));
		return FBridgeToolResult::Error(TEXT("PythonScriptPlugin is not enabled. Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin"));
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		return FBridgeToolResult::Error(TEXT("Failed to get PythonScriptPlugin interface"));
	}

	// Get script or script_path (mutually exclusive)
	FString Script = GetStringArgOrDefault(Arguments, TEXT("script"));
	FString ScriptPath = GetStringArgOrDefault(Arguments, TEXT("script_path"));

	if (Script.IsEmpty() && ScriptPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Either 'script' or 'script_path' must be provided"));
	}

	if (!Script.IsEmpty() && !ScriptPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Cannot specify both 'script' and 'script_path'. Use only one."));
	}

	// If script_path is provided, read the file
	if (!ScriptPath.IsEmpty())
	{
		FString ReadError;
		if (!ReadScriptFile(ScriptPath, Script, ReadError))
		{
			return FBridgeToolResult::Error(ReadError);
		}
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Loaded script from %s"), *ScriptPath);
	}

	// Get additional Python paths if provided
	TArray<FString> PythonPaths;
	if (Arguments->HasField(TEXT("python_paths")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PathsArray;
		if (Arguments->TryGetArrayField(TEXT("python_paths"), PathsArray))
		{
			for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
			{
				FString PathStr = PathValue->AsString();
				if (!PathStr.IsEmpty())
				{
					PythonPaths.Add(PathStr);
				}
			}
			UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Adding %d Python path(s)"), PythonPaths.Num());
		}
	}

	// Build Python command with arguments and paths if provided
	FString PythonCommand = BuildPythonCommand(Script, Arguments, PythonPaths);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Executing Python code..."));

	// Execute Python command
	bool bSuccess = false;
	FString Error;
	FString Output = ExecutePython(PythonCommand, bSuccess, Error);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSuccess);

	// Detect if script performed level loading operations
	bool bLevelLoadDetected = Output.Contains(TEXT("LoadMap")) ||
	                          Output.Contains(TEXT("load_level")) ||
	                          Output.Contains(TEXT("Loading map"));

	if (bSuccess)
	{
		Result->SetStringField(TEXT("output"), Output);
		if (!ScriptPath.IsEmpty())
		{
			Result->SetStringField(TEXT("script_path"), ScriptPath);
		}
		if (bLevelLoadDetected)
		{
			Result->SetStringField(TEXT("advisory"),
				TEXT("Level loading detected. World state may have changed. Verify editor state before continuing."));
		}
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Execution completed successfully"));
	}
	else
	{
		Result->SetStringField(TEXT("error"), Error);
		if (!Output.IsEmpty())
		{
			Result->SetStringField(TEXT("output"), Output);
		}
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("run-python-script: Execution failed: %s"), *Error);
	}

	return FBridgeToolResult::Json(Result);
}

FString URunPythonScriptTool::ExecutePython(const FString& Command, bool& bOutSuccess, FString& OutError)
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		bOutSuccess = false;
		OutError = TEXT("PythonScriptPlugin interface not available");
		return FString();
	}

	// Register output capture before execution
	FPythonOutputCapture Capture;
	GLog->AddOutputDevice(&Capture);

	// Wrap script with error handling
	FString WrappedScript = FString::Printf(TEXT(
		"_mcp_success = True\n"
		"_mcp_error = ''\n"
		"try:\n"
		"    %s\n"
		"except Exception as e:\n"
		"    _mcp_success = False\n"
		"    _mcp_error = str(e)\n"
		"    import traceback\n"
		"    print(traceback.format_exc())\n"
	), *Command.Replace(TEXT("\n"), TEXT("\n    ")));

	// Execute the script
	PythonPlugin->ExecPythonCommand(*WrappedScript);

	// Force garbage collection to clean up any orphaned objects created by the script
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// Check success status via a status variable printed to log
	PythonPlugin->ExecPythonCommand(TEXT("print('MCP_STATUS:' + ('SUCCESS' if _mcp_success else 'FAILURE:' + _mcp_error))"));

	GLog->RemoveOutputDevice(&Capture);

	// Build output from captured lines
	FString Output;
	FString StatusLine;
	for (const FString& Line : Capture.Lines)
	{
		if (Line.Contains(TEXT("MCP_STATUS:")))
		{
			StatusLine = Line;
		}
		else
		{
			if (!Output.IsEmpty()) Output += TEXT("\n");
			Output += Line;
		}
	}

	bOutSuccess = true;
	OutError = TEXT("");

	int32 FailureIdx = StatusLine.Find(TEXT("FAILURE:"));
	if (FailureIdx != INDEX_NONE)
	{
		bOutSuccess = false;
		OutError = StatusLine.Mid(FailureIdx + 8);
	}

	return Output;
}

bool URunPythonScriptTool::ReadScriptFile(const FString& ScriptPath, FString& OutScript, FString& OutError)
{
	// Convert to absolute path if relative
	FString AbsolutePath = ScriptPath;
	if (FPaths::IsRelative(AbsolutePath))
	{
		AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ScriptPath);
	}

	// Check if file exists
	if (!FPaths::FileExists(AbsolutePath))
	{
		OutError = FString::Printf(TEXT("Script file not found: %s"), *AbsolutePath);
		return false;
	}

	// Read file contents
	if (!FFileHelper::LoadFileToString(OutScript, *AbsolutePath))
	{
		OutError = FString::Printf(TEXT("Failed to read script file: %s"), *AbsolutePath);
		return false;
	}

	return true;
}

FString URunPythonScriptTool::BuildPythonCommand(const FString& Script, const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& PythonPaths)
{
	FString FinalScript = Script;
	bool bNeedsPreamble = false;
	FString Preamble;

	// Add Python paths to sys.path if provided
	if (PythonPaths.Num() > 0)
	{
		bNeedsPreamble = true;
		Preamble += TEXT("import sys\n");
		Preamble += TEXT("import os\n");

		for (const FString& Path : PythonPaths)
		{
			// Convert to absolute path if relative
			FString AbsolutePath = Path;
			if (FPaths::IsRelative(AbsolutePath))
			{
				AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
			}

			// Normalize path separators for Python (use forward slashes)
			AbsolutePath = AbsolutePath.Replace(TEXT("\\"), TEXT("/"));

			Preamble += FString::Printf(TEXT("if os.path.exists(r'%s') and r'%s' not in sys.path:\n"), *AbsolutePath, *AbsolutePath);
			Preamble += FString::Printf(TEXT("    sys.path.insert(0, r'%s')\n"), *AbsolutePath);
		}
		Preamble += TEXT("\n");
	}

	// If arguments are provided, inject them as a Python dict accessible via unreal module
	if (Arguments.IsValid() && Arguments->HasField(TEXT("arguments")))
	{
		const TSharedPtr<FJsonObject>* ArgsObject;
		if (Arguments->TryGetObjectField(TEXT("arguments"), ArgsObject))
		{
			bNeedsPreamble = true;

			// Convert JSON object to Python dict string
			FString ArgsJson;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsJson);
			FJsonSerializer::Serialize(ArgsObject->ToSharedRef(), Writer);

			// Inject arguments as a global variable
			Preamble += TEXT("import json\n");
			Preamble += FString::Printf(TEXT("_mcp_args_json = '''%s'''\n"), *ArgsJson);
			Preamble += TEXT("_mcp_args = json.loads(_mcp_args_json)\n");
			Preamble += TEXT("\n");
			Preamble += TEXT("# Make arguments accessible via unreal.get_mcp_args()\n");
			Preamble += TEXT("import unreal\n");
			Preamble += TEXT("if not hasattr(unreal, 'get_mcp_args'):\n");
			Preamble += TEXT("    unreal.get_mcp_args = lambda: _mcp_args\n");
			Preamble += TEXT("\n");
		}
	}

	if (bNeedsPreamble)
	{
		FinalScript = Preamble + FinalScript;
	}

	return FinalScript;
}
