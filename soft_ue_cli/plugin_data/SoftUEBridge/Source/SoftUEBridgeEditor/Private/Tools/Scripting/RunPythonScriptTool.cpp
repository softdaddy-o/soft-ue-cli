// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Scripting/RunPythonScriptTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tools/Scripting/BridgePythonCallHelper.h"
#include "Engine/Engine.h"

FString URunPythonScriptTool::GetToolDescription() const
{
	return TEXT("Execute a Python script in Unreal Editor's Python environment. "
		"Requires PythonScriptPlugin to be enabled. Supports inline code, script files, "
		"arguments, extra sys.path entries, optional PIE/editor world helpers, and "
		"file execution that preserves __file__ and __future__ imports.");
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

	FBridgeSchemaProperty World;
	World.Type = TEXT("string");
	World.Description = TEXT("Optional world helper to expose during execution: 'editor', 'pie', or 'game' (default: editor)");
	World.bRequired = false;
	Schema.Add(TEXT("world"), World);

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

	FString ResolvedScriptPath;

	// If script_path is provided, read the file for validation and preserve the path for file execution.
	if (!ScriptPath.IsEmpty())
	{
		FString ReadError;
		if (!ReadScriptFile(ScriptPath, ResolvedScriptPath, Script, ReadError))
		{
			return FBridgeToolResult::Error(ReadError);
		}
		UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Loaded script from %s"), *ResolvedScriptPath);
	}

	if (ContainsUnsafeLevelLoad(Script))
	{
		return FBridgeToolResult::Error(
			TEXT("run-python-script: map-loading APIs such as unreal.EditorLoadingAndSavingUtils.load_map() and "
				 "unreal.EditorLevelLibrary.load_level() are not supported here because they can tear down the active "
				 "Python execution context. Use open-asset on the map outside run-python-script instead."));
	}

	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor")).ToLower();
	if (!WorldType.IsEmpty() && WorldType != TEXT("editor") && WorldType != TEXT("pie") && WorldType != TEXT("game"))
	{
		return FBridgeToolResult::Error(TEXT("Invalid world value. Use 'editor', 'pie', or 'game'."));
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

	// Build and execute the shared preamble first so file-based execution still gets helpers and arguments
	const FString PythonPreamble = BuildPythonPreamble(Arguments, PythonPaths, WorldType);
	bool bSuccess = false;
	FString Error;
	FString Output;

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("run-python-script: Executing Python code..."));

	if (!ScriptPath.IsEmpty() && !PythonPreamble.IsEmpty())
	{
		FString PreambleOutput;
		if (!ExecutePythonCommand(
			PythonPreamble,
			EPythonCommandExecutionMode::ExecuteFile,
			EPythonFileExecutionScope::Public,
			bSuccess,
			PreambleOutput,
			Error))
		{
			return FBridgeToolResult::Error(TEXT("Failed to execute Python preamble"));
		}

		Output = MoveTemp(PreambleOutput);
		if (!bSuccess)
		{
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), Error);
			if (!Output.IsEmpty())
			{
				Result->SetStringField(TEXT("output"), Output);
			}
			return FBridgeToolResult::Json(Result);
		}
	}

	FString ScriptOutput;
	if (!ScriptPath.IsEmpty())
	{
		if (!ExecutePythonCommand(
			ResolvedScriptPath,
			EPythonCommandExecutionMode::ExecuteFile,
			EPythonFileExecutionScope::Public,
			bSuccess,
			ScriptOutput,
			Error))
		{
			return FBridgeToolResult::Error(TEXT("Failed to execute Python script file"));
		}
	}
	else
	{
		const FString InlineCommand = PythonPreamble + Script;
		if (!ExecutePythonCommand(
			InlineCommand,
			EPythonCommandExecutionMode::ExecuteFile,
			EPythonFileExecutionScope::Public,
			bSuccess,
			ScriptOutput,
			Error))
		{
			return FBridgeToolResult::Error(TEXT("Failed to execute inline Python script"));
		}
		Output.Reset();
	}

	if (!Output.IsEmpty() && !ScriptOutput.IsEmpty())
	{
		Output += TEXT("\n");
	}
	Output += ScriptOutput;

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (bSuccess)
	{
		Result->SetStringField(TEXT("output"), Output);
		if (!ScriptPath.IsEmpty())
		{
			Result->SetStringField(TEXT("script_path"), ResolvedScriptPath);
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

bool URunPythonScriptTool::ExecutePythonCommand(
	const FString& Command,
	EPythonCommandExecutionMode ExecutionMode,
	EPythonFileExecutionScope FileExecutionScope,
	bool& bOutSuccess,
	FString& OutOutput,
	FString& OutError)
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		bOutSuccess = false;
		OutError = TEXT("PythonScriptPlugin interface not available");
		OutOutput.Reset();
		return false;
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Flags = EPythonCommandFlags::Unattended;
	PythonCommand.ExecutionMode = ExecutionMode;
	PythonCommand.FileExecutionScope = FileExecutionScope;
	PythonCommand.Command = Command;
	bOutSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	// Force garbage collection to clean up any orphaned objects created by the script
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	OutOutput.Reset();
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (!Entry.Output.IsEmpty())
		{
			if (!OutOutput.IsEmpty())
			{
				OutOutput += TEXT("\n");
			}
			OutOutput += Entry.Output;
		}
	}

	OutError = PythonCommand.CommandResult;
	if (!bOutSuccess && OutError.IsEmpty())
	{
		OutError = OutOutput;
	}

	return true;
}

