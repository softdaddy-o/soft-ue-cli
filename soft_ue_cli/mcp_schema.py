"""Argparse introspection → MCP tool schema generation."""

from __future__ import annotations

import argparse
import copy
from typing import Any

from .command_aliases import REMOVED_COMMAND_MIGRATIONS


def _canonical_name(canonical_command: str) -> str:
    return canonical_command.split(" --", 1)[0]


def _mcp_tool_name_from_removed(canonical_command: str) -> str:
    """Convert a canonical removed command string to its MCP tool name."""
    parts: list[str] = []
    for part in canonical_command.split():
        if part.startswith("--") or (part.startswith("<") and part.endswith(">")):
            break
        parts.append(part)
    return " ".join(parts)


def _build_alias_tool_map() -> dict[str, str]:
    aliases: dict[str, str] = {}
    for alias_name, canonical_command in REMOVED_COMMAND_MIGRATIONS.items():
        canonical_tool = _mcp_tool_name_from_removed(canonical_command)
        if canonical_tool and canonical_tool != alias_name:
            aliases[alias_name] = canonical_tool
    return aliases


MCP_TOOL_ALIASES: dict[str, str] = _build_alias_tool_map()
MCP_ALIAS_OVERRIDES: dict[str, dict[str, Any]] = {
    "capture-pie-screenshot": {
        "properties": {
            "mode": {"type": "string", "default": "pie-window", "enum": ["window", "tab", "region", "viewport", "pie-window"]},
        },
        "required_remove": ["mode"],
    },
}


EXCLUDED_COMMANDS: frozenset[str] = frozenset({
    "anim",
    "await-bridge",
    "asset",
    "blueprint",
    "capture",
    "umg",
    "metasound",
    "mutable",
    "skills",
    "mcp-serve",
    "statetree",
})

# Commands executed client-side (no bridge call). Their existing cmd_* handlers
# are invoked directly by the MCP server instead of being forwarded to the bridge.
CLIENT_SIDE_COMMANDS: frozenset[str] = frozenset({
    "status",
    "commands",
    "wait-for-ready",
    "check-setup",
    "setup",
    "report-bug",
    "submit-testimonial",
    "request-feature",
    "config",
} | {_canonical_name(command) for command in REMOVED_COMMAND_MIGRATIONS.values()} | set(REMOVED_COMMAND_MIGRATIONS))

