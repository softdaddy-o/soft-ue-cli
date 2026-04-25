"""HTTP/JSON-RPC client for the SoftUEBridge server."""

from __future__ import annotations

import itertools
import json
import os
import sys
from typing import Any

import httpx

from .discovery import get_server_url

_id_counter = itertools.count(1)


def call_tool(tool_name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    """Call a tool on the SoftUEBridge server and return the parsed result.

    Raises SystemExit(1) on connection errors or tool errors.
    """
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
        print(
            f"error: cannot connect to SoftUEBridge at {endpoint}\n"
            "Make sure the plugin is enabled and the game is running.",
            file=sys.stderr,
        )
        sys.exit(1)
    except httpx.TimeoutException:
        print(
            f"error: request timed out after {timeout:.0f}s\n"
            "For slow operations (e.g. build-and-relaunch), use --timeout <seconds>.",
            file=sys.stderr,
        )
        sys.exit(1)
    except httpx.HTTPStatusError as exc:
        print(f"error: HTTP {exc.response.status_code}", file=sys.stderr)
        sys.exit(1)

    try:
        data = response.json()
    except Exception:
        print("error: server returned non-JSON response", file=sys.stderr)
        sys.exit(1)

    if "error" in data:
        err = data["error"]
        print(f"error: {err.get('message', err)}", file=sys.stderr)
        sys.exit(1)

    result = data.get("result", {})
    if result.get("isError"):
        content = result.get("content", [])
        msg = content[0].get("text", "unknown error") if content else "unknown error"
        print(f"error: {msg}", file=sys.stderr)
        sys.exit(1)

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
