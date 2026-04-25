"""HTTP/JSON-RPC client for the SoftUEBridge server."""

from __future__ import annotations

import itertools
import json
import os
from typing import Any

import httpx

from .discovery import get_server_url

_id_counter = itertools.count(1)


def call_tool(tool_name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    """Call a tool on the SoftUEBridge server and return the parsed result.

    Raises BridgeError on connection errors or tool errors.
    """
    from .errors import BridgeError, ErrorKind

    url = get_server_url()
    endpoint = f"{url}/bridge"
    timeout = float(os.environ.get("SOFT_UE_BRIDGE_TIMEOUT", "30"))

    payload = {
        "jsonrpc": "2.0",
        "id": str(next(_id_counter)),
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": arguments},
    }

    try:
        response = httpx.post(endpoint, json=payload, timeout=timeout)
        response.raise_for_status()
    except httpx.ConnectError:
        raise BridgeError(
            kind=ErrorKind.EXPECTED,
            message=(
                f"cannot connect to SoftUEBridge at {endpoint}\n"
                "Make sure the plugin is enabled and the game is running."
            ),
            tool_name=tool_name,
            arguments=arguments,
        )
    except httpx.TimeoutException:
        raise BridgeError(
            kind=ErrorKind.EXPECTED,
            message=(
                f"request timed out after {timeout:.0f}s\n"
                "Possible causes:\n"
                "  - A modal dialog may be blocking the UE editor (check for popups)\n"
                "  - The operation is slow (set SOFT_UE_BRIDGE_TIMEOUT=<seconds>)"
            ),
            tool_name=tool_name,
            arguments=arguments,
        )
    except httpx.HTTPStatusError as exc:
        kind = ErrorKind.UNEXPECTED if exc.response.status_code >= 500 else ErrorKind.EXPECTED
        raise BridgeError(
            kind=kind,
            message=f"HTTP {exc.response.status_code}",
            tool_name=tool_name,
            arguments=arguments,
        )

    try:
        data = response.json()
    except Exception:
        raise BridgeError(
            kind=ErrorKind.UNEXPECTED,
            message="server returned non-JSON response",
            tool_name=tool_name,
            arguments=arguments,
        )

    if "error" in data:
        err = data["error"]
        raise BridgeError(
            kind=ErrorKind.UNEXPECTED,
            message=str(err.get("message", err)),
            tool_name=tool_name,
            arguments=arguments,
        )

    result = data.get("result", {})
    if result.get("isError"):
        content = result.get("content", [])
        msg = content[0].get("text", "unknown error") if content else "unknown error"
        raise BridgeError(
            kind=ErrorKind.UNEXPECTED,
            message=msg,
            tool_name=tool_name,
            arguments=arguments,
        )

    # Parse text content as JSON when possible
    content = result.get("content", [])
    if content and content[0].get("type") == "text":
        text = content[0]["text"]
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"text": text}

    return result


def health_check() -> dict[str, Any]:
    """GET /bridge health check."""
    url = get_server_url()
    try:
        response = httpx.get(f"{url}/bridge", timeout=5.0)
        response.raise_for_status()
        return response.json()
    except Exception as exc:
        return {"error": str(exc)}
