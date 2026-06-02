"""Tests for the exported SoftUEBridge plugin descriptor."""

from __future__ import annotations

import json
from pathlib import Path

import pytest


def _repo_root() -> Path:
    for parent in Path(__file__).resolve().parents:
        pyproject = parent / "cli" / "pyproject.toml"
        if pyproject.exists():
            return parent
        exported_pyproject = parent / "pyproject.toml"
        if exported_pyproject.exists() and (parent / "soft_ue_cli").exists():
            return parent
    raise AssertionError("Could not locate repository root")


def _descriptor_path() -> Path:
    root = _repo_root()
    monorepo_path = root / "plugin" / "SoftUEBridge" / "SoftUEBridge.uplugin"
    if monorepo_path.exists():
        return monorepo_path
    return root / "soft_ue_cli" / "plugin_data" / "SoftUEBridge" / "SoftUEBridge.uplugin"


def _plugin_source_path(relative: str) -> Path:
    root = _repo_root()
    monorepo_path = root / "plugin" / "SoftUEBridge" / relative
    if monorepo_path.exists():
        return monorepo_path
    return root / "soft_ue_cli" / "plugin_data" / "SoftUEBridge" / relative


def _agent_guide_path() -> Path:
    guide = _repo_root() / "AGENTS.md"
    if not guide.exists():
        pytest.skip("AGENTS.md is not shipped in the public export")
    return guide


def _skill_path(relative: str) -> Path:
    root = _repo_root()
    monorepo_path = root / "cli" / "soft_ue_cli" / "skills" / relative
    if monorepo_path.exists():
        return monorepo_path
    return root / "soft_ue_cli" / "skills" / relative


def test_editor_dependency_plugins_are_editor_target_only():
    descriptor = json.loads(_descriptor_path().read_text(encoding="utf-8"))
    plugin_refs = {entry["Name"]: entry for entry in descriptor["Plugins"]}

    for plugin_name in ["EditorScriptingUtilities", "PythonScriptPlugin", "StateTree"]:
        assert plugin_refs[plugin_name]["TargetAllowList"] == ["Editor"]


def test_runtime_module_stays_developer_tool():
    descriptor = json.loads(_descriptor_path().read_text(encoding="utf-8"))
    modules = {entry["Name"]: entry for entry in descriptor["Modules"]}

    assert modules["SoftUEBridge"]["Type"] == "DeveloperTool"
    assert modules["SoftUEBridgeEditor"]["Type"] == "Editor"


def test_customizable_object_compile_uses_runtime_compile_params():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "CompileParams" in source
    assert "skipifcompiled" in source.lower()
    assert "skipifnotoutofdate" in source.lower()
    assert "gatherreferences" in source.lower()
    assert "IsCompiled" in source
    assert "GetParameterCount" in source


def test_mutable_parameter_inspection_reads_runtime_enum_options():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/MutableIntrospectionUtils.cpp"
    ).read_text(encoding="utf-8")

    assert "GetEnumParameterNumValues" in source
    assert "GetEnumParameterValue" in source
    assert 'SetArrayField(TEXT("options")' in source
    assert "runtime_parameter_count" in source


def test_mutable_runtime_integer_reflection_disambiguates_ue57_overload():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/MutableIntrospectionUtils.cpp"
    ).read_text(encoding="utf-8")

    assert "SetIntPropertyValue" in source
    assert "static_cast<int64>(Value)" in source


def test_customizable_object_pin_fallback_sets_default_object():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "DefaultObject" in source
    assert "LoadObject<UObject>" in source


def test_customizable_object_table_node_gets_details_panel_refresh_sequence():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "RefreshCustomizableObjectTableNodePins" in source
    assert "CustomizableObjectNodeTable" in source
    assert "PreEditChange(Property)" in source
    assert "PostEditChange()" in source
    assert "AllocateDefaultPins()" in source
    assert "Pins.Num() == 0" in source


def test_customizable_object_regenerate_node_pins_tool_is_registered():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Asset/EditCustomizableObjectGraphTool.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "regenerate-customizable-object-node-pins" in header
    assert "URegenerateCustomizableObjectNodePinsTool" in module
    assert "BuildPinList" in source
    assert "pin_count" in source


def test_customizable_object_connect_auto_regenerates_missing_pins_before_error():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "auto_regenerate" in source
    assert "RefreshCustomizableObjectNodePins" in source
    assert "SourceNode == TargetNode" in source
    assert "pin not found after regenerate" in source.lower()


def test_live_coding_reflected_header_check_can_scope_to_module_or_plugin():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Build/TriggerLiveCodingTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Build/TriggerLiveCodingTool.h"
    ).read_text(encoding="utf-8")

    assert 'Schema.Add(TEXT("module")' in source
    assert 'Schema.Add(TEXT("plugin")' in source
    assert "IsHeaderPathInLiveCodingScope" in source
    assert "DetectReflectedHeaderChanges(RiskyHeaders, ModuleScope, PluginScope)" in source
    assert 'FString(TEXT("Plugins/")) + PluginScope' in source
    assert 'FString(TEXT("Source/")) + ModuleScope' in source
    assert "DetectReflectedHeaderChanges(TArray<FString>& OutFiles, const FString& ModuleScope, const FString& PluginScope)" in header


