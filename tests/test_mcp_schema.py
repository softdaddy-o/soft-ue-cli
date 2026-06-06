"""Tests for cli/soft_ue_cli/mcp_schema.py ??argparse to MCP tool schema conversion."""

from __future__ import annotations


import pytest


from soft_ue_cli.mcp_schema import CLIENT_SIDE_COMMANDS, EXCLUDED_COMMANDS, extract_tools


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
    assert "blueprint inspect" in tool_names
    assert "blueprint graph inspect" in tool_names
    assert "query-enum" in tool_names
    assert "query-struct" in tool_names
    assert "metasound inspect" in tool_names
    assert "mutable inspect graph" in tool_names
    assert "mutable inspect parameters" in tool_names
    assert "mutable inspect diagnostics" in tool_names
    assert "mutable graph add-node" in tool_names
    assert "mutable graph add-parameter" in tool_names
    assert "mutable graph add-mesh-option" in tool_names
    assert "mutable graph set-node-property" in tool_names
    assert "mutable graph connect-pins" in tool_names
    assert "mutable graph regenerate-node-pins" in tool_names
    assert "mutable compile" in tool_names
    assert "mutable graph remove-node" in tool_names
    assert "mutable graph wire-slot-from-table" in tool_names
    assert "reload-bridge-module" in tool_names
    assert "asset inspect-file" in tool_names
    assert "asset diff-file" in tool_names
    assert "umg designer apply" in tool_names
    assert "umg navigation wire" in tool_names
    assert "umg workflow run" in tool_names
    assert "capture screenshot" in tool_names
    assert "umg layout extract" in tool_names
    assert "umg layout compare" in tool_names
    assert "umg layout fit" in tool_names
    assert "umg runtime inspect" in tool_names
    assert "status" in tool_names
    assert "commands" in tool_names
    assert "wait-for-ready" in tool_names
    assert "anim sync-marker inspect" in tool_names
    assert "anim sync-marker compare" in tool_names
    assert "anim sync-marker add" in tool_names
    assert "anim sync-marker remove" in tool_names
    assert "query-blueprint" in tool_names
    assert "capture-viewport" in tool_names


def test_extract_tools_exposes_removed_flat_aliases():
    tool_names = {t["name"] for t in extract_tools()}

    for alias in (
        "capture-screenshot",
        "capture-pie-screenshot",
        "create-asset",
        "apply-widget-tree",
        "verify-umg-workflow",
        "inspect-runtime-widgets",
    ):
        assert alias in tool_names


def test_capture_screenshot_schema_default_mode_is_viewport():
    tools = {t["name"]: t for t in extract_tools()}
    tool = tools["capture-screenshot"]
    assert tool["parameters"]["properties"]["mode"]["type"] == "string"
    assert tool["parameters"]["properties"]["mode"]["default"] == "viewport"
    assert "mode" not in tool["parameters"].get("required", [])


def test_nested_metasound_family_root_is_not_auto_exposed_to_mcp():
    assert "metasound" in EXCLUDED_COMMANDS


def test_metasound_inspect_leaf_is_exposed_to_mcp():
    tool = next(t for t in extract_tools() if t["name"] == "metasound inspect")
    assert "asset_path" in tool["parameters"]["properties"]
    assert "asset_path" in tool["parameters"].get("required", [])


def test_capture_pie_screenshot_schema_defaults_to_pie_window():
    tools = {t["name"]: t for t in extract_tools()}
    tool = tools["capture-pie-screenshot"]
    assert tool["parameters"]["properties"]["mode"]["type"] == "string"
    assert tool["parameters"]["properties"]["mode"]["default"] == "pie-window"
    assert "mode" not in tool["parameters"].get("required", [])
    assert tool["parameters"]["properties"]["mode"]["enum"] == [
        "window",
        "tab",
        "region",
        "viewport",
        "pie-window",
    ]


def test_pie_tick_schema_exposes_timeout_parameter():
    tools = {t["name"]: t for t in extract_tools()}
    tool = tools["pie-tick"]
    assert tool["parameters"]["properties"]["timeout"]["type"] == "number"
    assert "timeout" not in tool["parameters"].get("required", [])


def test_call_function_mcp_schema_matches_bridge_contract():
    tools = {t["name"]: t for t in extract_tools()}
    params = tools["call-function"]["parameters"]

    assert params["properties"]["function_name"]["type"] == "string"
    assert params["properties"]["args"]["type"] == "object"
    assert params["properties"]["batch"]["type"] == "array"
    assert "function_name" in params.get("required", [])
    assert "batch_json" not in params["properties"]
    assert "output" not in params["properties"]


