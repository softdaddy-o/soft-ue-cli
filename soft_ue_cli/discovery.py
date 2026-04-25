"""Discover the SoftUEBridge HTTP server port."""

from __future__ import annotations

import json
import os
from pathlib import Path


def _load_instance_file(path: Path) -> str | None:
    """Read a .soft-ue-bridge/instance.json and return the URL, or None on failure."""
    try:
        data = json.loads(path.read_text())
        host = data.get("host", "127.0.0.1")
        port = data.get("port", 8080)
        return f"http://{host}:{port}"
    except Exception:
        return None


def _find_project_instance() -> str | None:
    """Walk up from cwd looking for .soft-ue-bridge/instance.json (project-local)."""
    current = Path.cwd()
    for directory in [current, *current.parents]:
        candidate = directory / ".soft-ue-bridge" / "instance.json"
        if candidate.exists():
            return _load_instance_file(candidate)
    return None


def get_server_url() -> str:
    """Return the base URL of the running SoftUEBridge server.

    Resolution order:
    1. SOFT_UE_BRIDGE_URL env var (full URL)
    2. SOFT_UE_BRIDGE_PORT env var (port only)
    3. .soft-ue-bridge/instance.json in cwd or any parent (project-local, written by plugin)
    4. Default: http://127.0.0.1:8080
    """
    if url := os.environ.get("SOFT_UE_BRIDGE_URL"):
        return url.rstrip("/")

    if port_str := os.environ.get("SOFT_UE_BRIDGE_PORT"):
        try:
            return f"http://127.0.0.1:{int(port_str)}"
        except ValueError:
            pass

    if url := _find_project_instance():
        return url

    return "http://127.0.0.1:8080"