def test_live_coding_cancelled_result_explains_full_build_recovery():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Build/TriggerLiveCodingTool.cpp"
    ).read_text(encoding="utf-8")

    assert 'StatusStr = TEXT("unsupported_change")' in source
    assert 'SetBoolField(TEXT("needs_full_build"), true)' in source
    assert 'SetStringField(TEXT("cancelled_reason")' in source
    assert 'SetStringField(TEXT("recovery_hint")' in source
    assert "build-and-relaunch --wait" in source


def test_build_and_relaunch_worker_writes_progress_status():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Build/BuildAndRelaunchTool.cpp"
    ).read_text(encoding="utf-8")

    assert "function Write-BridgeStatus" in source
    assert "complete = $Complete" in source
    assert "stage = $Stage" in source
    assert "last_stage = $Stage" in source
    assert "build_log_path = $BuildLogPath" in source
    assert "-Stage 'waiting_for_editor_shutdown'" in source
    assert "-Stage 'building'" in source
    assert "-Stage 'relaunching_editor'" in source
    assert "-Stage 'completed'" in source
    assert "-Stage 'worker_error'" in source


def test_bridge_reload_tool_is_runtime_registered_and_cleans_editor_tools():
    runtime_module = _plugin_source_path(
        "Source/SoftUEBridge/Private/SoftUEBridgeModule.cpp"
    ).read_text(encoding="utf-8")
    registry_header = _plugin_source_path(
        "Source/SoftUEBridge/Public/Tools/BridgeToolRegistry.h"
    ).read_text(encoding="utf-8")
    registry_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp"
    ).read_text(encoding="utf-8")
    reload_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/ReloadBridgeModuleTool.cpp"
    ).read_text(encoding="utf-8")

    assert "UReloadBridgeModuleTool" in runtime_module
    assert "reload-bridge-module" in reload_source
    assert "SoftUEBridgeEditor" in reload_source
    assert "SoftUEBridge runtime module cannot reload itself" in reload_source
    assert "RemoveToolsForModule" in registry_header
    assert "ToolModuleNames" in registry_header
    assert "RemoveToolsForModule" in registry_source
    assert "UnloadModule" in reload_source
    assert "LoadModuleWithFailureReason" in reload_source


def test_bridge_registry_remove_tools_avoids_instance_shadow():
    registry_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp"
    ).read_text(encoding="utf-8")

    assert "TObjectPtr<UBridgeToolBase>* FoundInstance" in registry_source
    assert "FoundInstance->Get()->RemoveFromRoot()" in registry_source
    assert "TObjectPtr<UBridgeToolBase>* Instance = ToolInstances.Find" not in registry_source


def test_customizable_object_graph_tool_uses_unique_token_helper_name():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "MatchesAnyToken" in source
    assert "static bool ContainsToken" not in source


def test_runtime_config_tools_are_explicitly_registered_in_startup():
    module = _plugin_source_path(
        "Source/SoftUEBridge/Private/SoftUEBridgeModule.cpp"
    ).read_text(encoding="utf-8")
    get_tool = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/GetConfigValueTool.cpp"
    ).read_text(encoding="utf-8")
    set_tool = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/SetConfigValueTool.cpp"
    ).read_text(encoding="utf-8")
    validate_tool = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/ValidateConfigKeyTool.cpp"
    ).read_text(encoding="utf-8")

    assert "REGISTER_BRIDGE_TOOL(UGetConfigValueTool)" not in get_tool
    assert "REGISTER_BRIDGE_TOOL(USetConfigValueTool)" not in set_tool
    assert "REGISTER_BRIDGE_TOOL(UValidateConfigKeyTool)" not in validate_tool

    assert "Registry.RegisterToolClass<UGetConfigValueTool>()" in module
    assert "Registry.RegisterToolClass<USetConfigValueTool>()" in module
    assert "Registry.RegisterToolClass<UValidateConfigKeyTool>()" in module


def test_bridge_tool_base_has_plugin_unavailable_error_contract():
    header = _plugin_source_path(
        "Source/SoftUEBridge/Public/Tools/BridgeToolBase.h"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/BridgeToolBase.cpp"
    ).read_text(encoding="utf-8")

    assert "PluginUnavailable(" in header
    assert "UBridgeToolBase::PluginUnavailable(" in source
    assert 'SetBoolField(TEXT("success"), false)' in source
    assert 'SetStringField(TEXT("error_code"), TEXT("plugin_unavailable"))' in source
    assert 'SetStringField(TEXT("plugin"), PluginName)' in source
    assert 'SetStringField(TEXT("command"), CommandName)' in source
    assert 'SetStringField(TEXT("recovery"), Recovery)' in source
    assert "FBridgeToolResult::Json(Result)" in source


