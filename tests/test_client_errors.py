"""Tests for call_tool raising BridgeError instead of SystemExit."""

from unittest.mock import patch, MagicMock
import httpx
import pytest

from soft_ue_cli.client import call_tool
from soft_ue_cli.errors import BridgeError, ErrorKind


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_connect_error_raises_expected(mock_url, mock_post):
    mock_post.side_effect = httpx.ConnectError("refused")
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {"class_name": "Foo"})
    assert exc_info.value.kind == ErrorKind.EXPECTED
    assert exc_info.value.tool_name == "spawn-actor"


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_timeout_raises_expected(mock_url, mock_post):
    mock_post.side_effect = httpx.TimeoutException("timed out")
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.EXPECTED


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_http_5xx_raises_unexpected(mock_url, mock_post):
    response = MagicMock()
    response.status_code = 500
    response.raise_for_status.side_effect = httpx.HTTPStatusError(
        "server error", request=MagicMock(), response=response,
    )
    mock_post.return_value = response
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.UNEXPECTED


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_http_4xx_raises_expected(mock_url, mock_post):
    response = MagicMock()
    response.status_code = 404
    response.raise_for_status.side_effect = httpx.HTTPStatusError(
        "not found", request=MagicMock(), response=response,
    )
    mock_post.return_value = response
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.EXPECTED


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_non_json_response_raises_unexpected(mock_url, mock_post):
    response = MagicMock()
    response.raise_for_status.return_value = None
    response.json.side_effect = ValueError("not json")
    mock_post.return_value = response
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.UNEXPECTED


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_rpc_error_raises_unexpected(mock_url, mock_post):
    response = MagicMock()
    response.raise_for_status.return_value = None
    response.json.return_value = {"error": {"message": "internal rpc error"}}
    mock_post.return_value = response
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.UNEXPECTED
    assert "internal rpc error" in exc_info.value.message


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_tool_is_error_raises_unexpected(mock_url, mock_post):
    response = MagicMock()
    response.raise_for_status.return_value = None
    response.json.return_value = {
        "result": {
            "isError": True,
            "content": [{"text": "actor not found"}],
        }
    }
    mock_post.return_value = response
    with pytest.raises(BridgeError) as exc_info:
        call_tool("spawn-actor", {})
    assert exc_info.value.kind == ErrorKind.UNEXPECTED
    assert "actor not found" in exc_info.value.message


@patch("soft_ue_cli.client.httpx.post")
@patch("soft_ue_cli.client.get_server_url", return_value="http://localhost:8080")
def test_success_returns_parsed_json(mock_url, mock_post):
    response = MagicMock()
    response.raise_for_status.return_value = None
    response.json.return_value = {
        "result": {
            "content": [{"type": "text", "text": '{"spawned": true}'}]
        }
    }
    mock_post.return_value = response
    result = call_tool("spawn-actor", {"class_name": "Foo"})
    assert result == {"spawned": True}
