"""HTTP/JSON-RPC client for the SoftUEBridge server."""

from __future__ import annotations

import itertools
import json
import os
import sys
from typing import Any

import httpx

from .discovery import get_forced_port_fallback_url, get_server_url

_id_counter = itertools.count(1)


def _forced_port_fallback_warning(original_url: str, fallback_url: str) -> str:
    return (
        "SOFT_UE_BRIDGE_PORT appears stale: "
        f"{original_url} is unreachable or not a SoftUEBridge server, "
        f"using live bridge from .soft-ue-bridge/instance.json at {fallback_url}."
    )


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

    def post_once(target_endpoint: str) -> httpx.Response:
        response = httpx.post(target_endpoint, json=payload, timeout=timeout)
        response.raise_for_status()
        return response

    def bridge_error_from_http(exc: httpx.HTTPStatusError) -> BridgeError:
        kind = ErrorKind.UNEXPECTED if exc.response.status_code >= 500 else ErrorKind.EXPECTED
        return BridgeError(
            kind=kind,
            message=f"HTTP {exc.response.status_code}",
            tool_name=tool_name,
            arguments=arguments,
        )

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

    def recover_or_raise(exc: httpx.ConnectError | httpx.TimeoutException) -> httpx.Response:
        recovery_note = None
        try:
            recovery_note = _handle_startup_recovery_for_connection()
        except Exception as recovery_exc:
            recovery_note = str(recovery_exc)

        if recovery_note and recovery_note.startswith("handled "):
            try:
                return post_once(endpoint)
            except (httpx.ConnectError, httpx.TimeoutException) as retry_exc:
                raise BridgeError(
                    kind=ErrorKind.EXPECTED,
                    message=connection_message(retry_exc, recovery_note),
                    tool_name=tool_name,
                    arguments=arguments,
                )
            except httpx.HTTPStatusError as retry_exc:
                raise bridge_error_from_http(retry_exc)

        raise BridgeError(
            kind=ErrorKind.EXPECTED,
            message=connection_message(exc, recovery_note),
            tool_name=tool_name,
            arguments=arguments,
        )

    def forced_port_fallback_response() -> httpx.Response | None:
        fallback_url = get_forced_port_fallback_url(url)
        if not fallback_url:
            return None
        fallback_endpoint = f"{fallback_url}/bridge"
        response = post_once(fallback_endpoint)
        print(_forced_port_fallback_warning(url, fallback_url), file=sys.stderr)
        return response

    try:
        response = post_once(endpoint)
    except (httpx.ConnectError, httpx.TimeoutException) as exc:
        try:
            response = forced_port_fallback_response()
        except (httpx.ConnectError, httpx.TimeoutException):
            response = None
        except httpx.HTTPStatusError as fallback_http_exc:
            raise bridge_error_from_http(fallback_http_exc)
        if response is None:
            response = recover_or_raise(exc)
    except httpx.HTTPStatusError as exc:
        try:
            response = forced_port_fallback_response()
        except (httpx.ConnectError, httpx.TimeoutException):
            response = None
        except httpx.HTTPStatusError as fallback_http_exc:
            raise bridge_error_from_http(fallback_http_exc)
        if response is None:
            raise bridge_error_from_http(exc)

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

    def fallback_health_result() -> dict[str, Any] | None:
        fallback_url = get_forced_port_fallback_url(url)
        if not fallback_url:
            return None
        response = httpx.get(f"{fallback_url}/bridge", timeout=5.0)
        response.raise_for_status()
        result = response.json()
        result["warning"] = _forced_port_fallback_warning(url, fallback_url)
        result["bridge_url"] = fallback_url
        return result

    try:
        response = httpx.get(f"{url}/bridge", timeout=timeout)
        response.raise_for_status()
        return response.json()
    except (httpx.ConnectError, httpx.TimeoutException) as exc:
        try:
            if result := fallback_health_result():
                return result
        except Exception:
            pass
        return {"error": str(exc)}
    except httpx.HTTPStatusError as exc:
        try:
            if result := fallback_health_result():
                return result
        except Exception:
            pass
        return {"error": str(exc)}
    except Exception as exc:
        return {"error": str(exc)}