def test_umg_preview_lifecycle_tools_are_registered_and_registry_lists_handles():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/UMGPreviewTool.h"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/UMGPreviewTool.cpp"
    ).read_text(encoding="utf-8")
    registry_header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/WidgetPreviewRegistry.h"
    ).read_text(encoding="utf-8")
    registry_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/WidgetPreviewRegistry.cpp"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    for tool_name in [
        "umg-preview-create",
        "umg-preview-replace",
        "umg-preview-remove",
        "umg-preview-list",
    ]:
        assert tool_name in header

    assert "UUMGPreviewCreateTool" in module
    assert "UUMGPreviewReplaceTool" in module
    assert "UUMGPreviewRemoveTool" in module
    assert "UUMGPreviewListTool" in module
    assert "FWidgetPreviewSummary" in registry_header
    assert "RemovePreviewByHandle" in registry_header
    assert "ListPreviewsForWorld" in registry_header
    assert "RemovePreviewByHandle" in registry_source
    assert "ListPreviewsForWorld" in registry_source
    assert 'SetStringField(TEXT("preview_handle")' in source
    assert 'SetArrayField(TEXT("root_widgets")' in source


def test_runtime_capture_viewport_tool_is_explicitly_registered_without_static_init():
    module = _plugin_source_path(
        "Source/SoftUEBridge/Private/SoftUEBridgeModule.cpp"
    ).read_text(encoding="utf-8")
    capture_tool = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/CaptureViewportTool.cpp"
    ).read_text(encoding="utf-8")

    assert "REGISTER_BRIDGE_TOOL(UCaptureViewportTool)" not in capture_tool
    assert "Registry.RegisterToolClass<UCaptureViewportTool>()" in module


def test_bridge_registry_remove_tools_does_not_shadow_singleton_instance():
    registry_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp"
    ).read_text(encoding="utf-8")

    assert "TObjectPtr<UBridgeToolBase>* Instance = ToolInstances.Find" not in registry_source
    assert "FoundInstance = ToolInstances.Find" in registry_source


def test_agent_guide_warns_new_tools_against_static_registration_macro():
    guide = _agent_guide_path().read_text(encoding="utf-8")

    assert "Do not use REGISTER_BRIDGE_TOOL" in guide
    assert "RegisterToolClass" in guide


def test_new_anim_tools_are_deferred_until_editor_uclasses_are_ready():
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    startup_body = module.split("void FSoftUEBridgeEditorModule::StartupModule()", 1)[1].split(
        "void FSoftUEBridgeEditorModule::ShutdownModule()", 1
    )[0]

    assert "FCoreDelegates::OnPostEngineInit" in startup_body
    assert "RegisterAnimationTools" in module
    assert "Registry.RegisterToolClass<UAddAnimStateMachineTool>()" not in startup_body
    assert "Registry.RegisterToolClass<UAddAnimStateTool>()" not in startup_body
    assert "Registry.RegisterToolClass<UAddAnimTransitionTool>()" not in startup_body


def test_anim_repoint_references_tool_uses_deferred_registration():
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Animation/AnimRepointReferencesTool.cpp"
    ).read_text(encoding="utf-8")

    startup_body = module.split("void FSoftUEBridgeEditorModule::StartupModule()", 1)[1].split(
        "void FSoftUEBridgeEditorModule::ShutdownModule()", 1
    )[0]

    assert "Tools/Animation/AnimRepointReferencesTool.h" in module
    assert "Registry.RegisterToolClass<UAnimRepointReferencesTool>()" in module
    assert "Registry.RegisterToolClass<UAnimRepointReferencesTool>()" not in startup_body
    assert "REGISTER_BRIDGE_TOOL(UAnimRepointReferencesTool)" not in source
    assert "ReplaceReferredAnimations" in source


def test_metasound_inspect_tool_uses_deferred_registration():
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/InspectMetaSoundTool.cpp"
    ).read_text(encoding="utf-8")

    startup_body = module.split("void FSoftUEBridgeEditorModule::StartupModule()", 1)[1].split(
        "void FSoftUEBridgeEditorModule::ShutdownModule()", 1
    )[0]

    assert "Tools/Asset/InspectMetaSoundTool.h" in module
    assert "Registry.RegisterToolClass<UInspectMetaSoundTool>()" in module
    assert "Registry.RegisterToolClass<UInspectMetaSoundTool>()" not in startup_body
    assert "REGISTER_BRIDGE_TOOL(UInspectMetaSoundTool)" not in source


def test_null_tool_class_registration_is_logged_as_error():
    registry_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp"
    ).read_text(encoding="utf-8")

    assert "RegisterToolClass called with null ToolClass" in registry_source
    assert "UE_LOG(LogSoftUEBridge, Error" in registry_source


def test_bridge_health_includes_process_identity_for_restart_detection():
    server_header = _plugin_source_path(
        "Source/SoftUEBridge/Public/Server/BridgeServer.h"
    ).read_text(encoding="utf-8")
    server_source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Server/BridgeServer.cpp"
    ).read_text(encoding="utf-8")

    assert "BridgeInstanceId" in server_header
    assert "StartedAtUtc" in server_header
    assert 'SetNumberField(TEXT("pid")' in server_source
    assert 'SetStringField(TEXT("started_at")' in server_source
    assert 'SetStringField(TEXT("bridge_instance_id")' in server_source


