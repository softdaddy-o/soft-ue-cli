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


def test_customizable_object_remove_tool_is_registered():
    header = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Public/Tools/Asset/EditCustomizableObjectGraphTool.h"
    ).read_text(encoding="utf-8")
    module = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp"
    ).read_text(encoding="utf-8")

    assert "remove-customizable-object-node" in header
    assert "URemoveCustomizableObjectNodeTool" in module


def test_datatable_row_tool_uses_field_level_bridge_deserializer():
    source = _plugin_source_path(
        "Source/SoftUEBridgeEditor/Private/Tools/Write/AddDataTableRowTool.cpp"
    ).read_text(encoding="utf-8")

    assert 'TryGetObjectField(TEXT("row_data")' in source
    assert "DeserializePropertyValue" in source
    assert "failed_fields" in source
