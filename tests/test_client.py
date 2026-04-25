"""Tests for cli/soft_ue_cli/client.py — uses httpx mock transport."""
from __future__ import annotations

import json
from unittest.mock import patch

import httpx
import pytest

from soft_ue_cli import client as client_mod
from soft_ue_cli.client import call_tool, health_check


_DUMMY_REQUEST = httpx.Request("POST", "http://127.0.0.1:8080/bridge")


def _resp(status: int, body: dict | None = None, text: str | None = None) -> httpx.Response:
    """Build an httpx.Response with a dummy request so raise_for_status() works."""
    if text is not None:
        r = httpx.Response(status, text=text, request=_DUMMY_REQUEST)
    else:
        r = httpx.Response(status, json=body or {}, request=_DUMMY_REQUEST)
    return r


def _patch_url(url: str = "http://127.0.0.1:8080"):
    return patch("soft_ue_cli.client.get_server_url", return_value=url)


# -- call_tool -----------------------------------------------------------------

def test_call_tool_success(monkeypatch):
    payload = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {"content": [{"type": "text", "text": '{"actors": []}'}]},
    }
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, payload))
    with _patch_url():
        result = call_tool("query-level", {})
    assert result == {"actors": []}


def test_call_tool_text_content_non_json(monkeypatch):
    payload = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {"content": [{"type": "text", "text": "plain text response"}]},
    }
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, payload))
    with _patch_url():
        result = call_tool("get-logs", {})
    assert result == {"text": "plain text response"}


def test_call_tool_result_is_error(monkeypatch):
    payload = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {"isError": True, "content": [{"text": "actor not found"}]},
    }
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, payload))
    with _patch_url():
        with pytest.raises(SystemExit) as exc:
            call_tool("spawn-actor", {"class": "BadClass"})
    assert exc.value.code == 1


def test_call_tool_jsonrpc_error(monkeypatch):
    payload = {"jsonrpc": "2.0", "id": "1", "error": {"code": -32601, "message": "Method not found"}}
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, payload))
    with _patch_url():
        with pytest.raises(SystemExit) as exc:
            call_tool("unknown-tool", {})
    assert exc.value.code == 1


def test_call_tool_connect_error(monkeypatch):
    def raise_connect(*args, **kw):
        raise httpx.ConnectError("refused")
    monkeypatch.setattr(httpx, "post", raise_connect)
    with _patch_url():
        with pytest.raises(SystemExit) as exc:
            call_tool("status", {})
    assert exc.value.code == 1


def test_call_tool_http_error(monkeypatch):
    def raise_http(*args, **kw):
        response = httpx.Response(500)
        raise httpx.HTTPStatusError("500", request=httpx.Request("POST", "http://x"), response=response)
    monkeypatch.setattr(httpx, "post", raise_http)
    with _patch_url():
        with pytest.raises(SystemExit) as exc:
            call_tool("status", {})
    assert exc.value.code == 1


def test_call_tool_non_json_response(monkeypatch):
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, text="<html>not json</html>"))
    with _patch_url():
        with pytest.raises(SystemExit) as exc:
            call_tool("status", {})
    assert exc.value.code == 1


def test_call_tool_empty_result(monkeypatch):
    payload = {"jsonrpc": "2.0", "id": "1", "result": {}}
    monkeypatch.setattr(httpx, "post", lambda url, **kw: _resp(200, payload))
    with _patch_url():
        result = call_tool("set-console-var", {"name": "r.VSync", "value": "0"})
    assert result == {}


# -- health_check --------------------------------------------------------------

def test_health_check_success(monkeypatch):
    req = httpx.Request("GET", "http://127.0.0.1:8080/bridge")
    monkeypatch.setattr(httpx, "get", lambda url, **kw: httpx.Response(200, json={"status": "ok"}, request=req))
    with _patch_url():
        result = health_check()
    assert result == {"status": "ok"}


def test_health_check_connection_error(monkeypatch):
    def raise_exc(*args, **kw):
        raise httpx.ConnectError("refused")
    monkeypatch.setattr(httpx, "get", raise_exc)
    with _patch_url():
        result = health_check()
    assert "error" in result


def test_health_check_timeout(monkeypatch):
    def raise_timeout(*args, **kw):
        raise httpx.TimeoutException("timeout")
    monkeypatch.setattr(httpx, "get", raise_timeout)
    with _patch_url():
        result = health_check()
    assert "error" in result