def test_agent_guide_requires_deferred_registration_for_new_uclass_tools():
    guide = _agent_guide_path().read_text(encoding="utf-8")

    assert "OnPostEngineInit" in guide
    assert "newly added UCLASS" in guide


def test_compile_blueprint_returns_structured_compiler_diagnostics():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/CompileBlueprintTool.cpp"
    ).read_text(encoding="utf-8")

    assert "FCompilerResultsLog" in source
    assert "&ResultsLog" in source
    assert 'SetArrayField(TEXT("diagnostics")' in source
    assert 'SetNumberField(TEXT("error_count")' in source
    assert 'SetNumberField(TEXT("warning_count")' in source
    assert 'SetStringField(TEXT("severity")' in source
    assert 'SetStringField(TEXT("message")' in source
    assert "ToText().ToString()" in source


def test_anim_graph_query_exposes_cache_wrapper_and_binding_metadata():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Blueprint/QueryBlueprintGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "ExtractAnimGraphNodeMetadata" in source
    assert 'SetObjectField(TEXT("anim_graph_node")' in source
    assert 'SetObjectField(TEXT("cache")' in source
    assert 'SetObjectField(TEXT("property_bindings")' in source
    assert 'SetObjectField(TEXT("fast_path")' in source
    assert 'SetStringField(TEXT("cache_name")' in source
    assert 'SetStringField(TEXT("linked_save_node_guid")' in source
    assert "FindLinkedSaveCachedPoseNode" in source


def test_set_node_property_syncs_anim_graph_cache_wrapper_name():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp"
    ).read_text(encoding="utf-8")

    assert "SyncAnimGraphCacheName" in source
    assert "AnimGraphNode_SaveCachedPose" in source
    assert "CachePoseName" in source
    assert "CacheName" in source
    assert "FPropertyChangedEvent" in source


def test_runtime_tools_do_not_use_static_registration_macro():
    runtime_source_root = _plugin_source_path("Source/SoftUEBridge")
    registry_header = _plugin_source_path(
        "Source/SoftUEBridge/Public/Tools/BridgeToolRegistry.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridge/Private/SoftUEBridgeModule.cpp"
    ).read_text(encoding="utf-8")

    macro_users = [
        str(path.relative_to(runtime_source_root)).replace("\\", "/")
        for path in runtime_source_root.rglob("*.cpp")
        if "REGISTER_BRIDGE_TOOL(" in path.read_text(encoding="utf-8")
    ]

    assert "REGISTER_BRIDGE_TOOL" not in registry_header
    assert macro_users == []
    assert "Registry.RegisterToolClass<UCaptureViewportTool>()" in module


def test_customizable_object_remove_tool_is_registered():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Asset/EditCustomizableObjectGraphTool.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    assert "remove-customizable-object-node" in header
    assert "URemoveCustomizableObjectNodeTool" in module


def test_customizable_object_slot_wiring_macro_is_registered_and_wires_expected_chain():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Asset/EditCustomizableObjectGraphTool.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "wire-customizable-object-slot-from-table" in header
    assert "UWireCustomizableObjectSlotFromTableTool" in module
    assert "CustomizableObjectNodeTable" in source
    assert "CustomizableObjectNodeMaterial" in source
    assert "CONodeMaterialConstant" in source
    assert 'SetArrayField(TEXT("CompilationFilterOptions")' in source
    assert "OperationType" in source
    assert "TCFOT_OR" in source
    assert "TCFOT_AND" in source
    assert 'SetStringField(TEXT("FilterOperation")' not in source
    assert "material_assignment_mode" in source
    assert "material_node_property" in source
    assert "Mesh LOD_%d Mat_%d" in source
    assert "Mesh_Input_Pin" in source
    assert "Material_Input_Pin" in source
    assert "Mesh Section_Output_Pin" in source
    assert "created_edges" in source


def test_pie_screenshot_capture_uses_composited_game_viewport_and_safe_window_fallback():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Editor/CaptureScreenshotTool.h"
    ).read_text(encoding="utf-8")

    assert "pie-window" in source
    assert "CapturePIEWindow" in source
    assert "FindPIEGameViewportWidget" in source
    assert "GetGameViewportWidget" in source
    assert "safe_mode" in source
    assert "unsafe_slate_window_capture" in source
    assert "unsafe_window_capture" in source
    assert "fallback_reason" in source
    assert "requested_mode" in source
    assert "native_window_handle" in source
    assert "CapturePIEWindow" in header


def test_pie_window_capture_defaults_to_safe_viewport_fallback():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Editor/CaptureScreenshotTool.h"
    ).read_text(encoding="utf-8")

    assert "pie_window_safe_viewport_fallback" in source
    assert "pie_window_slate_capture_opt_in" in source
    assert "bool bSafeMode" in header
    assert "if (bSafeMode)" in source
    assert "TakeWidgetScreenshot(PIEViewportWidget.ToSharedRef()" in source
    assert "unsafe_slate_window_capture" in source