def test_config_bridge_schemas_match_string_only_bridge_contract():
    tools = {t["name"]: t for t in extract_tools()}

    for name in ("validate-config-key", "set-config-value", "get-config-value"):
        params = tools[name]["parameters"]
        assert "config_type" in params.get("required", [])

    assert tools["set-config-value"]["parameters"]["properties"]["value"]["type"] == "string"


def test_tool_has_required_fields():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "spawn-actor")
    assert "name" in tool
    assert "description" in tool
    assert "parameters" in tool


def test_commands_is_client_side_tool():
    assert "commands" in CLIENT_SIDE_COMMANDS


def test_nested_umg_family_root_is_not_auto_exposed_to_mcp():
    assert "umg" in EXCLUDED_COMMANDS


def test_nested_capture_family_root_is_not_auto_exposed_to_mcp():
    assert "capture" in EXCLUDED_COMMANDS


def test_nested_taxonomy_family_roots_are_not_auto_exposed_to_mcp():
    for family in ["mutable", "statetree", "anim", "asset", "blueprint"]:
        assert family in EXCLUDED_COMMANDS


def test_canonical_leaf_commands_are_exposed_to_mcp():
    tool_names = {t["name"] for t in extract_tools()}

    for name in [
        "umg designer apply",
        "capture viewport",
        "mutable graph add-node",
        "statetree state add",
        "anim rewind status",
        "asset query",
        "blueprint node add",
    ]:
        assert name in tool_names


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


def test_world_options_are_exposed_for_level_actor_property_queries():
    tools = extract_tools()
    query_level = next(t for t in tools if t["name"] == "query-level")
    get_property = next(t for t in tools if t["name"] == "get-property")

    assert query_level["parameters"]["properties"]["world"]["enum"] == ["editor", "pie", "game"]
    assert get_property["parameters"]["properties"]["world"]["enum"] == ["editor", "pie", "game"]


def test_store_true_maps_to_boolean():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "blueprint inspect")
    params = tool["parameters"]
    assert params["properties"]["no_detail"]["type"] == "boolean"


def test_set_property_value_override_maps_to_any():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "set-property")
    params = tool["parameters"]
    assert params["properties"]["value"]["type"] == "any"


def test_customizable_object_edit_schema_uses_native_json_types():
    tools = extract_tools()

    add_node = next(t for t in tools if t["name"] == "mutable graph add-node")
    add_node_params = add_node["parameters"]["properties"]
    assert add_node_params["position"]["type"] == "array"
    assert add_node_params["properties"]["type"] == "object"

    set_node_property = next(t for t in tools if t["name"] == "mutable graph set-node-property")
    assert set_node_property["parameters"]["properties"]["properties"]["type"] == "object"

    add_datatable_row = next(t for t in tools if t["name"] == "add-datatable-row")
    assert add_datatable_row["parameters"]["properties"]["row_data"]["type"] == "object"

    connect_pins = next(t for t in tools if t["name"] == "mutable graph connect-pins")
    assert connect_pins["parameters"]["properties"]["auto_regenerate"]["type"] == "boolean"

    slot_macro = next(t for t in tools if t["name"] == "mutable graph wire-slot-from-table")
    slot_params = slot_macro["parameters"]["properties"]
    assert slot_params["filter_values"]["type"] == "array"
    assert slot_params["node_position"]["type"] == "array"


def test_apply_widget_tree_schema_uses_native_json_types():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "umg designer apply")
    params = tool["parameters"]

    assert params["properties"]["spec"]["type"] == "object"
    assert params["properties"]["spec_file"]["type"] == "string"
    assert params["properties"]["append"]["type"] == "boolean"
    assert params["properties"]["compile"]["type"] == "boolean"
    assert params["properties"]["save"]["type"] == "boolean"
    assert params["properties"]["checkout"]["type"] == "boolean"
    assert "asset_path" in params.get("required", [])


