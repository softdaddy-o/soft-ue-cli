"""MCP server mode for soft-ue-cli.

Exposes CLI commands as MCP tools and skills as MCP prompts
over stdio transport using FastMCP.

The ``mcp`` package is imported at call time so this module
can be imported without mcp installed (for testing/introspection).
"""

from __future__ import annotations

import io
import json
import sys
from typing import Any

from . import client as _client
from . import streak as _streak
from .errors import BridgeError, ErrorKind, bug_nudge_payload
from .mcp_schema import extract_tools
from .skills import get_skill, list_skills


def _make_tool_fn(tool_name: str):
    """Create a tool handler function that forwards to call_tool()."""

    def tool_fn(**kwargs: Any) -> str:
        # Filter out None and False (unset optional / store_true args)
        arguments = {k: v for k, v in kwargs.items() if v is not None and v is not False}

        try:
            result = _client.call_tool(tool_name, arguments)
        except BridgeError as exc:
            error_response = {"error": f"Tool '{tool_name}' failed: {exc.message}"}
            if exc.kind == ErrorKind.UNEXPECTED:
                error_response["bug_report_hint"] = bug_nudge_payload(
                    exc.tool_name, exc.message,
                )
            return json.dumps(error_response, indent=2, ensure_ascii=False)

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

        fn = _make_tool_fn(name)
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
