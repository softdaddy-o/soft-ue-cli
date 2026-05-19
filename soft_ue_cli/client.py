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


def _handle_startup_recovery_for_connection() -> str | None:
    """Handle an Unreal startup recovery prompt before retrying a failed connection."""
    from .startup_recovery import handle_startup_recovery_prompt

    interactive = sys.stdin.isatty()
    requested_action = "ask" if interactive else "remembered"
    result = handle_startup_recovery_prompt(requested_action, interactive=interactive)
    if result is None:
        return None
    if result.action == "manual":
        return (
            "Unreal Editor startup recovery prompt is visible. Choose in the editor or rerun a "
            "launch workflow with --startup-recovery recover|skip --remember-startup-recovery."
        )
    return f"handled Unreal startup recovery prompt with action '{result.action}'"


def call_tool(tool_name: str, arguments: dict[str, Any], timeout: float | None = None) -> dict[str, Any]:
    """Call a tool on the SoftUEBridge server and return the parsed result.

    Raises BridgeError on connection errors or tool errors.
    """
    from .errors import BridgeError, ErrorKind

    url = get_server_url()
    endpoint = f"{url}/bridge"
    timeout = timeout if timeout is not None else float(os.environ.get("SOFT_UE_BRIDGE_TIMEOUT", "30"))

    payload = {
        "jsonrpc": "2.0",
        "id": str(next(_id_counter)),
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": arguments},
    }

    def post_once() -> httpx.Response:
        response = httpx.post(endpoint, json=payload, timeout=timeout)
        response.raise_for_status()
        return response

    def connection_message(exc: httpx.ConnectError | httpx.TimeoutException, note: str | None = None) -> str:
        if isinstance(exc, httpx.ConnectError):
            message = (
                f"cannot connect to SoftUEBridge at {endpoint}\n"
                "Make sure the plugin is enabled and the game is running."
            )
        else:
            message = (
                f"request timed out after {timeout:.0f}s\n"
                "Possible causes:\n"
                "  - A modal dialog may be blocking the UE editor (check for popups)\n"
                "  - The operation is slow (set SOFT_UE_BRIDGE_TIMEOUT=<seconds>)"
            )
        if note:
            message += f"\n{note}"
        return message

    try:
        response = post_once()
    except (httpx.ConnectError, httpx.TimeoutException) as exc:
        recovery_note = None
        try:
            recovery_note = _handle_startup_recovery_for_connection()
        except Exception as recovery_exc:
            recovery_note = str(recovery_exc)

        if recovery_note and recovery_note.startswith("handled "):
            try:
                response = post_once()
            except (httpx.ConnectError, httpx.TimeoutException) as retry_exc:
                raise BridgeError(
                    kind=ErrorKind.EXPECTED,
                    message=connection_message(retry_exc, recovery_note),
                    tool_name=tool_name,
                    arguments=arguments,
                )
            except httpx.HTTPStatusError as retry_exc:
                kind = ErrorKind.UNEXPECTED if retry_exc.response.status_code >= 500 else ErrorKind.EXPECTED
                raise BridgeError(
                    kind=kind,
                    message=f"HTTP {retry_exc.response.status_code}",
                    tool_name=tool_name,
                    arguments=arguments,
                )
        else:
            raise BridgeError(
                kind=ErrorKind.EXPECTED,
                message=connection_message(exc, recovery_note),
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


def health_check(timeout: float = 5.0) -> dict[str, Any]:
    """GET /bridge health check."""
    url = get_server_url()
    try:
        response = httpx.get(f"{url}/bridge", timeout=timeout)
        response.raise_for_status()
        return response.json()
    except Exception as exc:
        return {"error": str(exc)}