def test_pie_session_blueprint_compile_error_policy_is_non_modal_when_requested():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/PIE/PieSessionTool.h"
    ).read_text(encoding="utf-8")

    assert "blueprint_error_action" in source
    assert "preflight_blueprints" in source
    assert "bDisplayCompilePIEWarning" in source
    assert "BS_Error" in source
    assert "blueprint_compile_errors" in source
    assert "blocked_by_blueprint_compile_errors" in source
    assert "pie_started" in source
    assert "FindBlueprintCompileErrors" in header


def test_pie_session_start_uses_slate_ticker_and_nonblocking_diagnostics():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/PIE/PieSessionTool.h"
    ).read_text(encoding="utf-8")

    assert "GetExecutionContextRequirement" in header
    assert "EBridgeToolExecutionContext::SlateTicker" in header
    assert "non_blocking_start" in source
    assert "start_request_dispatched" in source
    assert "transition_age_seconds" in source


def test_create_asset_widget_blueprint_honors_parent_class():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/CreateAssetTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Write/CreateAssetTool.h"
    ).read_text(encoding="utf-8")

    assert "CreateWidgetBlueprint(PackagePath, AssetName, ParentClass" in source
    assert "const FString& ParentClassName" in header
    assert "ParentClassName" in source
    assert "ResolveClass(ParentClassName" in source
    assert "IsChildOf(UUserWidget::StaticClass())" in source
    assert 'SetStringField(TEXT("parent_class_path")' in source


def test_add_widget_accepts_widget_blueprint_child_classes():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/AddWidgetTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "ResolveWidgetClass",
        "LoadClass<UWidget>",
        "UWidgetBlueprint",
        "GeneratedClass",
        "UUserWidget::StaticClass",
        "child_user_widget_class",
        "resolved_widget_class_path",
    ):
        assert token in source


def test_create_customizable_object_from_spec_tool_is_registered_and_uses_graph_helpers():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Asset/EditCustomizableObjectGraphTool.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp"
    ).read_text(encoding="utf-8")

    assert "create-customizable-object-from-spec" in header
    assert "UCreateCustomizableObjectFromSpecTool" in module
    assert "CreateCustomizableObjectGraphNode" in source
    assert 'TryGetArrayField(TEXT("nodes")' in source
    assert 'TryGetArrayField(TEXT("edges")' in source
    assert "spec_node_id" in source
    assert "created_edges" in source


def test_set_node_position_supports_customizable_object_graphs():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/SetNodePositionTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Write/SetNodePositionTool.h"
    ).read_text(encoding="utf-8")

    assert "CustomizableObject" in header
    assert "LooksLikeCustomizableObject" in source
    assert "FindCustomizableObjectGraph" in source
    assert "Asset must be a Material, Blueprint, AnimBlueprint, or CustomizableObject" in source
    assert "FBridgeAssetModifier::MarkPackageDirty(Object)" in source


def test_live_smoke_skill_expects_slot_wiring_macro():
    content = _skill_path("test-tools.md").read_text(encoding="utf-8")

    assert "wire-customizable-object-slot-from-table" in content


def test_datatable_row_tool_uses_field_level_bridge_deserializer():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/AddDataTableRowTool.cpp"
    ).read_text(encoding="utf-8")

    assert 'TryGetObjectField(TEXT("row_data")' in source
    assert "DeserializePropertyValue" in source
    assert "failed_fields" in source


def test_editor_screenshot_compression_validates_dimensions_and_pixel_count():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")

    assert "Width <= 0 || Height <= 0" in source
    assert "const int64 ExpectedPixelCount" in source
    assert "RawData.Num() != ExpectedPixelCount" in source
    assert "ExpectedPixelCount * 4" in source


def test_viewport_capture_supports_resize_and_color_transform_options():
    source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/CaptureViewportTool.cpp"
    ).read_text(encoding="utf-8")

    for field in ("scale", "width", "height", "color_mode", "cleanup_previous"):
        assert f'Schema.Add(TEXT("{field}")' in source

    assert "ApplyImageTransform" in source
    assert "ResizeImage" in source
    assert "ApplyColorMode" in source
    assert 'SetNumberField(TEXT("width")' in source
    assert 'SetNumberField(TEXT("original_width")' in source
    assert 'SetStringField(TEXT("color_mode")' in source


def test_viewport_capture_file_output_does_not_cleanup_by_default():
    source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/CaptureViewportTool.cpp"
    ).read_text(encoding="utf-8")

    assert "if (bCleanupPrevious)" in source
    assert "CleanupPreviousCaptures(TempDir);" not in source.replace(
        "if (bCleanupPrevious)\n\t{\n\t\tCleanupPreviousCaptures(TempDir);\n\t}", ""
    )


def test_editor_screenshot_forwards_viewport_transform_options():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")

    for field in ("scale", "width", "height", "color_mode", "cleanup_previous"):
        assert f'Schema.Add(TEXT("{field}")' in source

    assert 'Args->SetNumberField(TEXT("scale")' in source
    assert 'Args->SetNumberField(TEXT("width")' in source
    assert 'Args->SetNumberField(TEXT("height")' in source
    assert 'Args->SetStringField(TEXT("color_mode")' in source
    assert 'Args->SetBoolField(TEXT("cleanup_previous")' in source


