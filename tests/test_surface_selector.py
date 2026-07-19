"""Tests for UE 5.8 official MCP / SoftUEBridge surface selection."""

from __future__ import annotations

import sys
from pathlib import Path

import httpx

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))

from soft_ue_cli import surface_selector
from soft_ue_cli.surface_selector import SurfaceProbe, build_surface_report


def _probe(name: str, available: bool, endpoint: str) -> SurfaceProbe:
    return SurfaceProbe(
        name=name,
        available=available,
        endpoint=endpoint,
        status="available" if available else "unreachable",
        detail=None,
    )


def test_surface_report_recommends_official_when_both_surfaces_are_available():
    report = build_surface_report(
        official_mcp=_probe("official_mcp", True, "http://127.0.0.1:8000/mcp"),
        soft_ue_bridge=_probe("soft_ue_bridge", True, "http://127.0.0.1:8080"),
    )

    assert report["availability"] == "both"
    assert report["recommendation"]["primary"] == "official-mcp"
    assert report["recommendation"]["fallback"] == "soft-ue-bridge"
    assert "UE 5.8 editor-native" in report["recommendation"]["reason"]


def test_surface_report_recommends_official_when_only_official_mcp_is_available():
    report = build_surface_report(
        official_mcp=_probe("official_mcp", True, "http://127.0.0.1:8000/mcp"),
        soft_ue_bridge=_probe("soft_ue_bridge", False, "http://127.0.0.1:8080"),
    )

    assert report["availability"] == "official-only"
    assert report["recommendation"]["primary"] == "official-mcp"
    assert report["recommendation"]["fallback"] is None


def test_surface_report_recommends_bridge_when_only_soft_ue_bridge_is_available():
    report = build_surface_report(
        official_mcp=_probe("official_mcp", False, "http://127.0.0.1:8000/mcp"),
        soft_ue_bridge=_probe("soft_ue_bridge", True, "http://127.0.0.1:8080"),
    )

    assert report["availability"] == "bridge-only"
    assert report["recommendation"]["primary"] == "soft-ue-bridge"
    assert report["recommendation"]["fallback"] is None
    assert "runtime" in report["recommendation"]["reason"]


def test_surface_report_returns_no_primary_when_neither_surface_is_available():
    report = build_surface_report(
        official_mcp=_probe("official_mcp", False, "http://127.0.0.1:8000/mcp"),
        soft_ue_bridge=_probe("soft_ue_bridge", False, "http://127.0.0.1:8080"),
    )

    assert report["availability"] == "neither"
    assert report["recommendation"]["primary"] is None
    assert report["recommendation"]["fallback"] is None
    assert "Start Unreal Editor" in report["recommendation"]["reason"]


def test_probe_official_mcp_accepts_successful_local_initialize(monkeypatch):
    request = httpx.Request("POST", "http://127.0.0.1:8000/mcp")

    def fake_post(url, **kwargs):
        assert url == "http://127.0.0.1:8000/mcp"
        assert kwargs["headers"]["MCP-Protocol-Version"]
        return httpx.Response(200, json={"jsonrpc": "2.0", "result": {}}, request=request)

    monkeypatch.setattr(surface_selector.httpx, "post", fake_post)

    result = surface_selector.probe_official_mcp("http://127.0.0.1:8000/mcp", timeout=0.5)

    assert result.available is True
    assert result.status == "available"


def test_probe_official_mcp_rejects_remote_endpoint_without_network(monkeypatch):
    monkeypatch.setattr(surface_selector.httpx, "post", lambda *_args, **_kwargs: (_ for _ in ()).throw(AssertionError))

    result = surface_selector.probe_official_mcp("http://example.com/mcp", timeout=0.5)

    assert result.available is False
    assert result.status == "remote_endpoint_rejected"


def test_probe_soft_ue_bridge_uses_existing_health_discovery(monkeypatch):
    monkeypatch.setattr("soft_ue_cli.discovery.get_server_url", lambda: "http://127.0.0.1:8080")
    monkeypatch.setattr("soft_ue_cli.client.health_check", lambda timeout=1.0: {"status": "ok"})

    result = surface_selector.probe_soft_ue_bridge(timeout=0.5)

    assert result.available is True
    assert result.endpoint == "http://127.0.0.1:8080"
    assert result.status == "available"
