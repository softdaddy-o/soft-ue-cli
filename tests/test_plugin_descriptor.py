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
