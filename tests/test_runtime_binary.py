"""Tests for runtime/binary support planning."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))

from soft_ue_cli.runtime_binary import (
    build_runtime_smoke_plan,
    inspect_packaged_readiness,
    plan_binary_install,
    plan_binary_rollback,
    plan_binary_update,
    read_installed_metadata,
    write_installed_metadata,
)


def _write_project(root: Path, *, enabled: bool = True) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    uproject = root / "Sample.uproject"
    uproject.write_text(
        json.dumps(
            {
                "EngineAssociation": "5.8",
                "Plugins": [{"Name": "SoftUEBridge", "Enabled": enabled}],
            }
        ),
        encoding="utf-8",
    )
    plugin_dir = root / "Plugins" / "SoftUEBridge"
    plugin_dir.mkdir(parents=True)
    (plugin_dir / "SoftUEBridge.uplugin").write_text(
        json.dumps({"FileVersion": 3, "VersionName": "1.42.0"}),
        encoding="utf-8",
    )
    return uproject


def _write_manifest(path: Path) -> Path:
    path.write_text(
        json.dumps(
            {
                "schema": "soft-ue.bridge-binary-manifest.v1",
                "packages": [
                    {
                        "ue_version": "5.8",
                        "platform": "Win64",
                        "configuration": "Development",
                        "bridge_version": "1.42.0",
                        "digest": "a" * 64,
                        "modules": ["SoftUEBridge", "SoftUEBridgeEditor"],
                        "files": [
                            {
                                "source": "Win64/Development/SoftUEBridge.uplugin",
                                "destination": "SoftUEBridge.uplugin",
                            }
                        ],
                        "notes": "Windows UE 5.8 Development test payload.",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    return path


def test_packaged_readiness_reports_supported_development_project(tmp_path: Path) -> None:
    _write_project(tmp_path)

    report = inspect_packaged_readiness(tmp_path, configuration="Development")

    assert report["schema"] == "soft-ue.runtime.packaged-readiness.v1"
    assert report["status"] == "ready"
    assert report["supported"] is True
    assert report["configuration"] == "Development"
    assert {check["id"] for check in report["checks"]} >= {
        "configuration-supported",
        "project-found",
        "plugin-files",
        "plugin-enabled",
    }


def test_packaged_readiness_accepts_bom_encoded_uproject(tmp_path: Path) -> None:
    _write_project(tmp_path)
    (tmp_path / "Sample.uproject").write_bytes(
        b"\xef\xbb\xbf" + json.dumps({"Plugins": [{"Name": "SoftUEBridge", "Enabled": True}]}).encode("utf-8")
    )

    report = inspect_packaged_readiness(tmp_path, configuration="Development")

    assert any(check["id"] == "plugin-enabled" and check["status"] == "pass" for check in report["checks"])


def test_packaged_readiness_blocks_shipping_by_default(tmp_path: Path) -> None:
    _write_project(tmp_path)

    report = inspect_packaged_readiness(tmp_path, configuration="Shipping")

    assert report["status"] == "blocked"
    assert report["supported"] is False
    assert any(check["id"] == "configuration-supported" and check["status"] == "fail" for check in report["checks"])
    assert "Shipping" in report["recovery_hints"][0]


def test_binary_install_plan_selects_matching_manifest_package(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    manifest = _write_manifest(tmp_path / "manifest.json")

    plan = plan_binary_install(
        tmp_path / "project",
        manifest,
        ue_version="5.8",
        platform="Win64",
        configuration="Development",
    )

    assert plan["schema"] == "soft-ue.binary.install-plan.v1"
    assert plan["action"] == "binary-install"
    assert plan["package"]["bridge_version"] == "1.42.0"
    assert plan["owned_paths"] == ["Plugins/SoftUEBridge/"]
    assert plan["copy_plan"][0]["destination"].endswith("Plugins/SoftUEBridge/SoftUEBridge.uplugin")


def test_binary_install_plan_accepts_bom_encoded_manifest(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    manifest = _write_manifest(tmp_path / "manifest.json")
    manifest.write_bytes(b"\xef\xbb\xbf" + manifest.read_bytes())

    plan = plan_binary_install(
        tmp_path / "project",
        manifest,
        ue_version="5.8",
        platform="Win64",
        configuration="Development",
    )

    assert plan["action"] == "binary-install"


def test_binary_install_plan_falls_back_to_source_when_no_binary_matches(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    manifest = _write_manifest(tmp_path / "manifest.json")

    plan = plan_binary_install(
        tmp_path / "project",
        manifest,
        ue_version="5.7",
        platform="Win64",
        configuration="Development",
    )

    assert plan["action"] == "source-install"
    assert "No matching binary package" in plan["reason"]


def test_binary_update_blocks_existing_unowned_plugin(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    manifest = _write_manifest(tmp_path / "manifest.json")

    plan = plan_binary_update(
        tmp_path / "project",
        manifest,
        ue_version="5.8",
        platform="Win64",
        configuration="Development",
    )

    assert plan["action"] == "blocked"
    assert plan["reason"] == "existing_plugin_unowned"


def test_binary_update_plans_backup_when_existing_plugin_is_owned(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    manifest = _write_manifest(tmp_path / "manifest.json")
    write_installed_metadata(
        tmp_path / "project",
        {
            "schema": "soft-ue.bridge-install.v1",
            "bridge_version": "1.41.0",
            "digest": "b" * 64,
            "owned_paths": ["Plugins/SoftUEBridge/"],
        },
    )

    plan = plan_binary_update(
        tmp_path / "project",
        manifest,
        ue_version="5.8",
        platform="Win64",
        configuration="Development",
    )

    assert plan["schema"] == "soft-ue.binary.update-plan.v1"
    assert plan["action"] == "update"
    assert plan["current"]["bridge_version"] == "1.41.0"
    assert plan["next"]["bridge_version"] == "1.42.0"
    assert plan["backup_path"].endswith(".soft-ue-bridge/backups/SoftUEBridge-1.41.0")


def test_binary_metadata_and_rollback_plan(tmp_path: Path) -> None:
    _write_project(tmp_path / "project")
    metadata = {
        "schema": "soft-ue.bridge-install.v1",
        "bridge_version": "1.41.0",
        "digest": "b" * 64,
        "owned_paths": ["Plugins/SoftUEBridge/"],
        "backup_path": ".soft-ue-bridge/backups/SoftUEBridge-1.41.0",
    }
    write_installed_metadata(tmp_path / "project", metadata)

    assert read_installed_metadata(tmp_path / "project")["bridge_version"] == "1.41.0"
    rollback = plan_binary_rollback(tmp_path / "project")

    assert rollback["schema"] == "soft-ue.binary.rollback-plan.v1"
    assert rollback["action"] == "rollback"
    assert rollback["backup_path"].endswith(".soft-ue-bridge/backups/SoftUEBridge-1.41.0")


def test_runtime_smoke_plan_is_cli_and_ci_friendly(tmp_path: Path) -> None:
    exe = tmp_path / "Windows" / "Sample.exe"
    exe.parent.mkdir()
    exe.write_text("", encoding="utf-8")

    plan = build_runtime_smoke_plan(executable=exe, bridge_url="http://127.0.0.1:8080", timeout=30.0)

    assert plan["schema"] == "soft-ue.runtime.smoke-plan.v1"
    assert plan["mode"] == "launch"
    assert [step["id"] for step in plan["steps"]] == [
        "launch-runtime",
        "wait-for-ready",
        "status",
        "runtime-inspection",
        "collect-diagnostics",
    ]
    assert plan["ci_friendly"] is True


def test_runtime_smoke_plan_requires_executable_or_attach_url() -> None:
    with pytest.raises(ValueError, match="executable or bridge_url"):
        build_runtime_smoke_plan()
