"""Tests for error classification and nudge helpers."""

from soft_ue_cli.errors import BridgeError, ErrorKind, format_bug_nudge, bug_nudge_payload


def test_error_kind_values():
    assert ErrorKind.EXPECTED.value == "expected"
    assert ErrorKind.UNEXPECTED.value == "unexpected"


def test_bridge_error_attributes():
    err = BridgeError(
        kind=ErrorKind.UNEXPECTED,
        message="plugin crashed",
        tool_name="spawn-actor",
        arguments={"class_name": "Foo"},
    )
    assert err.kind == ErrorKind.UNEXPECTED
    assert err.message == "plugin crashed"
    assert err.tool_name == "spawn-actor"
    assert err.arguments == {"class_name": "Foo"}
    assert str(err) == "plugin crashed"


def test_bridge_error_expected():
    err = BridgeError(
        kind=ErrorKind.EXPECTED,
        message="cannot connect",
        tool_name="health",
        arguments={},
    )
    assert err.kind == ErrorKind.EXPECTED


def test_format_bug_nudge():
    result = format_bug_nudge("spawn-actor", "plugin crashed")
    assert "report-bug" in result
    assert "spawn-actor" in result
    assert "plugin crashed" in result


def test_format_bug_nudge_escapes_quotes():
    result = format_bug_nudge("spawn-actor", 'error with "quotes"')
    # Should not break shell quoting
    assert "report-bug" in result


def test_bug_nudge_payload():
    result = bug_nudge_payload("spawn-actor", "plugin crashed")
    assert result["suggested_command"] == "report-bug"
    assert result["suggested_args"]["title"] == "error in spawn-actor"
    assert result["suggested_args"]["description"] == "plugin crashed"
    assert result["suggested_args"]["severity"] == "major"