def test_bridge_tools_declare_safe_execution_context_metadata():
    base_header = _plugin_source_path("Source/SoftUEBridge/Public/Tools/BridgeToolBase.h").read_text(encoding="utf-8")
    protocol_header = _plugin_source_path("Source/SoftUEBridge/Public/Protocol/BridgeTypes.h").read_text(encoding="utf-8")
    protocol_source = _plugin_source_path("Source/SoftUEBridge/Private/Protocol/BridgeTypes.cpp").read_text(encoding="utf-8")
    registry_header = _plugin_source_path("Source/SoftUEBridge/Public/Tools/BridgeToolRegistry.h").read_text(encoding="utf-8")
    registry_source = _plugin_source_path("Source/SoftUEBridge/Private/Tools/BridgeToolRegistry.cpp").read_text(encoding="utf-8")
    server_source = _plugin_source_path("Source/SoftUEBridge/Private/Server/BridgeServer.cpp").read_text(encoding="utf-8")
    pie_header = _plugin_source_path("Source/SoftUEBridgeEditor/Public/Tools/PIE/PieTickTool.h").read_text(encoding="utf-8")

    assert "enum class EBridgeToolExecutionContext" in base_header
    assert "GetExecutionContextRequirement" in base_header
    assert "ExecutionContext" in protocol_header
    assert 'SetStringField(TEXT("executionContext")' in protocol_source
    assert "GetToolExecutionContext" in registry_header
    assert "unsafe_execution_context" in registry_source
    assert "ScheduleToolRequest" in server_source
    assert "EBridgeToolExecutionContext::PIEWorldTickSafe" in pie_header


def test_pie_tick_reports_structured_timeout_and_phase_diagnostics():
    source = _plugin_source_path("Source/SoftUEBridgeEditor/Private/Tools/PIE/PieTickTool.cpp").read_text(encoding="utf-8")
    header = _plugin_source_path("Source/SoftUEBridgeEditor/Public/Tools/PIE/PieTickTool.h").read_text(encoding="utf-8")

    for token in (
        "BuildPieTickTimeoutResult",
        "active_phase",
        "requested_frames",
        "frames_completed",
        "elapsed_seconds",
        "timeout_seconds",
        "remaining_budget_seconds",
        "map_load",
        "request_play_session",
        "wait_for_play_world",
        "tick_frames",
    ):
        assert token in source or token in header


def test_window_screenshot_avoids_unsafe_pie_world_rendering_path():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")

    assert "IsUnsafeWindowScreenshotDuringPIE" in source
    assert "bDisableWorldRendering" in source
    assert "falling back to viewport capture" in source
    assert "return CaptureViewport(Format, OutputMode, ScalePercent, TargetWidth, TargetHeight, ColorMode, bCleanupPrevious)" in source


def test_apply_widget_tree_tool_is_explicitly_registered_and_supports_designer_spec():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/ApplyWidgetTreeTool.h"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/ApplyWidgetTreeTool.cpp"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    assert "apply-widget-tree" in header
    assert "UApplyWidgetTreeTool" in module
    assert "Registry.RegisterToolClass<UApplyWidgetTreeTool>()" in module
    assert "REGISTER_BRIDGE_TOOL(UApplyWidgetTreeTool)" not in source

    for widget_class in (
        "CanvasPanel",
        "Overlay",
        "Border",
        "SizeBox",
        "ScaleBox",
        "Image",
        "TextBlock",
        "Button",
        "HorizontalBox",
        "VerticalBox",
        "UniformGridPanel",
        "GridPanel",
        "ScrollBox",
        "Spacer",
        "WidgetSwitcher",
    ):
        assert widget_class in source

    for schema_field in ("spec", "replace", "compile", "save", "checkout"):
        assert f'Schema.Add(TEXT("{schema_field}")' in source

    assert "ConstructWidget" in source
    assert "DetachDesignerTree" in source
    assert "RemoveWidget" in source
    assert "REN_DontCreateRedirectors" in source
    assert "ApplyWidgetProperties" in source
    assert "ApplySlotProperties" in source
    assert "SetText(FText::FromString" in source
    assert "SetBrushFromTexture" in source
    assert "SetBrushFromMaterial" in source
    assert "SetRenderOpacity" in source
    assert "SetVisibility" in source
    assert "SetZOrder" in source
    assert "SetOffsets" in source
    assert 'SetNumberField(TEXT("removed_widget_count")' in source
    assert "CompileBlueprint" in source
    assert "SaveAsset" in source


