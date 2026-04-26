"""Tests for the diff_uasset public API."""

from __future__ import annotations

from pathlib import Path


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
