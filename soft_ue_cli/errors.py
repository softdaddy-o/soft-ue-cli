"""Error classification and bug-report nudge helpers for soft-ue-cli."""

from __future__ import annotations

import enum


class ErrorKind(enum.Enum):
    """Whether an error is expected (operational) or unexpected (likely a bug)."""

    EXPECTED = "expected"
    UNEXPECTED = "unexpected"


class BridgeError(Exception):
    """Raised by call_tool() on any failure, carrying classification metadata."""

    def __init__(
        self,
        kind: ErrorKind,
        message: str,
        tool_name: str,
        arguments: dict,
    ) -> None:
        super().__init__(message)
        self.kind = kind
        self.message = message
        self.tool_name = tool_name
        self.arguments = arguments


def format_bug_nudge(tool_name: str, error_message: str) -> str:
    """Return a CLI-formatted hint suggesting the user file a bug report."""
    safe_title = f"error in {tool_name}"
    safe_desc = error_message.replace('"', '\\"')
    return (
        "\n  Looks like a bug? Run:\n"
        f'    soft-ue-cli report-bug --title "{safe_title}" '
        f'--description "{safe_desc}"'
    )


def bug_nudge_payload(tool_name: str, error_message: str) -> dict:
    """Return a structured dict for MCP error responses."""
    return {
        "suggested_command": "report-bug",
        "suggested_args": {
            "title": f"error in {tool_name}",
            "description": error_message,
            "severity": "major",
        },
    }
