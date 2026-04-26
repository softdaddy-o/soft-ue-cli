"""Tests for cli/soft_ue_cli/mcp_schema.py ??argparse to MCP tool schema conversion."""

from __future__ import annotations


import pytest


from soft_ue_cli.mcp_schema import extract_tools, EXCLUDED_COMMANDS


def test_extract_tools_returns_nonempty():
    tools = extract_tools()
    assert len(tools) > 0


def test_extract_tools_excludes_blocked_commands():
    tools = extract_tools()
    tool_names = {t["name"] for t in tools}
    for excluded in EXCLUDED_COMMANDS:
        assert excluded not in tool_names, f"{excluded} should be excluded"


def test_extract_tools_contains_known_command():
    tools = extract_tools()
    tool_names = {t["name"] for t in tools}
    assert "spawn-actor" in tool_names
    assert "query-blueprint" in tool_names
    assert "query-enum" in tool_names
    assert "query-struct" in tool_names
    assert "inspect-uasset" in tool_names
    assert "diff-uasset" in tool_names
    assert "status" in tool_names


def test_tool_has_required_fields():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "spawn-actor")
    assert "name" in tool
    assert "description" in tool
    assert "parameters" in tool


def test_positional_arg_is_required():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "spawn-actor")
    params = tool["parameters"]
    assert "actor_class" in params["properties"]
    assert "actor_class" in params.get("required", [])


def test_optional_arg_is_not_required():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "spawn-actor")
    params = tool["parameters"]
    assert "location" in params["properties"]
    assert "location" not in params.get("required", [])


def test_int_type_maps_to_integer():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "query-level")
    params = tool["parameters"]
    assert params["properties"]["limit"]["type"] == "integer"


def test_store_true_maps_to_boolean():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "query-blueprint")
    params = tool["parameters"]
    assert params["properties"]["no_detail"]["type"] == "boolean"


def test_set_property_value_override_maps_to_any():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "set-property")
    params = tool["parameters"]
    assert params["properties"]["value"]["type"] == "any"


def test_choices_map_to_enum():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "report-bug")
    params = tool["parameters"]
    severity = params["properties"]["severity"]
    assert "enum" in severity


def test_feedback_tools_include_privacy_guidance():
    tools = extract_tools()

    report_bug = next(t for t in tools if t["name"] == "report-bug")
    feature = next(t for t in tools if t["name"] == "request-feature")

    report_desc = report_bug["parameters"]["properties"]["description"]["description"]
    report_steps = report_bug["parameters"]["properties"]["steps"]["description"]
    feature_desc = feature["parameters"]["properties"]["description"]["description"]
    feature_use_case = feature["parameters"]["properties"]["use_case"]["description"]

    assert "project-specific information or personal information" in report_desc
    assert "generic placeholders" in report_steps
    assert "project-specific information or personal information" in feature_desc
    assert "generic placeholders" in feature_use_case


def test_help_text_becomes_description():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "spawn-actor")
    params = tool["parameters"]
    assert "description" in params["properties"]["actor_class"]


def test_tool_count_is_reasonable():
    """Should have a stable, non-trivial tool count after exclusions."""
    tools = extract_tools()
    assert len(tools) >= 60
    assert len(tools) <= 90


def test_skills_excluded():
    tools = extract_tools()
    tool_names = {t["name"] for t in tools}
    assert "skills" not in tool_names


def test_mcp_serve_excluded():
    tools = extract_tools()
    tool_names = {t["name"] for t in tools}
    assert "mcp-serve" not in tool_names


# -- CLI parser ----------------------------------------------------------------

from soft_ue_cli.__main__ import build_parser


def test_parser_mcp_serve():
    parser = build_parser()
    args = parser.parse_args(["mcp-serve"])
    assert args.command == "mcp-serve"