def test_umg_workflow_schema_uses_native_json_types():
    tools = extract_tools()

    wire = next(t for t in tools if t["name"] == "umg navigation wire")
    wire_params = wire["parameters"]
    assert wire_params["properties"]["bindings"]["type"] == "array"
    assert wire_params["properties"]["bindings_file"]["type"] == "string"
    assert wire_params["properties"]["compile"]["type"] == "boolean"
    assert wire_params["properties"]["save"]["type"] == "boolean"
    assert wire_params["properties"]["allow_pie"]["type"] == "boolean"
    assert wire_params["properties"]["allow_busy"]["type"] == "boolean"
    assert "asset_path" in wire_params.get("required", [])

    verify_widgets = next(t for t in tools if t["name"] == "umg verify widgets")
    verify_text = next(t for t in tools if t["name"] == "umg verify text")
    verify_navigation = next(t for t in tools if t["name"] == "umg verify navigation")
    workflow = next(t for t in tools if t["name"] == "umg workflow run")
    iterate = next(t for t in tools if t["name"] == "umg workflow iterate-layout")
    assert verify_widgets["parameters"]["properties"]["expected_widgets"]["type"] == "array"
    assert verify_text["parameters"]["properties"]["expected_text"]["type"] == "array"
    assert verify_navigation["parameters"]["properties"]["click_sequence"]["type"] == "array"
    assert workflow["parameters"]["properties"]["plan"]["type"] == "string"
    assert "plan" in workflow["parameters"].get("required", [])
    assert iterate["parameters"]["properties"]["apply"]["type"] == "boolean"
    assert iterate["parameters"]["properties"]["compile"]["type"] == "boolean"
    assert iterate["parameters"]["properties"]["save"]["type"] == "boolean"
    assert iterate["parameters"]["properties"]["capture"]["type"] == "boolean"
    assert iterate["parameters"]["properties"]["max_iterations"]["type"] == "integer"
    assert "concept_layout" in iterate["parameters"].get("required", [])


def test_visual_capture_schema_exposes_safe_pie_and_compare_options():
    tools = extract_tools()

    capture = next(t for t in tools if t["name"] == "capture screenshot")
    capture_props = capture["parameters"]["properties"]
    assert "pie-window" in capture_props["mode"]["enum"]
    assert capture_props["unsafe_slate_window_capture"]["type"] == "boolean"
    assert capture_props["output_file"]["type"] == "string"

    pie_capture = next(t for t in tools if t["name"] == "capture viewport")
    pie_capture_props = pie_capture["parameters"]["properties"]
    assert pie_capture_props["format"]["type"] == "string"
    assert pie_capture_props["cleanup_previous"]["type"] == "boolean"

    compare = next(t for t in tools if t["name"] == "umg layout compare")
    compare_params = compare["parameters"]
    assert compare_params["properties"]["crop"]["type"] == "array"
    assert compare_params["properties"]["annotated_output"]["type"] == "string"
    assert compare_params["properties"]["threshold"]["type"] == "number"
    assert "expected_layout" in compare_params.get("required", [])
    assert "actual_layout" in compare_params.get("required", [])

    extract = next(t for t in tools if t["name"] == "umg layout extract")
    assert extract["parameters"]["properties"]["preview_handle"]["type"] == "string"

    runtime = next(t for t in tools if t["name"] == "umg runtime inspect")
    runtime_props = runtime["parameters"]["properties"]
    assert runtime_props["root_widget"]["type"] == "string"
    assert runtime_props["include_slate"]["type"] == "boolean"

    preview = next(t for t in tools if t["name"] == "umg preview replace")
    preview_props = preview["parameters"]["properties"]
    assert preview_props["fullscreen"]["type"] == "boolean"
    assert preview_props["viewport_anchors"]["type"] == "array"
    assert preview_props["viewport_position"]["type"] == "array"
    assert preview_props["viewport_size"]["type"] == "array"
    assert preview_props["viewport_alignment"]["type"] == "array"


