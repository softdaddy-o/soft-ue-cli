"""CLI tests for runtime/binary planning commands."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))

from soft_ue_cli import __main__ as main_mod
from soft_ue_cli.__main__ import (
    build_parser,
    cmd_binary_install_plan,
    cmd_binary_rollback_plan,
    cmd_binary_update_plan,
    cmd_runtime_readiness,
    cmd_runtime_smoke_plan,
)


def test_runtime_readiness_cli_outputs_json(capsys, monkeypatch, tmp_path: Path) -> None:
    args = build_parser().parse_args(["runtime", "readiness", "--project", str(tmp_path), "--configuration", "Development"])
    monkeypatch.setattr(
        main_mod,
        "inspect_packaged_readiness",
        lambda project_path, configuration="Development": {
            "schema": "soft-ue.runtime.packaged-readiness.v1",
            "status": "ready",
            "project_path": str(project_path),
            "configuration": configuration,
        },
    )

    cmd_runtime_readiness(args)

    payload = json.loads(capsys.readouterr().out)
    assert payload["schema"] == "soft-ue.runtime.packaged-readiness.v1"
    assert payload["configuration"] == "Development"


def test_binary_plan_commands_parse_and_dispatch(capsys, monkeypatch, tmp_path: Path) -> None:
    manifest = tmp_path / "manifest.json"
    manifest.write_text('{"packages":[]}', encoding="utf-8")

    monkeypatch.setattr(main_mod, "plan_binary_install", lambda *args, **kwargs: {"schema": "soft-ue.binary.install-plan.v1", "action": "source-install"})
    monkeypatch.setattr(main_mod, "plan_binary_update", lambda *args, **kwargs: {"schema": "soft-ue.binary.update-plan.v1", "action": "blocked"})
    monkeypatch.setattr(main_mod, "plan_binary_rollback", lambda *args, **kwargs: {"schema": "soft-ue.binary.rollback-plan.v1", "action": "rollback"})

    install_args = build_parser().parse_args([
        "runtime",
        "binary",
        "plan-install",
        "--project",
        str(tmp_path),
        "--manifest",
        str(manifest),
        "--ue-version",
        "5.8",
        "--platform",
        "Win64",
        "--configuration",
        "Development",
    ])
    cmd_binary_install_plan(install_args)
    assert json.loads(capsys.readouterr().out)["schema"] == "soft-ue.binary.install-plan.v1"

    update_args = build_parser().parse_args([
        "runtime",
        "binary",
        "plan-update",
        "--project",
        str(tmp_path),
        "--manifest",
        str(manifest),
        "--ue-version",
        "5.8",
        "--platform",
        "Win64",
        "--configuration",
        "Development",
    ])
    cmd_binary_update_plan(update_args)
    assert json.loads(capsys.readouterr().out)["schema"] == "soft-ue.binary.update-plan.v1"

    rollback_args = build_parser().parse_args(["runtime", "binary", "plan-rollback", "--project", str(tmp_path)])
    cmd_binary_rollback_plan(rollback_args)
    assert json.loads(capsys.readouterr().out)["schema"] == "soft-ue.binary.rollback-plan.v1"


def test_runtime_smoke_plan_cli_outputs_plan(capsys, monkeypatch, tmp_path: Path) -> None:
    exe = tmp_path / "Game.exe"
    monkeypatch.setattr(
        main_mod,
        "build_runtime_smoke_plan",
        lambda **kwargs: {"schema": "soft-ue.runtime.smoke-plan.v1", "mode": "launch", "executable": str(kwargs["executable"])},
    )

    args = build_parser().parse_args(["runtime", "smoke-plan", "--executable", str(exe), "--timeout", "20"])
    cmd_runtime_smoke_plan(args)

    payload = json.loads(capsys.readouterr().out)
    assert payload["schema"] == "soft-ue.runtime.smoke-plan.v1"
    assert payload["mode"] == "launch"
