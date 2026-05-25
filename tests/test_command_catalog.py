"""Tests for public command taxonomy metadata."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest


from soft_ue_cli.command_catalog import (  # noqa: E402
    command_metadata_as_json,
    filter_command_metadata,
    get_command_metadata,
    iter_command_metadata,
)


def test_catalog_marks_umg_layout_as_canonical_offline_command():
    meta = get_command_metadata("umg layout")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "offline"
    assert meta["category"] == "compare"
    assert meta["requires_bridge"] is False
    assert meta["requires_editor"] is False
    assert meta["requires_pie"] is False


def test_catalog_marks_legacy_umg_layout_wrappers_as_compatibility():
    meta = get_command_metadata("compare-umg-layout")

    assert meta["status"] == "compatibility"
    assert meta["canonical_command"] == "umg layout compare --mode geometry"
    assert meta["requires_bridge"] is False


def test_catalog_marks_capture_family_as_canonical_and_old_commands_as_compatibility():
    capture = get_command_metadata("capture")
    viewport = get_command_metadata("capture viewport")
    screenshot = get_command_metadata("capture screenshot")
    legacy_viewport = get_command_metadata("capture-viewport")
    legacy_screenshot = get_command_metadata("capture-screenshot")
    legacy_pie = get_command_metadata("capture-pie-screenshot")

    assert capture["status"] == "canonical"
    assert viewport["status"] == "canonical"
    assert screenshot["status"] == "canonical"
    assert viewport["requires_bridge"] is True
    assert screenshot["requires_bridge"] is True
    assert legacy_viewport["status"] == "compatibility"
    assert legacy_viewport["canonical_command"] == "capture viewport"
    assert legacy_screenshot["status"] == "compatibility"
    assert legacy_screenshot["canonical_command"] == "capture screenshot --source <mode>"
    assert legacy_pie["canonical_command"] == "capture screenshot --source pie-window"


@pytest.mark.parametrize(
    ("family", "legacy", "canonical"),
    [
        ("mutable", "compile-co", "mutable compile"),
        ("statetree", "query-statetree", "statetree inspect"),
        ("anim", "rewind-start", "anim rewind start"),
        ("asset", "query-asset", "asset query"),
        ("blueprint", "query-blueprint-graph", "blueprint graph inspect"),
    ],
)
def test_catalog_marks_remaining_command_families_as_canonical_and_legacy_as_compatibility(family, legacy, canonical):
    family_meta = get_command_metadata(family)
    legacy_meta = get_command_metadata(legacy)

    assert family_meta["status"] == "canonical"
    assert legacy_meta["status"] == "compatibility"
    assert legacy_meta["canonical_command"] == canonical


@pytest.mark.parametrize(
    ("legacy", "category"),
    [
        ("compile-co", "mutable"),
        ("query-statetree", "statetree"),
        ("rewind-status", "animation"),
        ("query-asset", "asset"),
        ("query-blueprint-graph", "blueprint"),
    ],
)
def test_compatibility_commands_use_canonical_family_category(legacy, category):
    meta = get_command_metadata(legacy)

    assert meta["category"] == category


def test_blueprint_compatibility_aliases_keep_editor_requirement():
    meta = get_command_metadata("modify-interface")

    assert meta["canonical_command"] == "blueprint interface modify"
    assert meta["requires_editor"] is True


def test_asset_file_compatibility_aliases_stay_offline():
    meta = get_command_metadata("inspect-uasset")

    assert meta["canonical_command"] == "asset inspect-file"
    assert meta["requires_bridge"] is False
    assert meta["requires_editor"] is False


def test_catalog_includes_plugin_requirements_for_optional_plugin_tools():
    mutable = get_command_metadata("compile-co")
    statetree = get_command_metadata("query-statetree")
    rewind = get_command_metadata("rewind-start")
    enhanced_input = get_command_metadata("trigger-input")

    assert mutable["required_plugins"][0]["name"] == "Mutable"
    assert mutable["requires_editor"] is True
    assert statetree["required_plugins"][0]["name"] == "StateTree"
    assert statetree["requires_editor"] is True
    assert rewind["required_plugins"][0]["name"] == "Animation Insights"
    assert rewind["requires_editor"] is True
    assert enhanced_input["required_plugins"][0]["name"] == "Enhanced Input"
    assert enhanced_input["requires_editor"] is False
    assert enhanced_input["requires_pie"] is True


def test_catalog_can_filter_commands_by_required_plugin():
    mutable_entries = filter_command_metadata(plugin="Mutable")
    mutable_names = {entry["name"] for entry in mutable_entries}

    assert "compile-co" in mutable_names
    assert "inspect-mutable-parameters" in mutable_names
    assert all(
        any(plugin["name"] == "Mutable" for plugin in entry["required_plugins"])
        for entry in mutable_entries
    )


def test_commands_probe_metadata_keeps_plugin_requirement_context():
    payload = command_metadata_as_json(probe=True)
    mutable_entry = next(entry for entry in payload["commands"] if entry["name"] == "compile-co")

    assert mutable_entry["available"] == "unknown"
    assert mutable_entry["availability_note"]
    assert mutable_entry["required_plugins"][0]["name"] == "Mutable"


def test_commands_json_contains_sorted_metadata_entries():
    payload = command_metadata_as_json()

    assert payload["schema"] == "soft-ue.commands.v1"
    names = [entry["name"] for entry in payload["commands"]]
    assert names == sorted(names)
    assert "umg layout" in names
    assert "compare-umg-layout" in names


def test_all_catalog_entries_have_required_metadata_fields():
    required = {
        "name",
        "summary",
        "layer",
        "category",
        "requires_bridge",
        "requires_editor",
        "requires_pie",
        "required_plugins",
        "status",
        "canonical_command",
        "examples",
    }

    for entry in iter_command_metadata():
        assert required <= set(entry), entry["name"]
