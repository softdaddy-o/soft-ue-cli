"""Argparse introspection → MCP tool schema generation."""

from __future__ import annotations

import argparse
from typing import Any

EXCLUDED_COMMANDS: frozenset[str] = frozenset({
    "await-bridge",
    "skills",
    "mcp-serve",
})

# Commands executed client-side (no bridge call). Their existing cmd_* handlers
# are invoked directly by the MCP server instead of being forwarded to the bridge.
CLIENT_SIDE_COMMANDS: frozenset[str] = frozenset({
    "status",
    "wait-for-ready",
    "check-setup",
    "setup",
    "report-bug",
    "submit-testimonial",
    "request-feature",
    "inspect-uasset",
    "diff-uasset",
    "config",
    "add-co-node",
    "add-co-parameter",
    "add-co-mesh-option",
    "set-co-base-mesh",
    "add-co-group-child",
    "set-co-node-property",
    "connect-co-pins",
    "regenerate-co-node-pins",
    "compile-co",
    "remove-co-node",
})

# Per-tool schema overrides. Merged into auto-generated schemas after extraction.
#
# Supported override keys:
#   "properties": dict of property overrides merged into auto-generated properties.
#     Special types not in JSON Schema but handled by _build_signature:
#       "any"   — accepts any JSON value (no pydantic type constraint)
#       "array" — accepts a JSON array (maps to Python list)
#   "required_remove": list of field names to remove from the required list.
#     Use when a positional argparse arg should be optional in MCP.
TOOL_OVERRIDES: dict[str, dict[str, Any]] = {
    # spawn-actor: location/rotation are X,Y,Z arrays in MCP (not comma strings)
    "spawn-actor": {
        "properties": {
            "location": {"type": "array", "description": "[X, Y, Z] location in world space"},
            "rotation": {"type": "array", "description": "[Pitch, Yaw, Roll] rotation in degrees"},
        },
    },
    # set-console-var: value can be string, int, or float
    "set-console-var": {
        "properties": {
            "value": {"type": "any", "description": "New value (string, int, or float)"},
        },
    },
    # set-property: value can be any JSON scalar the bridge/tool can coerce
    "set-property": {
        "properties": {
            "value": {"type": "any", "description": "New value (string, number, boolean, array, or object)"},
        },
    },
    # batch-delete-actors: actors is a JSON array of name strings
    "batch-delete-actors": {
        "properties": {
            "actors": {"type": "array", "description": "Array of actor names or labels to delete"},
        },
    },
    # batch-spawn-actors: actors is a JSON array of actor spec objects
    "batch-spawn-actors": {
        "properties": {
            "actors": {"type": "array", "description": "Array of actor spawn specs"},
        },
    },
    # batch-modify-actors: modifications is a JSON array of modification objects
    "batch-modify-actors": {
        "properties": {
            "modifications": {"type": "array", "description": "Array of actor modification specs"},
        },
    },
    # add-graph-node: position is an [X, Y] array
    "add-graph-node": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] node position"},
        },
    },
    # CustomizableObject graph editing: position/properties are native JSON in MCP
    "add-co-node": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] node position"},
            "properties": {"type": "object", "description": "Reflected node properties"},
        },
    },
    "add-co-parameter": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] node position"},
            "properties": {"type": "object", "description": "Additional reflected node properties"},
        },
    },
    "add-co-mesh-option": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] node position"},
            "properties": {"type": "object", "description": "Additional reflected node properties"},
        },
    },
    "set-co-node-property": {
        "properties": {
            "properties": {"type": "object", "description": "Reflected node properties to set"},
        },
    },
    "create-co-from-spec": {
        "properties": {
            "spec": {"type": "object", "description": "CustomizableObject graph spec with nodes and edges arrays"},
        },
    },
    "wire-customizable-object-slot-from-table": {
        "properties": {
            "filter_values": {"type": "array", "description": "Array of DataTable filter values"},
            "node_position": {"type": "array", "description": "[X, Y] node position"},
        },
    },
    "add-datatable-row": {
        "properties": {
            "row_data": {"type": "object", "description": "Row data keyed by row struct property name"},
        },
    },
    # set-node-position: positions is a JSON array of {guid, x, y} objects
    "set-node-position": {
        "properties": {
            "positions": {"type": "array", "description": "Array of {guid, x, y} position specs"},
        },
    },
    # capture-screenshot: mode is optional (default: viewport)
    "capture-screenshot": {
        "properties": {
            "mode": {"type": "string", "default": "viewport"},
        },
        "required_remove": ["mode"],
    },
}