bool URunPythonScriptTool::ReadScriptFile(
	const FString& ScriptPath,
	FString& OutResolvedPath,
	FString& OutScript,
	FString& OutError)
{
	// Convert to absolute path if relative
	OutResolvedPath = ScriptPath;
	if (FPaths::IsRelative(OutResolvedPath))
	{
		OutResolvedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ScriptPath);
	}

	// Check if file exists
	if (!FPaths::FileExists(OutResolvedPath))
	{
		OutError = FString::Printf(TEXT("Script file not found: %s"), *OutResolvedPath);
		return false;
	}

	// Read file contents
	if (!FFileHelper::LoadFileToString(OutScript, *OutResolvedPath))
	{
		OutError = FString::Printf(TEXT("Failed to read script file: %s"), *OutResolvedPath);
		return false;
	}

	return true;
}

FString URunPythonScriptTool::BuildPythonPreamble(
	const TSharedPtr<FJsonObject>& Arguments,
	const TArray<FString>& PythonPaths,
	const FString& WorldType)
{
	FString Preamble;
	Preamble += TEXT("import sys as _sub_sys\n");
	Preamble += TEXT("import json as _sub_json\n");
	Preamble += TEXT("import types as _sub_types\n");
	Preamble += TEXT("import unreal as _sub_unreal\n");
	Preamble += TEXT("if 'soft_ue_bridge' not in _sub_sys.modules:\n");
	Preamble += TEXT("    _sub_mod = _sub_types.ModuleType('soft_ue_bridge')\n");
	Preamble += TEXT("    class _SubBridgeCallError(Exception):\n");
	Preamble += TEXT("        def __init__(self, message, tool_name=None, payload=None):\n");
	Preamble += TEXT("            super().__init__(message)\n");
	Preamble += TEXT("            self.tool_name = tool_name\n");
	Preamble += TEXT("            self.payload = payload\n");
	Preamble += TEXT("    def _sub_call(tool_name, args=None):\n");
	Preamble += TEXT("        _args_json = _sub_json.dumps(args or {})\n");
	Preamble += TEXT("        _res_json = _sub_unreal.BridgePythonCallHelper.call_tool(tool_name, _args_json)\n");
	Preamble += TEXT("        _res = _sub_json.loads(_res_json) if _res_json else {}\n");
	Preamble += TEXT("        if _res.get('is_error'):\n");
	Preamble += TEXT("            raise _SubBridgeCallError(_res.get('error_message', 'unknown error'), tool_name=tool_name, payload=_res)\n");
	Preamble += TEXT("        return _res\n");
	Preamble += TEXT("    _sub_mod.call = _sub_call\n");
	Preamble += TEXT("    _sub_mod.BridgeCallError = _SubBridgeCallError\n");
	Preamble += TEXT("    _sub_sys.modules['soft_ue_bridge'] = _sub_mod\n");
	Preamble += TEXT("\n");
	Preamble += FString::Printf(TEXT("_mcp_world_type = r'%s'\n"), *WorldType);
	Preamble += TEXT("def _sub_get_world():\n");
	Preamble += TEXT("    if _mcp_world_type == 'pie':\n");
	Preamble += TEXT("        _editor = _sub_unreal.get_editor_subsystem(_sub_unreal.UnrealEditorSubsystem)\n");
	Preamble += TEXT("        if _editor:\n");
	Preamble += TEXT("            _world = _editor.get_game_world()\n");
	Preamble += TEXT("            if _world:\n");
	Preamble += TEXT("                return _world\n");
	Preamble += TEXT("        raise RuntimeError('PIE world not found. Start PIE or omit --world pie.')\n");
	Preamble += TEXT("    if _mcp_world_type == 'game':\n");
	Preamble += TEXT("        _editor = _sub_unreal.get_editor_subsystem(_sub_unreal.UnrealEditorSubsystem)\n");
	Preamble += TEXT("        if _editor:\n");
	Preamble += TEXT("            _world = _editor.get_game_world()\n");
	Preamble += TEXT("            if _world:\n");
	Preamble += TEXT("                return _world\n");
	Preamble += TEXT("        return _sub_unreal.EditorLevelLibrary.get_editor_world()\n");
	Preamble += TEXT("    return _sub_unreal.EditorLevelLibrary.get_editor_world()\n");
	Preamble += TEXT("if not hasattr(_sub_unreal, 'get_mcp_world'):\n");
	Preamble += TEXT("    _sub_unreal.get_mcp_world = _sub_get_world\n");
	Preamble += TEXT("if 'soft_ue_bridge' in _sub_sys.modules and not hasattr(_sub_sys.modules['soft_ue_bridge'], 'get_world'):\n");
	Preamble += TEXT("    _sub_sys.modules['soft_ue_bridge'].get_world = _sub_get_world\n");
	Preamble += TEXT("_mcp_world = _sub_get_world()\n");
	Preamble += TEXT("world = _mcp_world\n");
	Preamble += TEXT("\n");

	// Add Python paths to sys.path if provided
	if (PythonPaths.Num() > 0)
	{
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

			Preamble += FString::Printf(TEXT("if os.path.exists(r'%s') and r'%s' not in _sub_sys.path:\n"), *AbsolutePath, *AbsolutePath);
			Preamble += FString::Printf(TEXT("    _sub_sys.path.insert(0, r'%s')\n"), *AbsolutePath);
		}
		Preamble += TEXT("\n");
	}

	// If arguments are provided, inject them as a Python dict accessible via unreal module
	if (Arguments.IsValid() && Arguments->HasField(TEXT("arguments")))
	{
		const TSharedPtr<FJsonObject>* ArgsObject;
		if (Arguments->TryGetObjectField(TEXT("arguments"), ArgsObject))
		{
			// Convert JSON object to Python dict string
			FString ArgsJson;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsJson);
			FJsonSerializer::Serialize((*ArgsObject).ToSharedRef(), Writer);

			// Inject arguments as a global variable
			Preamble += FString::Printf(TEXT("_mcp_args_json = '''%s'''\n"), *ArgsJson);
			Preamble += TEXT("_mcp_args = _sub_json.loads(_mcp_args_json)\n");
			Preamble += TEXT("\n");
			Preamble += TEXT("# Make arguments accessible via unreal.get_mcp_args()\n");
			Preamble += TEXT("if not hasattr(_sub_unreal, 'get_mcp_args'):\n");
			Preamble += TEXT("    _sub_unreal.get_mcp_args = lambda: _mcp_args\n");
			Preamble += TEXT("\n");
		}
	}

	return Preamble;
}

bool URunPythonScriptTool::ContainsUnsafeLevelLoad(const FString& Script) const
{
	const FString LowerScript = Script.ToLower();
	return LowerScript.Contains(TEXT("editorloadingandsavingutils.load_map("))
		|| LowerScript.Contains(TEXT("editorlevellibrary.load_level("));
}
