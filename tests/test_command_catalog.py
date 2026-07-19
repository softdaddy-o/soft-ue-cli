"""Tests for public command taxonomy metadata."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))

from soft_ue_cli.command_catalog import (  # noqa: E402
    command_metadata_as_json,
    filter_command_metadata,
    get_command_metadata,
    iter_command_metadata,
    iter_removed_command_metadata,
)


def test_catalog_marks_umg_layout_as_canonical_offline_command():
    meta = get_command_metadata("umg layout")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "offline"
    assert meta["category"] == "compare"
    assert meta["requires_bridge"] is False
    assert meta["requires_editor"] is False
    assert meta["requires_pie"] is False


def test_catalog_hides_removed_umg_layout_wrappers_by_default():
    payload = command_metadata_as_json()
    names = {entry["name"] for entry in payload["commands"]}

    assert "umg layout compare" in names
    assert "compare-umg-layout" not in names


def test_catalog_marks_capture_family_as_canonical_and_tracks_removed_migrations():
    capture = get_command_metadata("capture")
    viewport = get_command_metadata("capture viewport")
    screenshot = get_command_metadata("capture screenshot")
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}

    assert capture["status"] == "canonical"
    assert viewport["status"] == "canonical"
    assert screenshot["status"] == "canonical"
    assert viewport["requires_bridge"] is True
    assert screenshot["requires_bridge"] is True
    assert removed["capture-viewport"]["status"] == "removed"
    assert removed["capture-viewport"]["canonical_command"] == "capture viewport"
    assert removed["capture-screenshot"]["canonical_command"] == "capture screenshot --source <mode>"
    assert removed["capture-pie-screenshot"]["canonical_command"] == "capture screenshot --source pie-window"


def test_catalog_exposes_umg_layout_iteration_workflow():
    workflow = get_command_metadata("umg workflow iterate-layout")

    assert workflow["status"] == "canonical"
    assert workflow["layer"] == "workflow"
    assert workflow["category"] == "workflow"
    assert workflow["requires_bridge"] is True
    assert workflow["requires_editor"] is True
    assert workflow["requires_pie"] is True


def test_catalog_migrates_runtime_widget_inspection_to_umg_family():
    canonical = get_command_metadata("umg runtime inspect")
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}

    assert canonical["status"] == "canonical"
    assert canonical["category"] == "umg"
    assert canonical["requires_bridge"] is True
    assert canonical["requires_editor"] is True
    assert canonical["requires_pie"] is True
    assert removed["inspect-runtime-widgets"]["status"] == "removed"
    assert removed["inspect-runtime-widgets"]["canonical_command"] == "umg runtime inspect"


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
def test_catalog_marks_remaining_command_families_as_canonical_and_legacy_as_removed(family, legacy, canonical):
    family_meta = get_command_metadata(family)
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}

    assert family_meta["status"] == "canonical"
    assert legacy in removed
    assert removed[legacy]["status"] == "removed"
    assert removed[legacy]["canonical_command"] == canonical


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
def test_removed_commands_use_canonical_family_category(legacy, category):
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}
    meta = removed[legacy]

    assert meta["category"] == category


def test_blueprint_removed_aliases_keep_migration_metadata():
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}
    meta = removed["modify-interface"]

    assert meta["status"] == "removed"
    assert meta["canonical_command"] == "blueprint interface modify"
    assert meta["requires_editor"] is True


def test_asset_file_removed_aliases_stay_offline():
    removed = {entry["name"]: entry for entry in iter_removed_command_metadata()}
    meta = removed["inspect-uasset"]

    assert meta["status"] == "removed"
    assert meta["canonical_command"] == "asset inspect-file"
    assert meta["requires_bridge"] is False
    assert meta["requires_editor"] is False


def test_catalog_includes_plugin_requirements_for_optional_plugin_tools():
    mutable = get_command_metadata("mutable compile")
    statetree = get_command_metadata("statetree inspect")
    rewind = get_command_metadata("anim rewind start")
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


def test_catalog_marks_anim_retarget_repoint_as_bridge_editor_command():
    meta = get_command_metadata("anim retarget repoint-references")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "animation"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False


def test_catalog_marks_anim_retarget_blueprint_as_bridge_editor_command():
    meta = get_command_metadata("anim retarget blueprint")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "animation"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False


def test_catalog_marks_anim_montage_set_slot_animation_as_bridge_editor_command():
    family = get_command_metadata("anim montage")
    meta = get_command_metadata("anim montage set-slot-animation")

    assert family["status"] == "canonical"
    assert family["category"] == "animation"
    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "animation"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False
    assert "set-slot-animation" in meta["examples"][0]


def test_catalog_marks_anim_montage_inspect_as_bridge_editor_command():
    meta = get_command_metadata("anim montage inspect")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "animation"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False
    assert "inspect" in meta["examples"][0]


def test_catalog_marks_anim_retarget_sequence_as_ikrig_editor_command():
    meta = get_command_metadata("anim retarget sequence")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "animation"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False
    assert meta["required_plugins"][0]["name"] == "IK Rig"
    assert "sequence" in meta["examples"][0]


def test_catalog_marks_pose_search_schema_commands_as_pose_search_plugin_tools():
    inspect = get_command_metadata("anim pose-search inspect")
    remap = get_command_metadata("anim pose-search remap")
    database_repoint = get_command_metadata("anim pose-search database-repoint")

    assert inspect["status"] == "canonical"
    assert inspect["layer"] == "bridge"
    assert inspect["category"] == "animation"
    assert inspect["requires_bridge"] is True
    assert inspect["requires_editor"] is True
    assert inspect["required_plugins"][0]["name"] == "PoseSearch"

    assert remap["status"] == "canonical"
    assert remap["layer"] == "bridge"
    assert remap["category"] == "animation"
    assert remap["requires_bridge"] is True
    assert remap["requires_editor"] is True
    assert remap["required_plugins"][0]["module"] == "PoseSearch"

    assert database_repoint["status"] == "canonical"
    assert database_repoint["layer"] == "bridge"
    assert database_repoint["category"] == "animation"
    assert database_repoint["requires_bridge"] is True
    assert database_repoint["requires_editor"] is True
    assert database_repoint["required_plugins"][0]["name"] == "PoseSearch"


def test_catalog_marks_asset_repoint_references_as_bridge_editor_command():
    meta = get_command_metadata("asset repoint-references")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "asset"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False


def test_catalog_marks_metasound_inspect_as_bridge_editor_command():
    meta = get_command_metadata("metasound inspect")

    assert meta["status"] == "canonical"
    assert meta["layer"] == "bridge"
    assert meta["category"] == "inspect"
    assert meta["requires_bridge"] is True
    assert meta["requires_editor"] is True
    assert meta["requires_pie"] is False


def test_catalog_can_filter_commands_by_required_plugin():
    mutable_entries = filter_command_metadata(plugin="Mutable")
    mutable_names = {entry["name"] for entry in mutable_entries}

    assert "mutable compile" in mutable_names
    assert "mutable inspect parameters" in mutable_names
    assert "compile-co" not in mutable_names
    assert all(
        any(plugin["name"] == "Mutable" for plugin in entry["required_plugins"])
        for entry in mutable_entries
    )


def test_commands_probe_metadata_keeps_plugin_requirement_context():
    payload = command_metadata_as_json(probe=True)
    mutable_entry = next(entry for entry in payload["commands"] if entry["name"] == "mutable compile")

    assert mutable_entry["available"] == "unknown"
    assert mutable_entry["availability_note"]
    assert mutable_entry["required_plugins"][0]["name"] == "Mutable"


def test_catalog_marks_diagnose_family_as_offline_and_workflow_commands():
    diagnose = get_command_metadata("diagnose")
    build_log = get_command_metadata("diagnose build-log")
    probe = get_command_metadata("diagnose probe")

    assert diagnose["status"] == "canonical"
    assert diagnose["category"] == "support"
    assert build_log["layer"] == "offline"
    assert build_log["requires_bridge"] is False
    assert probe["layer"] == "workflow"
    assert probe["requires_bridge"] is True
    assert probe["requires_pie"] is True


def test_catalog_marks_runtime_binary_family_as_offline_workflow_commands():
    runtime = get_command_metadata("runtime")
    readiness = get_command_metadata("runtime readiness")
    install = get_command_metadata("runtime binary plan-install")
    smoke = get_command_metadata("runtime smoke-plan")

    assert runtime["category"] == "runtime"
    assert readiness["layer"] == "offline"
    assert readiness["requires_bridge"] is False
    assert install["layer"] == "offline"
    assert smoke["layer"] == "workflow"


def test_catalog_marks_expert_context_as_opt_in_workflow_command():
    expert = get_command_metadata("expert")
    context = get_command_metadata("expert context")

    assert expert["layer"] == "workflow"
    assert expert["category"] == "support"
    assert expert["requires_bridge"] is False
    assert expert["requires_editor"] is False
    assert context["layer"] == "workflow"
    assert context["category"] == "support"
    assert context["requires_bridge"] is False
    assert context["requires_editor"] is False
    assert context["requires_pie"] is False
    assert context["examples"] == [
        'soft-ue-cli expert context --task "Build fails" --ue-version 5.8'
    ]


def test_catalog_marks_cloth_family_as_bridge_editor_commands():
    cloth = get_command_metadata("cloth")
    query = get_command_metadata("cloth query")
    apply_weightmap = get_command_metadata("cloth apply-weightmap")

    assert cloth["status"] == "canonical"
    assert cloth["category"] == "cloth"
    assert query["layer"] == "bridge"
    assert query["requires_bridge"] is True
    assert query["requires_editor"] is True
    assert apply_weightmap["layer"] == "bridge"
    assert apply_weightmap["category"] == "cloth"


def test_commands_json_contains_sorted_metadata_entries():
    payload = command_metadata_as_json()

    assert payload["schema"] == "soft-ue.commands.v1"
    names = [entry["name"] for entry in payload["commands"]]
    assert names == sorted(names)
    assert "umg layout" in names
    assert "compare-umg-layout" not in names


def test_commands_json_can_include_removed_migration_entries():
    payload = command_metadata_as_json(include_removed=True)

    names = [entry["name"] for entry in payload["commands"]]
    removed_entry = next(entry for entry in payload["commands"] if entry["name"] == "query-blueprint")

    assert names == sorted(names)
    assert removed_entry["status"] == "removed"
    assert removed_entry["canonical_command"] == "blueprint inspect"
    assert "Removed flat command" in removed_entry["summary"]


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