def test_umg_workflow_tools_are_explicitly_registered_and_support_runtime_contracts():
    wire_header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/WireWidgetNavigationTool.h"
    ).read_text(encoding="utf-8")
    wire_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/WireWidgetNavigationTool.cpp"
    ).read_text(encoding="utf-8")
    verify_header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/VerifyUMGWorkflowTool.h"
    ).read_text(encoding="utf-8")
    verify_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/VerifyUMGWorkflowTool.cpp"
    ).read_text(encoding="utf-8")
    preview_registry_header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/WidgetPreviewRegistry.h"
    ).read_text(encoding="utf-8")
    preview_registry_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/WidgetPreviewRegistry.cpp"
    ).read_text(encoding="utf-8")
    pie_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp"
    ).read_text(encoding="utf-8")
    inspect_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/InspectRuntimeWidgetsTool.cpp"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    assert "wire-widget-navigation" in wire_header
    assert "verify-umg-workflow" in verify_header
    assert "Registry.RegisterToolClass<UWireWidgetNavigationTool>()" in module
    assert "Registry.RegisterToolClass<UVerifyUMGWorkflowTool>()" in module
    assert "REGISTER_BRIDGE_TOOL(UWireWidgetNavigationTool)" not in wire_source
    assert "REGISTER_BRIDGE_TOOL(UVerifyUMGWorkflowTool)" not in verify_source

    for token in (
        "bindings",
        "allow_pie",
        "allow_busy",
        "GIsSavingPackage",
        "IsGarbageCollecting",
        "IsPlaySessionInProgress",
        "editor_state",
        "blocked_active_pie",
        "button",
        "WidgetSwitcher",
        "bIsVariable",
        "CompileBlueprint",
        "SaveAsset",
        "parent_binding_contract",
    ):
        assert token in wire_source

    for token in (
        "CreateWidget",
        "AddToViewport",
        "expected_widgets",
        "expected_text",
        "click_sequence",
        "OnClicked.Broadcast",
        "GetActiveWidgetIndex",
        "capture_after",
        "preview_lifecycle",
        "FWidgetPreviewRegistry::RemovePreviewsForWorld",
        "FWidgetPreviewRegistry::RegisterPreview",
        "RemoveFromParent",
    ):
        assert token in verify_source

    assert "RemovePreviewsForWorld" in preview_registry_header
    assert "TWeakObjectPtr<UUserWidget>" in preview_registry_source
    assert "FWidgetPreviewRegistry::RemovePreviewsForWorld" in pie_source
    assert "cleanup_tool_previews" in pie_source

    assert "UUserWidget" in inspect_source
    assert "WidgetTree->RootWidget" in inspect_source


def test_umg_runtime_lookup_includes_tool_owned_preview_registry():
    verify_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/VerifyUMGWorkflowTool.cpp"
    ).read_text(encoding="utf-8")
    inspect_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/InspectRuntimeWidgetsTool.cpp"
    ).read_text(encoding="utf-8")
    preview_registry_header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Widget/WidgetPreviewRegistry.h"
    ).read_text(encoding="utf-8")
    preview_registry_source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/WidgetPreviewRegistry.cpp"
    ).read_text(encoding="utf-8")

    assert "CollectPreviewWidgetsForWorld" in preview_registry_header
    assert "CollectPreviewWidgetsForWorld" in preview_registry_source
    assert "FWidgetPreviewRegistry::CollectPreviewWidgetsForWorld" in verify_source
    assert "FWidgetPreviewRegistry::CollectPreviewWidgetsForWorld" in inspect_source
    assert "Runtime root widget not found" in verify_source
    assert "current tool previews" in verify_source


def test_umg_preview_tool_applies_viewport_layout_controls():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Widget/UMGPreviewTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "fullscreen",
        "viewport_anchors",
        "viewport_position",
        "viewport_size",
        "viewport_alignment",
        "SetAnchorsInViewport",
        "SetAlignmentInViewport",
        "SetPositionInViewport",
        "SetDesiredSizeInViewport",
        "viewport_layout",
    ):
        assert token in source


def test_capture_screenshot_tool_returns_structured_pie_fallback_diagnostics():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "structured_exception",
        "capture_failure_kind",
        "fallback_command",
        "safe_mode",
        "capture-screenshot viewport",
    ):
        assert token in source


def test_call_function_reports_process_event_invocation_evidence():
    source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/CallFunctionTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "BuildInvocationEvidence",
        "invocation_evidence",
        "process_event_dispatched",
        "dispatch_path",
        "target_class",
        "target_path",
        "function_path",
        "function_flags",
        "is_native",
        "is_blueprint_callable",
        "is_exec",
        "input_param_count",
        "out_param_count",
        "has_return_value",
        "unobservable_no_return_or_out_params",
    ):
        assert token in source


def test_build_and_relaunch_uses_absolute_paths_and_startup_marker():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Build/BuildAndRelaunchTool.cpp"
    ).read_text(encoding="utf-8")
    build_cs = _plugin_source_path(
        "Source/SoftUEBridgeEditor/SoftUEBridgeEditor.Build.cs"
    ).read_text(encoding="utf-8")

    assert "ConvertRelativePathToFull(FPaths::EngineDir())" in source
    assert "ResolveEngineDirForBuild" in source
    assert "GetEngineRootDirFromIdentifier" in source
    assert "EngineAssociation" in source
    assert "compiler_version" in source
    assert "toolchain" in source
    assert "-CompilerVersion=$CompilerVersion" in source
    assert "-Compiler=$Compiler" in source
    assert '"DesktopPlatform"' in build_cs
    assert "BuildAndRelaunch.started" in source
    assert "$StartupMarkerPath" in source
    assert "worker_failed_to_start" in source
    assert 'Schema.Add(TEXT("startup_marker_timeout")' in source
    assert "StartupMarkerTimeoutSeconds" in source
    assert "within %.0fs" in source
    assert "-File" in source
    assert "Start-Process -WindowStyle Hidden -FilePath powershell.exe" not in source


