from __future__ import annotations

import json
import sys
from unittest.mock import patch

from soft_ue_cli import __main__ as main_mod
from soft_ue_cli.__main__ import build_parser
from soft_ue_cli.diagnostics import (
    build_handoff_report,
    make_issue_investigation_plan,
    summarize_build_log,
    summarize_p4_opened,
    validate_data_files,
)


def test_summarize_build_log_classifies_unreal_failures():
    text = """
    Source.cpp(12): error C2664: cannot convert argument
    Module.MyGame.cpp.obj : error LNK2019: unresolved external symbol
    UnrealHeaderTool failed for target MyGameEditor
    """

    result = summarize_build_log(text)

    assert result["schema"] == "soft_ue.diagnose.build_log.v1"
    assert result["finding_count"] >= 3
    assert {finding["kind"] for finding in result["findings"]} >= {
        "compiler_error",
        "linker_error",
        "unreal_header_tool",
    }
    assert result["next_steps"]


def test_summarize_p4_opened_flags_binary_and_generated_changes():
    text = """
    //depot/Game/Content/Hero.uasset#3 - edit change 123 (binary)
    //depot/Game/Source/MyActor.generated.h#1 - edit default change (text)
    //depot/Game/Source/MyActor.cpp#1 - edit default change (text)
    """

    result = summarize_p4_opened(text)

    assert result["schema"] == "soft_ue.diagnose.p4.v1"
    assert result["opened_count"] == 3
    assert result["risky_count"] == 2
    assert any(item["risk"] == "binary_asset" for item in result["risky_files"])
    assert any(item["risk"] == "generated_file" for item in result["risky_files"])


def test_issue_investigation_plan_accepts_jira_and_sentry_payloads():
    jira = make_issue_investigation_plan(
        "jira",
        json.dumps({"key": "UE-42", "fields": {"summary": "Hero falls through floor", "description": "PIE repro"}}),
    )
    sentry = make_issue_investigation_plan(
        "sentry",
        json.dumps({"title": "Crash in ABP_Hero", "culprit": "AnimGraph", "platform": "native"}),
    )

    assert jira["source"] == "jira"
    assert jira["issue_key"] == "UE-42"
    assert any("get-logs" in command for command in jira["suggested_commands"])
    assert sentry["source"] == "sentry"
    assert "Crash in ABP_Hero" in sentry["title"]
    assert sentry["investigation_plan"]


def test_validate_data_files_detects_empty_references_and_duplicate_rows(tmp_path):
    csv_path = tmp_path / "DT_Items.csv"
    csv_path.write_text(
        "Name,AssetPath,GameplayTag\nSword,/Game/Weapons/Sword,Item.Weapon\nSword,,\n",
        encoding="utf-8",
    )

    result = validate_data_files([csv_path])

    assert result["schema"] == "soft_ue.diagnose.data.v1"
    assert result["problem_count"] >= 2
    assert any(problem["kind"] == "duplicate_row_name" for problem in result["problems"])
    assert any(problem["kind"] == "empty_reference" for problem in result["problems"])


def test_build_handoff_report_generates_markdown():
    result = build_handoff_report(
        title="Animation crash",
        summary="Crash after retargeting",
        evidence=["build log classified as linker_error"],
        next_steps=["Run anim montage inspect"],
    )

    assert result["schema"] == "soft_ue.diagnose.handoff.v1"
    assert "# Animation crash" in result["markdown"]
    assert "Run anim montage inspect" in result["markdown"]


def test_cmd_diagnose_asset_runs_existing_inspectors(capsys):
    parser = build_parser()
    args = parser.parse_args([
        "diagnose",
        "asset",
        "/Game/Blueprints/BP_Hero",
        "--kind",
        "blueprint",
        "--include-graph",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    calls = [call.args for call in mock_run.call_args_list]
    assert calls == [
        ("query-asset", {"asset_path": "/Game/Blueprints/BP_Hero", "depth": 2}),
        ("query-blueprint", {"asset_path": "/Game/Blueprints/BP_Hero"}),
        ("query-blueprint-graph", {"asset_path": "/Game/Blueprints/BP_Hero"}),
    ]
    payload = json.loads(capsys.readouterr().out)
    assert payload["schema"] == "soft_ue.diagnose.asset.v1"
    assert payload["checks"][0]["tool"] == "query-asset"


def test_cmd_diagnose_character_returns_retarget_lod_plan(capsys):
    parser = build_parser()
    args = parser.parse_args([
        "diagnose",
        "character",
        "/Game/Characters/BP_MetaHuman",
        "--anim-blueprint",
        "/Game/Characters/ABP_MetaHuman",
        "--target-mesh",
        "/Game/Characters/SKM_Target",
    ])

    args.func(args)

    payload = json.loads(capsys.readouterr().out)
    assert payload["schema"] == "soft_ue.diagnose.character.v1"
    assert any("retarget" in step.lower() for step in payload["diagnostic_steps"])
    assert any("lod" in step.lower() for step in payload["diagnostic_steps"])


def test_cmd_diagnose_probe_runs_repeatable_pie_sequence(capsys):
    parser = build_parser()
    args = parser.parse_args([
        "diagnose",
        "probe",
        "--map",
        "/Game/Maps/Test",
        "--script",
        "print('probe')",
        "--frames",
        "3",
        "--capture",
        "--stop",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    calls = [call.args for call in mock_run.call_args_list]
    assert calls[0] == ("pie-session", {"action": "start", "map": "/Game/Maps/Test"})
    assert calls[1] == ("run-python-script", {"script": "print('probe')", "world": "pie"})
    assert calls[2] == ("pie-tick", {"frames": 3})
    assert calls[3] == ("get-logs", {"lines": 200})
    assert calls[4] == ("capture-screenshot", {"mode": "pie-window", "safe_mode": True})
    assert calls[5] == ("pie-session", {"action": "stop"})
    payload = json.loads(capsys.readouterr().out)
    assert payload["schema"] == "soft_ue.diagnose.probe.v1"
    assert payload["step_count"] == 6