def _argparse_type_to_json(action: argparse.Action) -> dict[str, Any]:
    """Convert a single argparse action to a JSON Schema property."""
    prop: dict[str, Any] = {}

    # Type mapping
    if isinstance(action, (argparse._StoreTrueAction, argparse._StoreFalseAction)):
        prop["type"] = "boolean"
    elif action.type is int:
        prop["type"] = "integer"
    elif action.type is float:
        prop["type"] = "number"
    else:
        prop["type"] = "string"

    # Choices → enum
    if action.choices is not None:
        prop["enum"] = list(action.choices)

    # Description from help
    if action.help and action.help != argparse.SUPPRESS:
        prop["description"] = action.help

    # Default
    if action.default is not None and action.default != argparse.SUPPRESS:
        prop["default"] = action.default

    return prop


def _extract_one(parser: argparse.ArgumentParser) -> dict[str, Any]:
    """Extract a JSON Schema from a single subcommand parser."""
    properties: dict[str, Any] = {}
    required: list[str] = []
    nested_subparsers: argparse._SubParsersAction | None = None

    for action in parser._actions:
        # Skip help action and subparsers
        if isinstance(action, argparse._HelpAction):
            continue
        if isinstance(action, argparse._SubParsersAction):
            nested_subparsers = action
            continue

        # Determine the property name (dest)
        name = action.dest

        # Skip internal argparse fields
        if name in ("command", "func"):
            continue

        prop = _argparse_type_to_json(action)
        properties[name] = prop

        # Positional args are required (option_strings is empty)
        if not action.option_strings and action.required is not False:
            required.append(name)
        # Explicitly required optional args
        elif action.required:
            required.append(name)

    schema: dict[str, Any] = {
        "type": "object",
        "properties": properties,
    }
    if required:
        schema["required"] = required

    if nested_subparsers is not None:
        nested_props, nested_required = _extract_nested_subcommands(nested_subparsers)
        schema["properties"].update(nested_props)
        schema["required"] = sorted(set(schema.get("required", [])) | set(nested_required))

    return schema


def _extract_nested_subcommands(subparsers_action: argparse._SubParsersAction) -> tuple[dict[str, Any], list[str]]:
    """Flatten one level of nested subcommands into a single tool schema."""
    properties: dict[str, Any] = {
        "subcommand": {
            "type": "string",
            "enum": list(subparsers_action.choices.keys()),
            "description": "Nested subcommand to execute",
        },
    }

    for name, sub_parser in subparsers_action.choices.items():
        nested_schema = _extract_one(sub_parser)
        for prop_name, prop in nested_schema.get("properties", {}).items():
            properties.setdefault(prop_name, prop)

    return properties, ["subcommand"]


def extract_tools() -> list[dict[str, Any]]:
    """Extract MCP tool definitions from the CLI's argparse parser.

    Returns a list of dicts with keys: name, description, parameters.
    """
    from .__main__ import build_parser

    parser = build_parser()

    tools: list[dict[str, Any]] = []

    # Get the subparsers action
    subparsers_action = None
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            subparsers_action = action
            break

    if subparsers_action is None:
        return tools

    # Get per-choice help strings for better description fallback
    choice_help: dict[str, str] = {}
    for choice_action in subparsers_action._choices_actions:
        choice_help[choice_action.dest] = choice_action.help or ""

    for cmd_name, sub_parser in subparsers_action.choices.items():
        if cmd_name in EXCLUDED_COMMANDS:
            continue

        params = _extract_one(sub_parser)

        # Apply per-tool overrides
        if cmd_name in TOOL_OVERRIDES:
            override = TOOL_OVERRIDES[cmd_name]
            for prop_name, prop_override in override.get("properties", {}).items():
                if prop_name in params["properties"]:
                    params["properties"][prop_name].update(prop_override)
                else:
                    # Allow overrides to add new properties not in argparse
                    params["properties"][prop_name] = prop_override
            # Remove fields from required list when the override marks them optional
            if "required_remove" in override:
                required = params.get("required", [])
                params["required"] = [r for r in required if r not in override["required_remove"]]

        tools.append({
            "name": cmd_name,
            "description": sub_parser.description or choice_help.get(cmd_name, sub_parser.prog),
            "parameters": params,
            "func": sub_parser.get_default("func"),
        })

    return tools
