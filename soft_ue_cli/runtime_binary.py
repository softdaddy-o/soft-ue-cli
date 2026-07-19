"""Runtime and binary-install planning helpers for SoftUEBridge."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


INSTALL_METADATA_SCHEMA = "soft-ue.bridge-install.v1"
INSTALL_METADATA_PATH = Path(".soft-ue-bridge") / "installed-plugin.json"
SUPPORTED_PACKAGED_CONFIGURATIONS = {"Development", "DebugGame"}


def inspect_packaged_readiness(project_path: str | Path, *, configuration: str = "Development") -> dict[str, Any]:
    root = Path(project_path).expanduser().resolve()
    checks: list[dict[str, Any]] = []
    recovery_hints: list[str] = []

    def add_check(check_id: str, status: str, message: str, **extra: Any) -> None:
        checks.append({"id": check_id, "status": status, "message": message, **extra})

    config_supported = configuration in SUPPORTED_PACKAGED_CONFIGURATIONS
    if config_supported:
        add_check("configuration-supported", "pass", f"{configuration} packaged targets are supported.")
    else:
        add_check(
            "configuration-supported",
            "fail",
            f"{configuration} is not supported by default for SoftUEBridge packaged runtime control.",
        )
        recovery_hints.append(
            "Shipping builds exclude SoftUEBridge by default; use Development or DebugGame unless the project explicitly enables developer tools."
        )

    uproject_files = sorted(root.glob("*.uproject")) if root.exists() else []
    if uproject_files:
        uproject = uproject_files[0]
        add_check("project-found", "pass", f"Found {uproject.name}.", path=str(uproject))
    else:
        uproject = None
        add_check("project-found", "fail", f"No .uproject file found in {root}.")
        recovery_hints.append("Run from a UE project root or pass --project <path>.")

    plugin_dir = root / "Plugins" / "SoftUEBridge"
    uplugin = plugin_dir / "SoftUEBridge.uplugin"
    if uplugin.is_file():
        add_check("plugin-files", "pass", "SoftUEBridge plugin files are present.", path=str(plugin_dir))
    else:
        add_check("plugin-files", "fail", "SoftUEBridge plugin files are missing.", path=str(plugin_dir))
        recovery_hints.append("Install SoftUEBridge before packaging the runtime target.")

    enabled = False
    if uproject is not None:
        try:
            data = json.loads(uproject.read_text(encoding="utf-8-sig"))
            enabled = any(
                plugin.get("Name") == "SoftUEBridge" and bool(plugin.get("Enabled"))
                for plugin in data.get("Plugins", [])
                if isinstance(plugin, dict)
            )
        except Exception as exc:
            add_check("plugin-enabled", "fail", f"Could not read {uproject.name}: {exc}")
        else:
            if enabled:
                add_check("plugin-enabled", "pass", "SoftUEBridge is enabled in the .uproject file.")
            else:
                add_check("plugin-enabled", "fail", "SoftUEBridge is not enabled in the .uproject file.")
                recovery_hints.append('Add {"Name": "SoftUEBridge", "Enabled": true} to the .uproject Plugins array.')

    metadata = read_installed_metadata(root)
    if metadata:
        add_check("install-metadata", "pass", "SoftUEBridge install metadata is present.", path=str(root / INSTALL_METADATA_PATH))
    else:
        add_check(
            "install-metadata",
            "warn",
            "No SoftUEBridge install metadata found; source installs remain usable but binary update/rollback cannot prove ownership.",
            path=str(root / INSTALL_METADATA_PATH),
        )

    failed = [check for check in checks if check["status"] == "fail"]
    status = "ready" if not failed else "blocked"
    return {
        "schema": "soft-ue.runtime.packaged-readiness.v1",
        "status": status,
        "supported": status == "ready",
        "project_path": str(root),
        "configuration": configuration,
        "checks": checks,
        "recovery_hints": recovery_hints,
    }


def load_binary_manifest(manifest_path: str | Path) -> dict[str, Any]:
    path = Path(manifest_path).expanduser().resolve()
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    packages = data.get("packages")
    if not isinstance(packages, list):
        raise ValueError("Binary manifest must contain a packages array.")
    return data


def plan_binary_install(
    project_path: str | Path,
    manifest_path: str | Path,
    *,
    ue_version: str,
    platform: str,
    configuration: str,
    allow_source_fallback: bool = True,
) -> dict[str, Any]:
    root = Path(project_path).expanduser().resolve()
    manifest = load_binary_manifest(manifest_path)
    package = _find_package(manifest, ue_version=ue_version, platform=platform, configuration=configuration)
    if package is None:
        action = "source-install" if allow_source_fallback else "rebuild-required"
        return {
            "schema": "soft-ue.binary.install-plan.v1",
            "action": action,
            "reason": "No matching binary package found; use source install or build SoftUEBridge locally.",
            "project_path": str(root),
            "requested": {"ue_version": ue_version, "platform": platform, "configuration": configuration},
        }
    return {
        "schema": "soft-ue.binary.install-plan.v1",
        "action": "binary-install",
        "project_path": str(root),
        "requested": {"ue_version": ue_version, "platform": platform, "configuration": configuration},
        "package": package,
        "owned_paths": ["Plugins/SoftUEBridge/"],
        "copy_plan": _copy_plan(root, package),
    }


def plan_binary_update(
    project_path: str | Path,
    manifest_path: str | Path,
    *,
    ue_version: str,
    platform: str,
    configuration: str,
) -> dict[str, Any]:
    root = Path(project_path).expanduser().resolve()
    current = read_installed_metadata(root)
    if (root / "Plugins" / "SoftUEBridge").exists() and not current:
        return {
            "schema": "soft-ue.binary.update-plan.v1",
            "action": "blocked",
            "reason": "existing_plugin_unowned",
            "project_path": str(root),
            "recovery_hint": "Run source install manually or add verified SoftUEBridge install metadata before binary update.",
        }
    install_plan = plan_binary_install(
        root,
        manifest_path,
        ue_version=ue_version,
        platform=platform,
        configuration=configuration,
        allow_source_fallback=False,
    )
    if install_plan["action"] != "binary-install":
        return {
            "schema": "soft-ue.binary.update-plan.v1",
            "action": "blocked",
            "reason": "no_matching_binary",
            "project_path": str(root),
            "requested": install_plan["requested"],
        }
    return {
        "schema": "soft-ue.binary.update-plan.v1",
        "action": "update",
        "project_path": str(root),
        "current": current,
        "next": install_plan["package"],
        "backup_path": _json_path(root / ".soft-ue-bridge" / "backups" / f"SoftUEBridge-{current.get('bridge_version', 'unknown')}"),
        "copy_plan": install_plan["copy_plan"],
    }


def plan_binary_rollback(project_path: str | Path) -> dict[str, Any]:
    root = Path(project_path).expanduser().resolve()
    metadata = read_installed_metadata(root)
    backup_path = str(metadata.get("backup_path") or "")
    if not metadata or not backup_path:
        return {
            "schema": "soft-ue.binary.rollback-plan.v1",
            "action": "blocked",
            "reason": "no_rollback_metadata",
            "project_path": str(root),
        }
    return {
        "schema": "soft-ue.binary.rollback-plan.v1",
        "action": "rollback",
        "project_path": str(root),
        "backup_path": _json_path(root / backup_path),
        "installed": metadata,
        "restore_paths": metadata.get("owned_paths", ["Plugins/SoftUEBridge/"]),
    }


def read_installed_metadata(project_path: str | Path) -> dict[str, Any]:
    path = Path(project_path).expanduser().resolve() / INSTALL_METADATA_PATH
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if data.get("schema") != INSTALL_METADATA_SCHEMA:
        raise ValueError(f"Unsupported SoftUEBridge install metadata schema: {data.get('schema')}")
    return data


def write_installed_metadata(project_path: str | Path, metadata: dict[str, Any]) -> Path:
    if metadata.get("schema") != INSTALL_METADATA_SCHEMA:
        raise ValueError(f"metadata schema must be {INSTALL_METADATA_SCHEMA}")
    path = Path(project_path).expanduser().resolve() / INSTALL_METADATA_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(metadata, indent=2, sort_keys=True), encoding="utf-8")
    return path


def build_runtime_smoke_plan(
    *,
    executable: str | Path | None = None,
    bridge_url: str | None = None,
    timeout: float = 120.0,
    log_path: str | Path | None = None,
) -> dict[str, Any]:
    if executable is None and not bridge_url:
        raise ValueError("Provide an executable or bridge_url for runtime smoke planning.")
    mode = "launch" if executable is not None else "attach"
    steps = []
    if executable is not None:
        steps.append(
            {
                "id": "launch-runtime",
                "command": [str(Path(executable).expanduser()), "-softuebridge"],
                "expected": "packaged runtime process starts",
            }
        )
    else:
        steps.append({"id": "attach-runtime", "bridge_url": bridge_url, "expected": "existing runtime process is reachable"})
    steps.extend(
        [
            {"id": "wait-for-ready", "timeout": timeout, "expected": "SoftUEBridge health endpoint responds"},
            {"id": "status", "expected": "bridge status returns structured JSON"},
            {"id": "runtime-inspection", "expected": "non-mutating runtime inspection succeeds"},
            {"id": "collect-diagnostics", "log_path": str(log_path) if log_path else None, "expected": "logs and recovery hints are available"},
        ]
    )
    return {
        "schema": "soft-ue.runtime.smoke-plan.v1",
        "mode": mode,
        "ci_friendly": True,
        "bridge_url": bridge_url,
        "executable": str(Path(executable).expanduser()) if executable is not None else None,
        "timeout": timeout,
        "steps": steps,
    }


def _find_package(manifest: dict[str, Any], *, ue_version: str, platform: str, configuration: str) -> dict[str, Any] | None:
    for package in manifest.get("packages", []):
        if not isinstance(package, dict):
            continue
        if (
            str(package.get("ue_version")) == ue_version
            and str(package.get("platform")) == platform
            and str(package.get("configuration")) == configuration
        ):
            return package
    return None


def _copy_plan(root: Path, package: dict[str, Any]) -> list[dict[str, str]]:
    plan = []
    for item in package.get("files", []):
        if not isinstance(item, dict):
            continue
        source = str(item.get("source", ""))
        destination = str(item.get("destination", ""))
        if not source or not destination:
            continue
        plan.append(
            {
                "source": source,
                "destination": _json_path(root / "Plugins" / "SoftUEBridge" / destination),
                "owner": "SoftUEBridge",
            }
        )
    return plan


def _json_path(path: Path) -> str:
    return path.as_posix()
