"""Tests for the diff_uasset public API."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))

import soft_ue_cli.uasset as uasset_mod


def test_diff_uasset_summary(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "BP_Test",
        "ue_version": "5.4",
        "asset_class": "Blueprint",
        "parent_class": "Actor",
        "parent_class_path": "/Script/Engine.Actor",
        "blueprint_type": "Normal",
        "variable_count": 1,
        "function_count": 0,
        "component_count": 0,
        "event_count": 0,
    }
    right = {
        **left,
        "parent_class": "Character",
        "parent_class_path": "/Script/Engine.Character",
        "blueprint_type": "Interface",
    }

    calls = []

    def fake_inspect(path, sections):
        calls.append((path, tuple(sections) if not isinstance(sections, str) else sections))
        return left if "left" in str(path) else right

    monkeypatch.setattr(uasset_mod, "inspect_uasset", fake_inspect)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset")
    assert result["has_changes"] is True
    assert result["total_changes"] == 3
    assert result["changes"]["summary"]["modified"]["parent_class"]["new"] == "Character"
    assert result["changes"]["summary"]["modified"]["blueprint_type"]["new"] == "Interface"
    assert len(calls) == 2


def test_diff_uasset_named_sections(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "BP_Test",
        "ue_version": "5.4",
        "asset_class": "Blueprint",
        "parent_class": "Actor",
        "parent_class_path": "/Script/Engine.Actor",
        "blueprint_type": "Normal",
        "variables": {
            "count": 1,
            "items": [{"name": "Health", "type": "float"}],
            "fidelity": "partial",
        },
    }
    right = {
        **left,
        "variables": {
            "count": 2,
            "items": [{"name": "Health", "type": "int"}, {"name": "Speed", "type": "float"}],
            "fidelity": "partial",
        },
    }

    monkeypatch.setattr(uasset_mod, "inspect_uasset", lambda path, sections: left if "left" in str(path) else right)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset", sections=["variables"])
    variables = result["changes"]["variables"]
    assert variables["change_count"] == 2
    assert variables["added"][0]["name"] == "Speed"
    assert variables["modified"][0]["name"] == "Health"


def test_diff_uasset_events(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "BP_Test",
        "ue_version": "5.4",
        "asset_class": "Blueprint",
        "parent_class": "Actor",
        "parent_class_path": "/Script/Engine.Actor",
        "blueprint_type": "Normal",
        "events": {
            "events": [{"name": "ReceiveBeginPlay"}],
            "custom_events": [],
            "event_count": 1,
            "custom_event_count": 0,
            "fidelity": "partial",
        },
    }
    right = {
        **left,
        "events": {
            "events": [{"name": "ReceiveBeginPlay"}],
            "custom_events": [{"name": "CustomEvent_0"}],
            "event_count": 1,
            "custom_event_count": 1,
            "fidelity": "partial",
        },
    }

    monkeypatch.setattr(uasset_mod, "inspect_uasset", lambda path, sections: left if "left" in str(path) else right)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset", sections=["events"])
    assert result["changes"]["events"]["added_custom"][0]["name"] == "CustomEvent_0"


def test_diff_uasset_external_actor_summary(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "SM_Tree_12",
        "ue_version": "5.4",
        "asset_class": "StaticMeshActor",
        "parent_class": "N/A",
        "parent_class_path": "",
        "blueprint_type": "N/A",
        "is_external_actor": True,
        "actor_label": "Pine_01",
        "actor_guid": "00112233-4455-6677-8899-aabbccddeeff",
        "actor_class_path": "/Script/Engine.StaticMeshActor",
        "actor_outer_path": "/Game/Maps/OpenWorld.PersistentLevel",
        "actor_folder_path": "Environment/Trees",
        "actor_runtime_grid": "MainGrid",
        "actor_tags": ["Gameplay"],
        "actor_data_layers": [],
        "actor_spatially_loaded": True,
    }
    right = {
        **left,
        "actor_label": "Pine_02",
        "actor_folder_path": "Environment/Trees/Big",
        "actor_tags": ["Gameplay", "Quest"],
    }

    monkeypatch.setattr(uasset_mod, "inspect_uasset", lambda path, sections: left if "left" in str(path) else right)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset")
    summary = result["changes"]["summary"]["modified"]
    assert summary["actor_label"]["new"] == "Pine_02"
    assert summary["actor_folder_path"]["new"] == "Environment/Trees/Big"
    assert summary["actor_tags"]["new"] == ["Gameplay", "Quest"]


def test_diff_uasset_properties(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "SM_Tree_12",
        "ue_version": "5.4",
        "asset_class": "StaticMeshActor",
        "parent_class": "N/A",
        "parent_class_path": "",
        "blueprint_type": "N/A",
        "properties": {
            "count": 2,
            "items": [
                {"name": "CustomDensity", "type": "FloatProperty", "value": 42.5, "fidelity": "parsed"},
                {"name": "bFrozen", "type": "BoolProperty", "value": False, "fidelity": "parsed"},
            ],
            "fidelity": "partial",
        },
    }
    right = {
        **left,
        "properties": {
            "count": 3,
            "items": [
                {"name": "CustomDensity", "type": "FloatProperty", "value": 84.0, "fidelity": "parsed"},
                {"name": "bFrozen", "type": "BoolProperty", "value": False, "fidelity": "parsed"},
                {"name": "ActorScale3D", "type": "StructProperty", "struct": "Vector", "value": {"x": 2.0, "y": 2.0, "z": 2.0}, "fidelity": "parsed"},
            ],
            "fidelity": "partial",
        },
    }

    monkeypatch.setattr(uasset_mod, "inspect_uasset", lambda path, sections: left if "left" in str(path) else right)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset", sections=["properties"])
    payload = result["changes"]["properties"]
    assert payload["change_count"] == 2
    assert payload["added"][0]["name"] == "ActorScale3D"
    assert payload["modified"][0]["name"] == "CustomDensity"
