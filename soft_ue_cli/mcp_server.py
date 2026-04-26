"""MCP server mode for soft-ue-cli.

Exposes CLI commands as MCP tools and skills as MCP prompts
over stdio transport using FastMCP.

The ``mcp`` package is imported at call time so this module
can be imported without mcp installed (for testing/introspection).
"""

from __future__ import annotations

import inspect
import io
import json
import sys
from typing import Any, List, Optional

import argparse as _argparse

from . import client as _client
from . import streak as _streak
from .errors import BridgeError, ErrorKind, bug_nudge_payload
from .mcp_schema import CLIENT_SIDE_COMMANDS, extract_tools
from .skills import get_skill, list_skills

# CLI command names that differ from their bridge tool names.
_BRIDGE_TOOL_NAME_MAP: dict[str, str] = {
    "class-hierarchy": "get-class-hierarchy",
    "project-info": "get-project-info",
}

# Per-tool parameter renames: MCP exposes argparse dest names, but some bridge
# tools expect different key names. Map (tool_name → {mcp_param: bridge_param}).
_PARAM_RENAMES: dict[str, dict[str, str]] = {
    "query-asset": {"asset_class": "class"},
}

_JSON_TYPE_TO_PY: dict[str, type] = {
    "string": str,
    "boolean": bool,
    "integer": int,
    "number": float,
    "array": list,
}


def _build_signature(params: dict | None) -> inspect.Signature:
    """Build a typed inspect.Signature from a tool's JSON Schema parameters dict.

    FastMCP introspects __signature__ to generate the MCP tool's JSON schema,
    so each parameter must be a proper named, typed, keyword-only Parameter.

    Special types beyond standard JSON Schema:
      "any"   — uses typing.Any so pydantic accepts any JSON value
      "array" — uses list so pydantic accepts JSON arrays
    """
    if not params:
        return inspect.Signature([])
    properties = params.get("properties", {})
    required_params = set(params.get("required", []))
    sig_params: list[inspect.Parameter] = []
    for param_name, prop in properties.items():
        json_type = prop.get("type", "string")
        if json_type == "any":
            # Accept any JSON value — no pydantic type constraint
            if param_name in required_params:
                annotation: Any = Any
                default = inspect.Parameter.empty
            else:
                annotation = Optional[Any]
                default = prop.get("default", None)
        else:
            py_type = _JSON_TYPE_TO_PY.get(json_type, str)
            if param_name in required_params:
                annotation = py_type
                default = inspect.Parameter.empty
            else:
                annotation = Optional[py_type]
                default = prop.get("default", None)
        sig_params.append(
            inspect.Parameter(
                param_name,
                inspect.Parameter.KEYWORD_ONLY,
                default=default,
                annotation=annotation,
            )
        )
    return inspect.Signature(sig_params)


def _normalize_add_graph_node_result(result: dict[str, Any]) -> dict[str, Any]:
    """Normalize add-graph-node results to always include ``node_guid`` when available."""
    if not isinstance(result, dict):
        return result

    node_guid = result.get("node_guid")
    if isinstance(node_guid, str) and node_guid:
        return result

    # Blueprint tools return ``created_nodes`` for special cases (e.g. AnimLayerFunction),
    # so extract the first available guid for compatibility.
    candidates: list[Any] = []
    created_nodes = result.get("created_nodes")
    if isinstance(created_nodes, list):
        candidates.extend(created_nodes)

    if not isinstance(node_guid, str):
        for candidate in candidates:
            if not isinstance(candidate, dict):
                continue
            for guid_key in ("node_guid", "guid"):
                value = candidate.get(guid_key)
                if isinstance(value, str) and value:
                    result["node_guid"] = value
                    return result

    return result


