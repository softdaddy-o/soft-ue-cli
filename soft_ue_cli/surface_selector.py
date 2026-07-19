"""Surface selection for UE 5.8 official MCP and SoftUEBridge."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any
from urllib.parse import urlparse

import httpx

DEFAULT_OFFICIAL_MCP_ENDPOINT = "http://127.0.0.1:8000/mcp"


@dataclass(frozen=True)
class SurfaceProbe:
    name: str
    available: bool
    endpoint: str
    status: str
    detail: str | None = None


def _is_local_endpoint(endpoint: str) -> bool:
    parsed = urlparse(endpoint)
    host = (parsed.hostname or "").lower()
    return host in {"127.0.0.1", "localhost", "::1"}


def probe_official_mcp(endpoint: str = DEFAULT_OFFICIAL_MCP_ENDPOINT, *, timeout: float = 1.0) -> SurfaceProbe:
    """Probe Epic's local UE 5.8 MCP endpoint without executing editor mutations."""
    if not _is_local_endpoint(endpoint):
        return SurfaceProbe(
            name="official_mcp",
            available=False,
            endpoint=endpoint,
            status="remote_endpoint_rejected",
            detail="official Unreal MCP probing is limited to localhost endpoints",
        )

    payload: dict[str, Any] = {
        "jsonrpc": "2.0",
        "id": "soft-ue-cli-surface-probe",
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "soft-ue-cli", "version": "surface-probe"},
        },
    }
    headers = {
        "Accept": "application/json, text/event-stream",
        "Content-Type": "application/json",
        "MCP-Protocol-Version": "2025-06-18",
    }

    try:
        response = httpx.post(endpoint, json=payload, headers=headers, timeout=timeout)
    except (httpx.ConnectError, httpx.TimeoutException) as exc:
        return SurfaceProbe("official_mcp", False, endpoint, "unreachable", str(exc))
    except Exception as exc:
        return SurfaceProbe("official_mcp", False, endpoint, "error", str(exc))

    if response.status_code in {200, 202}:
        return SurfaceProbe("official_mcp", True, endpoint, "available", "initialize accepted")

    text = response.text.strip()
    try:
        body = response.json()
    except Exception:
        body = None
    if isinstance(body, dict) and ("jsonrpc" in body or "error" in body):
        detail = str(body.get("error") or body)
        return SurfaceProbe("official_mcp", True, endpoint, "protocol_error", detail)

    detail = text[:200] if text else f"HTTP {response.status_code}"
    return SurfaceProbe("official_mcp", False, endpoint, f"http_{response.status_code}", detail)


def probe_soft_ue_bridge(*, timeout: float = 1.0) -> SurfaceProbe:
    """Probe SoftUEBridge through the existing CLI discovery and health path."""
    from .client import health_check
    from .discovery import get_server_url

    endpoint = get_server_url()
    health = health_check(timeout=timeout)
    if "error" not in health and bool(health):
        return SurfaceProbe("soft_ue_bridge", True, endpoint, "available", None)
    return SurfaceProbe("soft_ue_bridge", False, endpoint, "unreachable", str(health.get("error") or health))


def build_surface_report(*, official_mcp: SurfaceProbe, soft_ue_bridge: SurfaceProbe) -> dict[str, Any]:
    official_available = official_mcp.available
    bridge_available = soft_ue_bridge.available

    if official_available and bridge_available:
        availability = "both"
        recommendation = {
            "primary": "official-mcp",
            "fallback": "soft-ue-bridge",
            "reason": (
                "Use official MCP first for UE 5.8 editor-native workflows when its toolset covers the task; "
                "use SoftUEBridge for CLI automation, runtime, binary, and compatibility workflows."
            ),
        }
    elif official_available:
        availability = "official-only"
        recommendation = {
            "primary": "official-mcp",
            "fallback": None,
            "reason": "Use official MCP for UE 5.8 editor-native workflows when its toolset covers the task.",
        }
    elif bridge_available:
        availability = "bridge-only"
        recommendation = {
            "primary": "soft-ue-bridge",
            "fallback": None,
            "reason": (
                "Use SoftUEBridge for CLI automation, runtime control, binary workflows, "
                "and compatibility when official MCP is not reachable."
            ),
        }
    else:
        availability = "neither"
        recommendation = {
            "primary": None,
            "fallback": None,
            "reason": "Start Unreal Editor with either the UE 5.8 Unreal MCP plugin or SoftUEBridge enabled.",
        }

    return {
        "schema": "soft-ue.mcp-surface.v1",
        "availability": availability,
        "surfaces": {
            "official_mcp": asdict(official_mcp),
            "soft_ue_bridge": asdict(soft_ue_bridge),
        },
        "recommendation": {
            **recommendation,
            "official_mcp_use": [
                "UE 5.8 editor-native actor, scene, material, object, Slate, and automation toolsets when available",
            ],
            "soft_ue_bridge_use": [
                "UE 5.7 compatibility",
                "CLI and CI automation",
                "PIE/runtime and packaged Development or DebugGame workflows",
                "build, relaunch, diagnostics, and existing SoftUEBridge-specific tools",
            ],
        },
    }