def test_set_node_property_accepts_call_function_reference_string_shorthand():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "ApplyCallFunctionReferenceStringShortcut",
        "UK2Node_CallFunction",
        "FunctionReference",
        "GetTargetFunction",
        "SetFromFunction",
        "MemberName",
        "FunctionReference string shorthand",
    ):
        assert token in source


def test_connect_graph_pins_validates_actual_link_creation():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/ConnectGraphPinsTool.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "ValidateCreatedPinLink",
        "pins_in_different_graphs",
        "MakeLinkTo",
        "PinConnectionListChanged",
        "validated_link",
        "connection_method",
    ):
        assert token in source


def test_query_blueprint_graph_exposes_recursive_anim_filters_and_paths():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Blueprint/QueryBlueprintGraphTool.cpp"
    ).read_text(encoding="utf-8")
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Blueprint/QueryBlueprintGraphTool.h"
    ).read_text(encoding="utf-8")

    for token in (
        'Schema.Add(TEXT("recursive")',
        'Schema.Add(TEXT("node_class")',
        "graph_path",
        "NodeClassFilters",
        "BuildGraphPath",
        "MatchesNodeClassFilter",
    ):
        assert token in source or token in header


def test_animation_sync_marker_tools_are_registered_and_mutate_authored_markers():
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Animation/AnimSyncMarkerTools.cpp"
    ).read_text(encoding="utf-8")

    for token in (
        "UInspectSyncMarkersTool",
        "UCompareSyncMarkersTool",
        "UAddSyncMarkerTool",
        "URemoveSyncMarkerTool",
    ):
        assert token in module
        assert token in source

    for token in (
        "AuthoredSyncMarkers",
        "MarkerName",
        "Time",
        "BridgeAssetModifier",
        "MarkPackageDirty",
    ):
        assert token in source


def test_rewind_snapshot_and_overview_read_animation_provider_data():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Rewind/RewindHelper.cpp"
    ).read_text(encoding="utf-8")
    build_cs = _plugin_source_path(
        "Source/SoftUEBridgeEditor/SoftUEBridgeEditor.Build.cs"
    ).read_text(encoding="utf-8")
    descriptor = _plugin_source_path("SoftUEBridge.uplugin").read_text(encoding="utf-8")

    for token in (
        "GetAnalysisSession",
        "IAnimationProvider",
        "IGameplayProvider",
        "ReadStateMachinesTimeline",
        "ReadTickRecordTimeline",
        "ReadAnimGraphTimeline",
        "ReadAnimNodesTimeline",
        "ReadMontageTimeline",
        "ReadNotifyTimeline",
        "candidate_object_ids",
        "asset_players",
        "anim_graph",
        "flat_nodes",
    ):
        assert token in source

    assert '"GameplayInsights"' in build_cs
    assert '"Name": "GameplayInsights"' in descriptor


def test_run_automation_tests_tool_is_registered_and_structured():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Testing/RunAutomationTestsTool.h"
    ).read_text(encoding="utf-8")
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Testing/RunAutomationTestsTool.cpp"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    assert "run-automation-tests" in header
    assert "URunAutomationTestsTool" in module
    assert "FAutomationTestFramework" in source
    assert "GetValidTestNames" in source
    assert "StartTestByName" in source
    assert "StopTest" in source
    assert 'SetArrayField(TEXT("tests")' in source
    assert 'SetArrayField(TEXT("error_messages")' in source

def test_add_widget_supports_single_child_content_parents():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/AddWidgetTool.cpp"
    ).read_text(encoding="utf-8")

    assert "UContentWidget" in source
    assert "SetContent(NewWidget)" in source
    assert "AddChild(NewWidget)" in source
    assert "GetContent()" in source
    assert "already contains a child" in source


def test_trigger_input_routes_keys_through_player_controller_and_enhanced_input():
    source = _plugin_source_path(
        "Source/SoftUEBridge/Private/Tools/TriggerInputTool.cpp"
    ).read_text(encoding="utf-8")
    build_cs = _plugin_source_path("Source/SoftUEBridge/SoftUEBridge.Build.cs").read_text(encoding="utf-8")
    descriptor = _plugin_source_path("SoftUEBridge.uplugin").read_text(encoding="utf-8")

    assert "PC->InputKey" in source
    assert "PlayerInput->InputKey" not in source
    assert "UEnhancedPlayerInput" in source
    assert "InjectInputForAction" in source
    assert "FindEnhancedInputAction" in source
    assert '"EnhancedInput"' in build_cs
    assert '"Name": "EnhancedInput"' in descriptor