def _make_tool_fn(tool_name: str, params: dict | None = None):
    """Create a bridge tool handler that forwards kwargs to call_tool()."""
    bridge_name = _BRIDGE_TOOL_NAME_MAP.get(tool_name, tool_name)

    def tool_fn(**kwargs: Any) -> str:
        # Filter out None and falsey defaults for store_true-style args, while
        # keeping explicit values that must be translated into bridge args.
        arguments = {k: v for k, v in kwargs.items() if v is not None}

        # MCP exposes argparse dest names (no_auto_position). The bridge expects
        # auto_position=true/false, with false meaning "disable auto placement".
        if arguments.pop("no_auto_position", False):
            arguments["auto_position"] = False

        # Apply any per-tool parameter renames
        for mcp_name, bridge_name_param in _PARAM_RENAMES.get(tool_name, {}).items():
            if mcp_name in arguments:
                arguments[bridge_name_param] = arguments.pop(mcp_name)

        # Use tool-level timeout as HTTP timeout for pie-session start, to avoid
        # MCP request timeout when PIE warmup needs longer.
        http_timeout = None
        if tool_name == "pie-session" and arguments.get("action") == "start":
            timeout_arg = arguments.get("timeout")
            if timeout_arg is not None:
                try:
                    http_timeout = float(timeout_arg)
                except (TypeError, ValueError):
                    http_timeout = None

        try:
            result = _client.call_tool(bridge_name, arguments, timeout=http_timeout)
        except BridgeError as exc:
            # If PIE startup times out, attempt a best-effort stop to avoid leaving
            # the editor in a partially-initialized session state.
            if tool_name == "pie-session" and arguments.get("action") == "start":
                if "timed out" in exc.message.lower():
                    try:
                        _client.call_tool("pie-session", {"action": "stop"}, timeout=5.0)
                    except Exception:
                        pass
            error_response = {"error": f"Tool '{tool_name}' failed: {exc.message}"}
            if exc.kind == ErrorKind.UNEXPECTED:
                error_response["bug_report_hint"] = bug_nudge_payload(
                    exc.tool_name, exc.message,
                )
            return json.dumps(error_response, indent=2, ensure_ascii=False)

        if tool_name == "add-graph-node":
            try:
                result = _normalize_add_graph_node_result(result)
            except Exception as exc:
                return json.dumps({"error": f"Tool '{tool_name}' failed: {exc}"}, indent=2, ensure_ascii=False)

        # Record streak (best-effort)
        try:
            _streak.record_success(tool_name)
            if _streak.should_nudge_testimonial():
                result["testimonial_nudge"] = {
                    "message": (
                        "You've been using soft-ue-cli successfully for 3+ days! "
                        "If it's been helpful, consider sharing a testimonial."
                    ),
                    "suggested_command": "submit-testimonial",
                    "suggested_args": {
                        "message": "Share what you liked about soft-ue-cli",
                        "agent-name": "your agent name",
                    },
                }
                _streak.mark_nudged()
        except Exception:
            pass

        return json.dumps(result, indent=2, ensure_ascii=False)

    tool_fn.__name__ = tool_name.replace("-", "_")
    tool_fn.__qualname__ = tool_fn.__name__
    tool_fn.__signature__ = _build_signature(params)

    return tool_fn


def _make_client_tool_fn(tool_name: str, cmd_fn, params: dict | None = None):
    """Create a client-side tool handler that runs the existing cmd_* function.

    Captures stdout so the output reaches the MCP client instead of the terminal.
    SystemExit (from error paths in cmd_* handlers) is caught and reported cleanly.
    """

    def tool_fn(**kwargs: Any) -> str:
        namespace = _argparse.Namespace(**kwargs)
        if tool_name == "config" and hasattr(namespace, "subcommand") and not hasattr(namespace, "config_action"):
            namespace.config_action = namespace.subcommand
        buffer = io.StringIO()
        old_stdout = sys.stdout
        sys.stdout = buffer
        output = ""
        try:
            cmd_fn(namespace)
            output = buffer.getvalue().strip()
        except SystemExit as exc:
            output = buffer.getvalue().strip()
            return output or json.dumps(
                {"error": f"Command '{tool_name}' exited with code {exc.code}"},
                indent=2,
            )
        except Exception as exc:
            output = buffer.getvalue().strip()
            return output or json.dumps(
                {"error": f"Command '{tool_name}' failed: {exc}"},
                indent=2,
            )
        finally:
            sys.stdout = old_stdout

        output = output or buffer.getvalue().strip()
        return output or json.dumps({"status": "ok"}, indent=2)

    tool_fn.__name__ = tool_name.replace("-", "_")
    tool_fn.__qualname__ = tool_fn.__name__
    tool_fn.__signature__ = _build_signature(params)

    return tool_fn


def create_server():
    """Create and configure the MCP server with all tools and prompts."""
    from mcp.server.fastmcp import FastMCP
    from mcp.server.fastmcp.prompts import Prompt

    mcp = FastMCP("soft-ue-cli")

    # Register tools from argparse introspection
    for tool_def in extract_tools():
        name = tool_def["name"]
        description = tool_def["description"]
        params = tool_def["parameters"]

        if name in CLIENT_SIDE_COMMANDS:
            fn = _make_client_tool_fn(name, tool_def["func"], params)
        else:
            fn = _make_tool_fn(name, params)
        fn.__doc__ = description

        mcp.add_tool(fn, name=name, description=description)

    # Register skills as prompts
    for skill in list_skills():
        skill_name = skill["name"]
        skill_desc = skill["description"]

        def make_prompt_fn(sn: str, sd: str):
            def prompt_fn() -> str:
                content = get_skill(sn)
                return content or f"Skill '{sn}' not found"
            prompt_fn.__name__ = sn.replace("-", "_")
            prompt_fn.__qualname__ = prompt_fn.__name__
            prompt_fn.__doc__ = sd
            return prompt_fn

        prompt = Prompt.from_function(
            make_prompt_fn(skill_name, skill_desc),
            name=skill_name,
            description=skill_desc,
        )
        mcp.add_prompt(prompt)

    return mcp


def run_server() -> None:
    """Run the MCP server over stdio transport."""
    server = create_server()
    server.run(transport="stdio")
