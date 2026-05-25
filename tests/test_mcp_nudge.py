"""Tests for MCP server nudge payloads."""

import json
from unittest.mock import patch, MagicMock

from soft_ue_cli.errors import BridgeError, ErrorKind


def test_mcp_tool_unexpected_error_includes_bug_hint():
    """MCP tool handler returns bug_report_hint for unexpected errors."""
    from soft_ue_cli.mcp_server import _make_tool_fn

    fn = _make_tool_fn("spawn-actor")
    with patch("soft_ue_cli.mcp_server._client") as mock_client:
        mock_client.call_tool.side_effect = BridgeError(
            kind=ErrorKind.UNEXPECTED,
            message="plugin crashed",
            tool_name="spawn-actor",
            arguments={},
        )
        result = json.loads(fn(class_name="Foo"))
    assert "bug_report_hint" in result
    assert result["bug_report_hint"]["suggested_command"] == "report-bug"


def test_mcp_tool_expected_error_no_bug_hint():
    """MCP tool handler does NOT include bug_report_hint for expected errors."""
    from soft_ue_cli.mcp_server import _make_tool_fn

    fn = _make_tool_fn("spawn-actor")
    with patch("soft_ue_cli.mcp_server._client") as mock_client:
        mock_client.call_tool.side_effect = BridgeError(
            kind=ErrorKind.EXPECTED,
            message="cannot connect",
            tool_name="spawn-actor",
            arguments={},
        )
        result = json.loads(fn(class_name="Foo"))
    assert "bug_report_hint" not in result


def test_mcp_tool_success_with_testimonial_nudge():
    """MCP tool handler appends testimonial_nudge when streak qualifies."""
    from soft_ue_cli.mcp_server import _make_tool_fn

    fn = _make_tool_fn("spawn-actor")
    with (
        patch("soft_ue_cli.mcp_server._client") as mock_client,
        patch("soft_ue_cli.mcp_server._streak") as mock_streak,
    ):
        mock_client.call_tool.return_value = {"spawned": True}
        mock_streak.record_success.return_value = None
        mock_streak.should_nudge_testimonial.return_value = True
        mock_streak.mark_nudged.return_value = None
        result = json.loads(fn(class_name="Foo"))
    assert "testimonial_nudge" in result
    mock_streak.mark_nudged.assert_called_once()


def test_mcp_tool_success_without_testimonial_nudge():
    """MCP tool handler does NOT append nudge when streak doesn't qualify."""
    from soft_ue_cli.mcp_server import _make_tool_fn

    fn = _make_tool_fn("spawn-actor")
    with (
        patch("soft_ue_cli.mcp_server._client") as mock_client,
        patch("soft_ue_cli.mcp_server._streak") as mock_streak,
    ):
        mock_client.call_tool.return_value = {"spawned": True}
        mock_streak.record_success.return_value = None
        mock_streak.should_nudge_testimonial.return_value = False
        result = json.loads(fn(class_name="Foo"))
    assert "testimonial_nudge" not in result
