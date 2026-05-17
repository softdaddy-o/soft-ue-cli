"""Tests for cli/soft_ue_cli/mcp_server.py ??MCP server tool/prompt registration."""

from __future__ import annotations

import json
from unittest.mock import patch

import pytest


# Skip all tests if mcp is not installed
mcp = pytest.importorskip("mcp")

from soft_ue_cli.errors import BridgeError, ErrorKind
from soft_ue_cli.mcp_schema import extract_tools
from soft_ue_cli.mcp_server import create_server, _make_client_tool_fn


def test_create_server_returns_fastmcp():
    from mcp.server.fastmcp import FastMCP
    server = create_server()
    assert isinstance(server, FastMCP)


def test_server_has_tools():
    # NOTE: _tool_manager/_tools are private FastMCP internals.
    # No public enumeration API exists as of mcp 1.26. Update if one is added.
    server = create_server()
    assert server._tool_manager is not None
    assert len(server._tool_manager._tools) >= 60


def test_server_has_prompts():
    server = create_server()
    assert server._prompt_manager is not None
    assert len(server._prompt_manager._prompts) > 0


def test_anim_state_machine_tools_have_native_mcp_types():
    tools = {tool["name"]: tool for tool in extract_tools()}

    assert tools["add-anim-state-machine"]["parameters"]["properties"]["position"]["type"] == "array"
    assert tools["add-anim-state"]["parameters"]["properties"]["position"]["type"] == "array"
    assert tools["add-anim-transition"]["parameters"]["properties"]["rule"]["type"] == "boolean"


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_forwards_to_bridge(mock_call_tool):
    mock_call_tool.return_value = {"actors": []}
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "query-level":
            tool_fn = tool.fn
            break

    assert tool_fn is not None, "query-level tool not registered"
    result = tool_fn(limit=10)
    mock_call_tool.assert_called_once()
    call_args = mock_call_tool.call_args
    assert call_args[0][0] == "query-level"
    assert "limit" in call_args[0][1]


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_maps_no_auto_position(mock_call_tool):
    mock_call_tool.return_value = {"status": "ok"}
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "add-graph-node":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    result = tool_fn(asset_path="/Game/BP", node_class="K2Node_CallFunction", no_auto_position=True)
    mock_call_tool.assert_called_once()
    call_args = mock_call_tool.call_args
    assert call_args[0][0] == "add-graph-node"
    assert call_args.kwargs == {"timeout": None}
    arguments = call_args[0][1]
    assert arguments["auto_position"] is False
    assert "no_auto_position" not in arguments
    parsed = json.loads(result)
    assert parsed == {"status": "ok"}


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_forwards_pie_timeout_to_http_timeout(mock_call_tool):
    mock_call_tool.return_value = {"status": "ok"}
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "pie-session":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    tool_fn(action="start", timeout=42.5)
    mock_call_tool.assert_called_once()
    call_kwargs = mock_call_tool.call_args.kwargs
    assert call_kwargs["timeout"] == 42.5
    arguments = mock_call_tool.call_args.args[1]
    assert arguments["action"] == "start"
    assert arguments["timeout"] == 42.5


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_normalizes_add_graph_node_created_nodes(mock_call_tool):
    mock_call_tool.return_value = {
        "status": True,
        "created_nodes": [
            {"guid": "11111111-1111-1111-1111-111111111111", "class": "AnimGraphNode_Root"},
            {"guid": "22222222-2222-2222-2222-222222222222", "class": "AnimGraphNode_LinkedInputPose"},
        ],
    }
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "add-graph-node":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    result = tool_fn(asset_path="/Game/ALI", node_class="AnimLayerFunction", graph_name="ALIGraph")
    parsed = json.loads(result)
    assert parsed["node_guid"] == "11111111-1111-1111-1111-111111111111"


@patch("soft_ue_cli.__main__.call_tool")
def test_mcp_add_co_parameter_uses_cli_transform(mock_call_tool):
    mock_call_tool.return_value = {"success": True}
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "add-co-parameter":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    result = tool_fn(
        asset_path="/Game/Characters/CO_Hero.CO_Hero",
        name="BodyHeight",
        parameter_type="float",
    )

    mock_call_tool.assert_called_once()
    assert mock_call_tool.call_args.args[0] == "add-customizable-object-node"
    assert mock_call_tool.call_args.args[1] == {
        "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
        "node_class": "CustomizableObjectNodeFloatParameter",
        "properties": {"ParameterName": "BodyHeight"},
    }
    assert json.loads(result) == {"success": True}


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_stops_pie_on_start_timeout(mock_call_tool):
    def side_effect(tool_name, arguments, timeout=None):
        if tool_name == "pie-session" and arguments.get("action") == "start":
            raise BridgeError(
                kind=ErrorKind.EXPECTED,
                message="request timed out after 30s",
                tool_name=tool_name,
                arguments=arguments,
            )
        return {"stopped": True}

    mock_call_tool.side_effect = side_effect
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "pie-session":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    result = tool_fn(action="start", timeout=30)
    parsed = json.loads(result)
    assert parsed["error"] == "Tool 'pie-session' failed: request timed out after 30s"
    assert mock_call_tool.call_count == 2
    assert mock_call_tool.call_args.args[0] == "pie-session"
    assert mock_call_tool.call_args.args[1]["action"] == "stop"


@patch("soft_ue_cli.client.call_tool")
def test_tool_call_returns_json_string(mock_call_tool):
    mock_call_tool.return_value = {"status": "ok"}
    server = create_server()

    tool_fn = None
    for tool in server._tool_manager._tools.values():
        if tool.name == "query-level":
            tool_fn = tool.fn
            break

    assert tool_fn is not None
    result = tool_fn(limit=1)
    assert isinstance(result, str)
    parsed = json.loads(result)
    assert parsed == {"status": "ok"}


def test_client_tool_fn_handles_system_exit():
    def exiting_cmd(_args):
        raise SystemExit(1)

    tool_fn = _make_client_tool_fn("failing-tool", exiting_cmd, {})
    result = tool_fn()
    parsed = json.loads(result)
    assert parsed == {"error": "Command 'failing-tool' exited with code 1"}


def test_client_tool_fn_handles_exception():
    def failing_cmd(_args):
        raise RuntimeError("boom")

    tool_fn = _make_client_tool_fn("failing-tool", failing_cmd, {})
    result = tool_fn()
    parsed = json.loads(result)
    assert parsed == {"error": "Command 'failing-tool' failed: boom"}


def test_prompt_list_has_blueprint_to_cpp():
    server = create_server()
    prompts = server._prompt_manager._prompts
    prompt_names = {p for p in prompts}
    assert "blueprint-to-cpp" in prompt_names
    assert "author-test" in prompt_names


def test_prompt_fn_returns_content():
    server = create_server()
    prompt = server._prompt_manager._prompts.get("blueprint-to-cpp")
    assert prompt is not None
    result = prompt.fn()
    assert isinstance(result, str)
    assert "Blueprint to C++" in result
