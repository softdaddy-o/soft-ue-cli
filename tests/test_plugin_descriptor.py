"""Tests for the exported SoftUEBridge plugin descriptor."""

from __future__ import annotations

import json
from pathlib import Path


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
    guide_path = Path(__file__).parents[1].joinpath("AGENTS.md")
    if not guide_path.exists():
        return
    guide = guide_path.read_text(encoding="utf-8")

    assert "Do not use REGISTER_BRIDGE_TOOL" in guide
    assert "RegisterToolClass" in guide


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
    root = _repo_root()
    monorepo_path = root / "cli" / "soft_ue_cli" / "skills" / "test-tools.md"
    exported_path = root / "soft_ue_cli" / "skills" / "test-tools.md"
    content = (monorepo_path if monorepo_path.exists() else exported_path).read_text(encoding="utf-8")

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


def test_window_screenshot_avoids_unsafe_pie_world_rendering_path():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp"
    ).read_text(encoding="utf-8")

    assert "IsUnsafeWindowScreenshotDuringPIE" in source
    assert "bDisableWorldRendering" in source
    assert "falling back to viewport capture" in source
    assert "return CaptureViewport(Format, OutputMode)" in source


def test_add_widget_supports_single_child_content_parents():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/AddWidgetTool.cpp"
    ).read_text(encoding="utf-8")

    assert "UContentWidget" in source
    assert "SetContent(NewWidget)" in source
    assert "AddChild(NewWidget)" in source
    assert "GetContent()" in source
    assert "already contains a child" in source