def test_animation_graph_and_sync_marker_schema_uses_native_json_types():
    tools = extract_tools()

    query_graph = next(t for t in tools if t["name"] == "blueprint graph inspect")
    query_props = query_graph["parameters"]["properties"]
    assert query_props["recursive"]["type"] == "boolean"
    assert query_props["node_class"]["type"] == "string"

    inspect_anim = next(t for t in tools if t["name"] == "anim instance inspect")
    inspect_params = inspect_anim["parameters"]
    assert "actor_tag" not in inspect_params.get("required", [])
    assert inspect_params["properties"]["asset_path"]["type"] == "string"

    compare_markers = next(t for t in tools if t["name"] == "anim sync-marker compare")
    assert compare_markers["parameters"]["properties"]["asset_paths"]["type"] == "array"

    add_marker = next(t for t in tools if t["name"] == "anim sync-marker add")
    assert add_marker["parameters"]["properties"]["time"]["type"] == "number"

    repoint = next(t for t in tools if t["name"] == "anim retarget repoint-references")
    repoint_params = repoint["parameters"]
    assert repoint_params["properties"]["asset_paths"]["type"] == "array"
    assert repoint_params["properties"]["replacement_map"]["type"] == "object"
    assert "asset_paths" in repoint_params.get("required", [])
    assert "replacement_map" in repoint_params.get("required", [])

    retarget_blueprint = next(t for t in tools if t["name"] == "anim retarget blueprint")
    retarget_blueprint_params = retarget_blueprint["parameters"]
    assert retarget_blueprint_params["properties"]["bone_map"]["type"] == "object"
    assert retarget_blueprint_params["properties"]["animation_asset_map"]["type"] == "object"
    assert "bone_map" in retarget_blueprint_params.get("required", [])
    assert "bone_map_entries" not in retarget_blueprint_params["properties"]
    assert "animation_map_entries" not in retarget_blueprint_params["properties"]

    pose_remap = next(t for t in tools if t["name"] == "anim pose-search remap")
    pose_remap_params = pose_remap["parameters"]
    assert pose_remap_params["properties"]["bone_map"]["type"] == "object"
    assert "bone_map" in pose_remap_params.get("required", [])
    assert "bone_map_entries" not in pose_remap_params["properties"]

    pose_inspect = next(t for t in tools if t["name"] == "anim pose-search inspect")
    assert pose_inspect["parameters"]["properties"]["schema_path"]["type"] == "string"

    pose_database = next(t for t in tools if t["name"] == "anim pose-search database-repoint")
    pose_database_params = pose_database["parameters"]
    assert pose_database_params["properties"]["animation_asset_map"]["type"] == "object"
    assert pose_database_params["properties"]["schema_path"]["type"] == "string"
    assert "animation_map_entries" not in pose_database_params["properties"]

    asset_repoint = next(t for t in tools if t["name"] == "asset repoint-references")
    asset_repoint_params = asset_repoint["parameters"]
    assert asset_repoint_params["properties"]["asset_paths"]["type"] == "array"
    assert asset_repoint_params["properties"]["replacement_map"]["type"] == "object"
    assert "asset_paths" in asset_repoint_params.get("required", [])
    assert "replacement_map" in asset_repoint_params.get("required", [])


def test_pie_session_schema_exposes_blueprint_compile_error_policy():
    tools = extract_tools()
    tool = next(t for t in tools if t["name"] == "pie-session")
    params = tool["parameters"]

    action = params["properties"]["blueprint_error_action"]
    assert action["enum"] == ["modal", "report", "cancel", "continue"]
    assert params["properties"]["preflight_blueprints"]["type"] == "boolean"
    assert params["properties"]["continue_on_blueprint_compile_errors"]["type"] == "boolean"


def test_customizable_object_convenience_commands_run_client_side_for_mcp():
    for command in {
        "mutable graph add-node",
        "mutable graph add-parameter",
        "mutable graph add-mesh-option",
        "mutable graph set-base-mesh",
        "mutable graph add-group-child",
        "mutable graph set-node-property",
        "mutable graph connect-pins",
        "mutable graph regenerate-node-pins",
        "mutable compile",
        "mutable graph remove-node",
        "wait-for-ready",
    }:
        assert command in CLIENT_SIDE_COMMANDS


def test_visual_compare_command_runs_client_side_for_mcp():
    assert "capture screenshot" in CLIENT_SIDE_COMMANDS
    assert "capture viewport" in CLIENT_SIDE_COMMANDS
    assert "umg layout extract" in CLIENT_SIDE_COMMANDS
    assert "umg layout compare" in CLIENT_SIDE_COMMANDS


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

    assert "project-specific information, personal information" in report_desc
    assert "any clue that could identify your project" in report_desc
    assert "generic placeholders" in report_steps
    assert "project-specific information, personal information" in feature_desc
    assert "any clue that could identify your project" in feature_desc
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
    assert len(tools) <= 222


def test_skeletal_socket_tools_are_exposed():
    tools = extract_tools()
    tool_names = {t["name"] for t in tools}
    assert "asset skeletal-socket create" in tool_names
    assert "asset skeletal-socket remove" in tool_names


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