# Per-tool schema overrides. Merged into auto-generated schemas after extraction.
#
# Supported override keys:
#   "properties": dict of property overrides merged into auto-generated properties.
#     Special types not in JSON Schema but handled by _build_signature:
#       "any"   — accepts any JSON value (no pydantic type constraint)
#       "array" — accepts a JSON array (maps to Python list)
#   "required_remove": list of field names to remove from the required list.
#     Use when a positional argparse arg should be optional in MCP.
#   "required_add": list of field names to require in MCP.
#   "properties_remove": list of argparse-only fields to hide from MCP.
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
    # batch-call: calls is a native JSON array in MCP, not a CLI JSON string.
    "batch-call": {
        "properties": {
            "calls": {"type": "array", "description": "Array of {tool, args} entries"},
        },
        "properties_remove": ["calls_file"],
        "required_add": ["calls"],
    },
    # exec-console-command: MCP callers may pass a complete command string while
    # argparse exposes the legacy command_parts positional.
    "exec-console-command": {
        "properties": {
            "command": {"type": "string", "description": "Console command to execute"},
        },
        "properties_remove": ["command_parts"],
        "required_remove": ["command_parts"],
    },
    # call-function: legacy positional CLI args stay supported by argparse but
    # MCP should expose the explicit bridge argument names only.
    "call-function": {
        "properties": {
            "function_name": {"type": "string", "description": "Function or event name to call"},
            "args": {"type": "object", "description": "Named function arguments object. Mutually exclusive with batch."},
            "batch": {"type": "array", "description": "Array of named function argument objects. Mutually exclusive with args."},
        },
        "properties_remove": ["legacy_actor_name", "legacy_function_name", "batch_json", "output"],
        "required_remove": ["legacy_actor_name", "legacy_function_name"],
        "required_add": ["function_name"],
    },
    # add-graph-node: position is an [X, Y] array
    "add-graph-node": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] node position"},
        },
    },
    "add-anim-state-machine": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] state machine node position"},
        },
    },
    "add-anim-state": {
        "properties": {
            "position": {"type": "array", "description": "[X, Y] state node position"},
        },
    },
    "add-anim-transition": {
        "properties": {
            "rule": {"type": "boolean", "description": "Optional literal transition rule default"},
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
    "set-co-layout-blocks": {
        "properties": {
            "grid_size": {"type": "array", "description": "[X, Y] layout grid size"},
            "max_grid_size": {"type": "array", "description": "Optional [X, Y] max layout grid size"},
            "blocks": {"type": "array", "description": "Layout blocks with min plus max or size"},
            "lod_index": {"type": "integer", "description": "Source mesh LOD index for mesh pin UV layouts"},
            "section_index": {"type": "integer", "description": "Source mesh section/material index for mesh pin UV layouts"},
            "uv_channel": {"type": "integer", "description": "Source mesh UV channel for mesh pin layouts"},
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
    "apply-widget-tree": {
        "properties": {
            "spec": {"type": "object", "description": "Widget tree spec object"},
        },
    },
    "wire-widget-navigation": {
        "properties": {
            "bindings": {"type": "array", "description": "Array of UMG navigation binding specs"},
            "allow_pie": {"type": "boolean", "description": "Allow mutation while PIE is active; default is fail-fast"},
            "allow_busy": {
                "type": "boolean",
                "description": "Allow mutation while the editor is saving or garbage collecting; default is fail-fast",
            },
        },
    },
    "verify-umg-workflow": {
        "properties": {
            "expected_widgets": {"type": "array", "description": "Array of expected widget names"},
            "expected_text": {"type": "array", "description": "Array of expected TextBlock strings"},
            "click_sequence": {"type": "array", "description": "Array of named button click validation steps"},
            "preview_lifecycle": {
                "type": "string",
                "enum": ["replace", "keep", "remove"],
                "description": "Tool-owned preview lifecycle policy before verification",
            },
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
    "compare-umg-screenshot": {
        "properties": {
            "crop": {"type": "array", "description": "[X, Y, W, H] captured-image crop rectangle"},
        },
    },
    "umg-layout": {
        "properties": {
            "ignore_mask": {"type": "array", "description": "Ignore mask names, JSON objects, or JSON file paths"},
        },
    },
    "compare-sync-markers": {
        "properties": {
            "asset_paths": {"type": "array", "description": "Array of AnimSequence asset paths to compare"},
        },
    },
    "anim-repoint-references": {
        "properties": {
            "asset_paths": {"type": "array", "description": "Array of AnimMontage or BlendSpace asset paths to update"},
            "replacement_map": {"type": "object", "description": "Object mapping old AnimSequence asset paths to new AnimSequence asset paths"},
        },
        "properties_remove": ["replacements"],
        "required_remove": ["replacements"],
        "required_add": ["replacement_map"],
    },
    "anim-retarget-blueprint": {
        "properties": {
            "bone_map": {"type": "object", "description": "Object mapping old bone names to new bone names"},
            "animation_asset_map": {"type": "object", "description": "Object mapping old animation asset paths to new animation asset paths"},
        },
        "properties_remove": ["bone_map_entries", "animation_map_entries"],
        "required_remove": ["bone_map_entries"],
        "required_add": ["bone_map"],
    },
    "pose-search-schema-remap": {
        "properties": {
            "bone_map": {"type": "object", "description": "Object mapping old bone names to new bone names"},
        },
        "properties_remove": ["bone_map_entries"],
        "required_remove": ["bone_map_entries"],
        "required_add": ["bone_map"],
    },
    "pose-search-database-repoint": {
        "properties": {
            "animation_asset_map": {"type": "object", "description": "Object mapping old animation asset paths to new animation asset paths"},
        },
        "properties_remove": ["animation_map_entries"],
    },
    "asset-repoint-references": {
        "properties": {
            "asset_paths": {"type": "array", "description": "Array of asset paths to update"},
            "replacement_map": {"type": "object", "description": "Object mapping old asset paths to new asset paths"},
        },
        "properties_remove": ["replacements"],
        "required_remove": ["replacements"],
        "required_add": ["replacement_map"],
    },
}

EXTRA_BRIDGE_TOOLS: tuple[dict[str, Any], ...] = (
    {
        "name": "validate-config-key",
        "description": "Validate whether an Unreal config section/key is known and usable.",
        "parameters": {
            "type": "object",
            "properties": {
                "section": {"type": "string", "description": "Config section name"},
                "key": {"type": "string", "description": "Config key name"},
                "config_type": {"type": "string", "description": "Config type, e.g. Engine, Game, Input"},
                "platform": {"type": "string", "description": "Optional config platform"},
            },
            "required": ["section", "key", "config_type"],
        },
    },
    {
        "name": "set-config-value",
        "description": "Set an Unreal config value through the active bridge.",
        "parameters": {
            "type": "object",
            "properties": {
                "section": {"type": "string", "description": "Config section name"},
                "key": {"type": "string", "description": "Config key name"},
                "value": {"type": "string", "description": "Config value to write"},
                "config_type": {"type": "string", "description": "Config type, e.g. Engine, Game, Input"},
                "platform": {"type": "string", "description": "Optional config platform"},
            },
            "required": ["section", "key", "value", "config_type"],
        },
    },
    {
        "name": "get-config-value",
        "description": "Read an Unreal config value through the active bridge.",
        "parameters": {
            "type": "object",
            "properties": {
                "section": {"type": "string", "description": "Config section name"},
                "key": {"type": "string", "description": "Config key name"},
                "config_type": {"type": "string", "description": "Config type, e.g. Engine, Game, Input"},
                "platform": {"type": "string", "description": "Optional config platform"},
            },
            "required": ["section", "key", "config_type"],
        },
    },
)

TOOL_OVERRIDES.update({
    "blueprint node add": TOOL_OVERRIDES["add-graph-node"],
    "blueprint node position": TOOL_OVERRIDES["set-node-position"],
    "anim state-machine add": TOOL_OVERRIDES["add-anim-state-machine"],
    "anim state add": TOOL_OVERRIDES["add-anim-state"],
    "anim transition add": TOOL_OVERRIDES["add-anim-transition"],
    "mutable graph add-node": TOOL_OVERRIDES["add-co-node"],
    "mutable graph add-parameter": TOOL_OVERRIDES["add-co-parameter"],
    "mutable graph add-mesh-option": TOOL_OVERRIDES["add-co-mesh-option"],
    "mutable graph set-node-property": TOOL_OVERRIDES["set-co-node-property"],
    "mutable graph set-layout-blocks": TOOL_OVERRIDES["set-co-layout-blocks"],
    "mutable graph create-from-spec": TOOL_OVERRIDES["create-co-from-spec"],
    "mutable graph wire-slot-from-table": TOOL_OVERRIDES["wire-customizable-object-slot-from-table"],
    "umg designer apply": TOOL_OVERRIDES["apply-widget-tree"],
    "umg navigation wire": TOOL_OVERRIDES["wire-widget-navigation"],
    "umg navigation verify": TOOL_OVERRIDES["wire-widget-navigation"],
    "umg verify widgets": {
        "properties": {
            "expected_widgets": {"type": "array", "description": "Array of expected widget names"},
        },
    },
    "umg verify text": {
        "properties": {
            "expected_text": {"type": "array", "description": "Array of expected TextBlock strings"},
        },
    },
    "umg verify navigation": {
        "properties": {
            "click_sequence": {"type": "array", "description": "Array of named button click validation steps"},
        },
    },
    "umg preview create": {
        "properties": {
            "viewport_anchors": {"type": "array", "description": "[MinX, MinY, MaxX, MaxY] viewport anchors"},
            "viewport_position": {"type": "array", "description": "[X, Y] viewport position"},
            "viewport_size": {"type": "array", "description": "[W, H] desired viewport size"},
            "viewport_alignment": {"type": "array", "description": "[X, Y] viewport alignment"},
        },
    },
    "umg preview replace": {
        "properties": {
            "viewport_anchors": {"type": "array", "description": "[MinX, MinY, MaxX, MaxY] viewport anchors"},
            "viewport_position": {"type": "array", "description": "[X, Y] viewport position"},
            "viewport_size": {"type": "array", "description": "[W, H] desired viewport size"},
            "viewport_alignment": {"type": "array", "description": "[X, Y] viewport alignment"},
        },
    },
    "capture screenshot": TOOL_OVERRIDES["capture-screenshot"],
    "umg layout compare": {
        "properties": {
            **TOOL_OVERRIDES["compare-umg-screenshot"]["properties"],
            **TOOL_OVERRIDES["umg-layout"]["properties"],
        },
    },
    "umg verify runtime-layout": {
        "properties": {
            **TOOL_OVERRIDES["compare-umg-screenshot"]["properties"],
            **TOOL_OVERRIDES["umg-layout"]["properties"],
        },
    },
    "anim sync-marker compare": TOOL_OVERRIDES["compare-sync-markers"],
    "anim retarget repoint-references": TOOL_OVERRIDES["anim-repoint-references"],
    "anim retarget sequence": TOOL_OVERRIDES.get("anim-retarget-sequence", {}),
    "anim retarget blueprint": TOOL_OVERRIDES["anim-retarget-blueprint"],
    "anim montage inspect": TOOL_OVERRIDES.get("anim-montage-inspect", {}),
    "anim pose-search remap": TOOL_OVERRIDES["pose-search-schema-remap"],
    "anim pose-search database-repoint": TOOL_OVERRIDES["pose-search-database-repoint"],
    "asset repoint-references": TOOL_OVERRIDES["asset-repoint-references"],
})


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


def _first_subparsers_action(parser: argparse.ArgumentParser) -> argparse._SubParsersAction | None:
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action
    return None


def _choice_help(subparsers_action: argparse._SubParsersAction, name: str) -> str:
    for choice_action in subparsers_action._choices_actions:
        if choice_action.dest == name:
            return choice_action.help or ""
    return ""


def _iter_nested_leaf_commands(
    parser: argparse.ArgumentParser,
    path: tuple[str, ...],
) -> list[tuple[str, argparse.ArgumentParser, str]]:
    subparsers_action = _first_subparsers_action(parser)
    if subparsers_action is None:
        return [(" ".join(path), parser, parser.description or parser.prog)]

    leaves: list[tuple[str, argparse.ArgumentParser, str]] = []
    for choice, sub_parser in subparsers_action.choices.items():
        leaves.extend(_iter_nested_leaf_commands(sub_parser, (*path, choice)))
    return leaves


def _apply_tool_overrides(tool_name: str, params: dict[str, Any]) -> None:
    override = {
        **TOOL_OVERRIDES.get(tool_name, {}),
        **MCP_ALIAS_OVERRIDES.get(tool_name, {}),
    }
    if not override:
        return
    for prop_name in override.get("properties_remove", []):
        params["properties"].pop(prop_name, None)
    for prop_name, prop_override in override.get("properties", {}).items():
        if prop_name in params["properties"]:
            params["properties"][prop_name].update(prop_override)
        else:
            # Allow overrides to add new properties not in argparse
            params["properties"][prop_name] = prop_override
    if "required_remove" in override:
        required = params.get("required", [])
        params["required"] = [r for r in required if r not in override["required_remove"]]
    if "required_add" in override:
        required = list(params.get("required", []))
        for prop_name in override["required_add"]:
            if prop_name in params["properties"] and prop_name not in required:
                required.append(prop_name)
        params["required"] = required


def _tool_def(name: str, sub_parser: argparse.ArgumentParser, description: str = "") -> dict[str, Any]:
    params = _extract_one(sub_parser)
    _apply_tool_overrides(name, params)
    return {
        "name": name,
        "description": sub_parser.description or description or sub_parser.prog,
        "parameters": params,
        "func": sub_parser.get_default("func"),
        "defaults": dict(sub_parser._defaults),
    }


def extract_tools() -> list[dict[str, Any]]:
    """Extract MCP tool definitions from the CLI's argparse parser.

    Returns a list of dicts with keys: name, description, parameters.
    """
    from .__main__ import build_parser

    parser = build_parser(include_removed=True)

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
            if _first_subparsers_action(sub_parser) is not None:
                for leaf_name, leaf_parser, leaf_description in _iter_nested_leaf_commands(sub_parser, (cmd_name,)):
                    tools.append(_tool_def(leaf_name, leaf_parser, leaf_description))
            continue

        tools.append(_tool_def(cmd_name, sub_parser, choice_help.get(cmd_name, "")))

    existing_names = {tool["name"] for tool in tools}
    canonical_tools_by_name = {tool["name"]: tool for tool in tools}
    for tool_def in EXTRA_BRIDGE_TOOLS:
        if tool_def["name"] not in existing_names:
            tools.append({**tool_def, "func": None})
            existing_names.add(tool_def["name"])

    for alias_name, canonical_name in sorted(MCP_TOOL_ALIASES.items()):
        if alias_name in existing_names:
            continue
        canonical_tool = canonical_tools_by_name.get(canonical_name)
        if not canonical_tool:
            continue
        alias_tool = copy.deepcopy(canonical_tool)
        alias_tool["name"] = alias_name
        _apply_tool_overrides(alias_name, alias_tool["parameters"])
        tools.append(alias_tool)
        existing_names.add(alias_name)

    return tools
