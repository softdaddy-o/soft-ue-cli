"""soft-ue-cli entry point."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

from .client import call_tool, health_check
from .discovery import get_server_url


def _print_json(data: object) -> None:
    print(json.dumps(data, indent=2, ensure_ascii=False))


def _parse_vector(csv: str) -> list[float]:
    """Parse a comma-separated string like '1.0,2.0,3.0' into a list of floats."""
    try:
        return [float(x) for x in csv.split(",")]
    except ValueError:
        print(f"error: expected comma-separated numbers, got '{csv}'", file=sys.stderr)
        sys.exit(1)


def _parse_json_arg(value: str, flag: str) -> object:
    """Parse a JSON string, printing an error and exiting on failure."""
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        print(f"error: {flag} must be valid JSON", file=sys.stderr)
        sys.exit(1)


# -- Command handlers ----------------------------------------------------------

def cmd_status(args: argparse.Namespace) -> None:
    _print_json(health_check())


def cmd_spawn_actor(args: argparse.Namespace) -> None:
    arguments: dict = {"actor_class": args.actor_class}
    if args.location:
        arguments["location"] = _parse_vector(args.location)
    if args.rotation:
        arguments["rotation"] = _parse_vector(args.rotation)
    if args.label:
        arguments["label"] = args.label
    if args.world:
        arguments["world"] = args.world
    _print_json(call_tool("spawn-actor", arguments))


def cmd_query_level(args: argparse.Namespace) -> None:
    arguments: dict = {"limit": args.limit}
    if args.actor_name:
        arguments["actor_name"] = args.actor_name
    if args.class_filter:
        arguments["class_filter"] = args.class_filter
    if args.search:
        arguments["search"] = args.search
    if args.tag:
        arguments["tag_filter"] = args.tag
    if args.components:
        arguments["include_components"] = True
    _print_json(call_tool("query-level", arguments))


def cmd_call_function(args: argparse.Namespace) -> None:
    arguments: dict = {
        "actor_name": args.actor_name,
        "function_name": args.function_name,
    }
    if args.args:
        arguments["args"] = _parse_json_arg(args.args, "--args")
    _print_json(call_tool("call-function", arguments))


def cmd_set_property(args: argparse.Namespace) -> None:
    _print_json(call_tool("set-property", {
        "actor_name":    args.actor_name,
        "property_name": args.property_name,
        "value":         args.value,
    }))


def cmd_get_logs(args: argparse.Namespace) -> None:
    arguments: dict = {"lines": args.lines}
    if args.filter:
        arguments["filter"] = args.filter
    if args.category:
        arguments["category"] = args.category
    result = call_tool("get-logs", arguments)
    if args.raw:
        for line in result.get("lines", []):
            print(line)
    else:
        _print_json(result)


def cmd_get_console_var(args: argparse.Namespace) -> None:
    _print_json(call_tool("get-console-var", {"name": args.name}))


def cmd_set_console_var(args: argparse.Namespace) -> None:
    _print_json(call_tool("set-console-var", {"name": args.name, "value": args.value}))


# -- Editor tool handlers ------------------------------------------------------

def cmd_class_hierarchy(args: argparse.Namespace) -> None:
    arguments: dict = {"class_name": args.class_name}
    if args.direction:
        arguments["direction"] = args.direction
    if args.depth is not None:
        arguments["depth"] = args.depth
    if args.no_blueprints:
        arguments["include_blueprints"] = False
    _print_json(call_tool("get-class-hierarchy", arguments))


def cmd_query_asset(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.query:
        arguments["query"] = args.query
    if args.asset_class:
        arguments["class"] = args.asset_class
    if args.path:
        arguments["path"] = args.path
    if args.limit is not None:
        arguments["limit"] = args.limit
    if args.asset_path:
        arguments["asset_path"] = args.asset_path
    if args.depth is not None:
        arguments["depth"] = args.depth
    if args.include_defaults:
        arguments["include_defaults"] = True
    if args.property_filter:
        arguments["property_filter"] = args.property_filter
    if args.category_filter:
        arguments["category_filter"] = args.category_filter
    if args.row_filter:
        arguments["row_filter"] = args.row_filter
    if args.search:
        arguments["search"] = args.search
    _print_json(call_tool("query-asset", arguments))


def cmd_delete_asset(args: argparse.Namespace) -> None:
    _print_json(call_tool("delete-asset", {"asset_path": args.asset_path}))


def cmd_get_asset_diff(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.scm_type:
        arguments["scm_type"] = args.scm_type
    if args.base_revision:
        arguments["base_revision"] = args.base_revision
    _print_json(call_tool("get-asset-diff", arguments))


def cmd_get_asset_preview(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.resolution is not None:
        arguments["resolution"] = args.resolution
    if args.format:
        arguments["format"] = args.format
    if args.output:
        arguments["output"] = args.output
    _print_json(call_tool("get-asset-preview", arguments))


def cmd_open_asset(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.asset_path:
        arguments["asset_path"] = args.asset_path
    if args.window_name:
        arguments["window_name"] = args.window_name
    if args.no_focus:
        arguments["bring_to_front"] = False
    _print_json(call_tool("open-asset", arguments))


def cmd_query_blueprint(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.include:
        arguments["include"] = args.include
    if args.no_detail:
        arguments["detailed"] = False
    if args.property_filter:
        arguments["property_filter"] = args.property_filter
    if args.category_filter:
        arguments["category_filter"] = args.category_filter
    if args.include_inherited:
        arguments["include_inherited"] = True
    if args.component_filter:
        arguments["component_filter"] = args.component_filter
    if args.include_non_overridden:
        arguments["include_non_overridden"] = True
    if args.search:
        arguments["search"] = args.search
    _print_json(call_tool("query-blueprint", arguments))


def cmd_query_blueprint_graph(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.node_guid:
        arguments["node_guid"] = args.node_guid
    if args.callable_name:
        arguments["callable_name"] = args.callable_name
    if args.list_callables:
        arguments["list_callables"] = True
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    if args.graph_type:
        arguments["graph_type"] = args.graph_type
    if args.include_positions:
        arguments["include_positions"] = True
    if args.search:
        arguments["search"] = args.search
    if args.include_anim_props:
        arguments["include_anim_node_properties"] = True
    _print_json(call_tool("query-blueprint-graph", arguments))


def cmd_build_and_relaunch(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.config:
        arguments["build_config"] = args.config
    if args.skip_relaunch:
        arguments["skip_relaunch"] = True
    _print_json(call_tool("build-and-relaunch", arguments))


def cmd_trigger_live_coding(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.wait:
        arguments["wait_for_completion"] = True
    _print_json(call_tool("trigger-live-coding", arguments))


def cmd_capture_screenshot(args: argparse.Namespace) -> None:
    arguments: dict = {"mode": args.mode}
    if args.window_name:
        arguments["window_name"] = args.window_name
    if args.region:
        arguments["region"] = [int(x) for x in args.region.split(",")]
    if args.format:
        arguments["format"] = args.format
    if args.output:
        arguments["output"] = args.output
    _print_json(call_tool("capture-screenshot", arguments))


def cmd_query_material(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.include:
        arguments["include"] = args.include
    if args.include_positions:
        arguments["include_positions"] = True
    if args.no_defaults:
        arguments["include_defaults"] = False
    if args.parameter_filter:
        arguments["parameter_filter"] = args.parameter_filter
    _print_json(call_tool("query-material", arguments))


def cmd_pie_session(args: argparse.Namespace) -> None:
    arguments: dict = {"action": args.action}
    if args.mode:
        arguments["mode"] = args.mode
    if args.map:
        arguments["map"] = args.map
    if args.timeout is not None:
        arguments["timeout"] = args.timeout
    if args.include:
        arguments["include"] = args.include.split(",")
    if args.actor_name:
        arguments["actor_name"] = args.actor_name
    if args.property:
        arguments["property"] = args.property
    if args.operator:
        arguments["operator"] = args.operator
    if args.expected is not None:
        arguments["expected"] = _parse_json_arg(args.expected, "--expected")
    if args.wait_timeout is not None:
        arguments["wait_timeout"] = args.wait_timeout
    _print_json(call_tool("pie-session", arguments))


def cmd_pie_input(args: argparse.Namespace) -> None:
    arguments: dict = {"action": args.action}
    if args.player_index is not None:
        arguments["player_index"] = args.player_index
    if args.key:
        arguments["key"] = args.key
    if args.action_name:
        arguments["action_name"] = args.action_name
    if args.axis_name:
        arguments["axis_name"] = args.axis_name
    if args.value is not None:
        arguments["value"] = args.value
    if args.release:
        arguments["pressed"] = False
    if args.target:
        arguments["target"] = _parse_vector(args.target)
    if args.target_actor:
        arguments["target_actor"] = args.target_actor
    if args.acceptance_radius is not None:
        arguments["acceptance_radius"] = args.acceptance_radius
    _print_json(call_tool("pie-input", arguments))


def cmd_insights_capture(args: argparse.Namespace) -> None:
    arguments: dict = {"action": args.action}
    if args.channels:
        arguments["channels"] = args.channels.split(",")
    if args.output_file:
        arguments["output_file"] = args.output_file
    _print_json(call_tool("insights-capture", arguments))


def cmd_insights_list_traces(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.directory:
        arguments["directory"] = args.directory
    _print_json(call_tool("insights-list-traces", arguments))


def cmd_insights_analyze(args: argparse.Namespace) -> None:
    arguments: dict = {"trace_file": args.trace_file}
    if args.analysis_type:
        arguments["analysis_type"] = args.analysis_type
    _print_json(call_tool("insights-analyze", arguments))


def cmd_project_info(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.section:
        arguments["section"] = args.section
    _print_json(call_tool("get-project-info", arguments))


def cmd_find_references(args: argparse.Namespace) -> None:
    arguments: dict = {
        "type": args.type,
        "asset_path": args.asset_path,
    }
    if args.variable_name:
        arguments["variable_name"] = args.variable_name
    if args.node_class:
        arguments["node_class"] = args.node_class
    if args.function_name:
        arguments["function_name"] = args.function_name
    if args.limit is not None:
        arguments["limit"] = args.limit
    if args.search:
        arguments["search"] = args.search
    _print_json(call_tool("find-references", arguments))


_SCRIPTS_DIR = Path.home() / ".soft-ue-bridge" / "scripts"

_VALID_SCRIPT_NAME_CHARS = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
)


def _ensure_scripts_dir() -> Path:
    """Return the scripts directory, creating it if needed."""
    _SCRIPTS_DIR.mkdir(parents=True, exist_ok=True)
    return _SCRIPTS_DIR


def _validate_script_name(name: str) -> None:
    """Exit with error if name is empty or contains characters that could cause path traversal."""
    if not name or not all(c in _VALID_SCRIPT_NAME_CHARS for c in name):
        print(
            "error: script name must contain only letters, digits, hyphens, and underscores",
            file=sys.stderr,
        )
        sys.exit(1)


def cmd_run_python_script(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.name:
        _validate_script_name(args.name)
        dest = _SCRIPTS_DIR / f"{args.name}.py"
        if not dest.exists():
            print(f"error: script '{args.name}' not found", file=sys.stderr)
            sys.exit(1)
        arguments["script"] = dest.read_text(encoding="utf-8")
    else:
        if args.script:
            arguments["script"] = args.script
        if args.script_path:
            arguments["script_path"] = args.script_path
        if not arguments:
            print("error: provide --name, --script, or --script-path", file=sys.stderr)
            sys.exit(1)
    if args.python_paths:
        arguments["python_paths"] = args.python_paths
    if args.arguments:
        arguments["arguments"] = _parse_json_arg(args.arguments, "--arguments")
    _print_json(call_tool("run-python-script", arguments))


def cmd_save_script(args: argparse.Namespace) -> None:
    if not args.script and not args.script_path:
        print("error: provide --script or --script-path", file=sys.stderr)
        sys.exit(1)
    if args.script and args.script_path:
        print("error: use --script or --script-path, not both", file=sys.stderr)
        sys.exit(1)
    _validate_script_name(args.name)
    if args.script:
        content = args.script
    else:
        src = Path(args.script_path)
        if not src.exists():
            print(f"error: file not found: {args.script_path}", file=sys.stderr)
            sys.exit(1)
        content = src.read_text(encoding="utf-8")
    dest = _ensure_scripts_dir() / f"{args.name}.py"
    dest.write_text(content, encoding="utf-8")
    _print_json({"status": "ok", "name": args.name, "path": str(dest)})


def cmd_list_scripts(args: argparse.Namespace) -> None:
    scripts = []
    if _SCRIPTS_DIR.exists():
        for f in sorted(_SCRIPTS_DIR.glob("*.py")):
            stat = f.stat()
            scripts.append({
                "name": f.stem,
                "path": str(f),
                "size": stat.st_size,
                "modified": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(stat.st_mtime)),
            })
    _print_json({"scripts": scripts, "count": len(scripts)})


def cmd_delete_script(args: argparse.Namespace) -> None:
    _validate_script_name(args.name)
    dest = _SCRIPTS_DIR / f"{args.name}.py"
    if not dest.exists():
        print(f"error: script '{args.name}' not found", file=sys.stderr)
        sys.exit(1)
    dest.unlink()
    _print_json({"status": "ok", "name": args.name})


def cmd_query_statetree(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.include:
        arguments["include"] = args.include
    if args.no_detail:
        arguments["detailed"] = False
    _print_json(call_tool("query-statetree", arguments))


def cmd_add_statetree_state(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "state_name": args.state_name,
    }
    if args.state_type:
        arguments["state_type"] = args.state_type
    if args.parent_state:
        arguments["parent_state"] = args.parent_state
    if args.selection_behavior:
        arguments["selection_behavior"] = args.selection_behavior
    _print_json(call_tool("add-statetree-state", arguments))


def cmd_add_statetree_task(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "state_name": args.state_name,
        "task_class": args.task_class,
    }
    if args.task_name:
        arguments["task_name"] = args.task_name
    _print_json(call_tool("add-statetree-task", arguments))


def cmd_add_statetree_transition(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "source_state": args.source_state,
        "target_state": args.target_state,
    }
    if args.trigger:
        arguments["trigger"] = args.trigger
    if args.priority is not None:
        arguments["priority"] = args.priority
    _print_json(call_tool("add-statetree-transition", arguments))


def cmd_remove_statetree_state(args: argparse.Namespace) -> None:
    _print_json(call_tool("remove-statetree-state", {
        "asset_path": args.asset_path,
        "state_name": args.state_name,
    }))


def cmd_inspect_widget_blueprint(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.include_defaults:
        arguments["include_defaults"] = True
    if args.depth_limit is not None:
        arguments["depth_limit"] = args.depth_limit
    if args.no_bindings:
        arguments["include_bindings"] = False
    _print_json(call_tool("inspect-widget-blueprint", arguments))


def cmd_set_asset_property(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "property_path": args.property_path,
    }
    if args.value is not None:
        arguments["value"] = _parse_json_arg(args.value, "value")
    if args.component_name:
        arguments["component_name"] = args.component_name
    if args.clear_override:
        arguments["clear_override"] = True
    _print_json(call_tool("set-asset-property", arguments))


def cmd_add_component(args: argparse.Namespace) -> None:
    arguments: dict = {
        "actor_name": args.actor_name,
        "component_class": args.component_class,
    }
    if args.component_name:
        arguments["component_name"] = args.component_name
    if args.attach_to:
        arguments["attach_to"] = args.attach_to
    _print_json(call_tool("add-component", arguments))


def cmd_add_widget(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "widget_class": args.widget_class,
        "widget_name": args.widget_name,
    }
    if args.parent_widget:
        arguments["parent_widget"] = args.parent_widget
    _print_json(call_tool("add-widget", arguments))


def cmd_add_datatable_row(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "row_name": args.row_name,
    }
    if args.row_data:
        arguments["row_data"] = _parse_json_arg(args.row_data, "--row-data")
    _print_json(call_tool("add-datatable-row", arguments))


def cmd_add_graph_node(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "node_class": args.node_class,
    }
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    if args.position:
        arguments["position"] = [int(x) for x in args.position.split(",")]
    if args.no_auto_position:
        arguments["auto_position"] = False
    if args.connect_to_node:
        arguments["connect_to_node"] = args.connect_to_node
    if args.connect_to_pin:
        arguments["connect_to_pin"] = args.connect_to_pin
    if args.properties:
        arguments["properties"] = _parse_json_arg(args.properties, "--properties")
    _print_json(call_tool("add-graph-node", arguments))


def cmd_remove_graph_node(args: argparse.Namespace) -> None:
    _print_json(call_tool("remove-graph-node", {
        "asset_path": args.asset_path,
        "node_id": args.node_id,
    }))


def cmd_connect_graph_pins(args: argparse.Namespace) -> None:
    _print_json(call_tool("connect-graph-pins", {
        "asset_path":  args.asset_path,
        "source_node": args.source_node,
        "source_pin":  args.source_pin,
        "target_node": args.target_node,
        "target_pin":  args.target_pin,
    }))


def cmd_disconnect_graph_pin(args: argparse.Namespace) -> None:
    _print_json(call_tool("disconnect-graph-pin", {
        "asset_path": args.asset_path,
        "node_id":    args.node_id,
        "pin_name":   args.pin_name,
    }))


def cmd_set_node_position(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "positions": _parse_json_arg(args.positions, "--positions"),
    }
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    _print_json(call_tool("set-node-position", arguments))


def cmd_create_asset(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path":  args.asset_path,
        "asset_class": args.asset_class,
    }
    if args.parent_class:
        arguments["parent_class"] = args.parent_class
    if args.row_struct:
        arguments["row_struct"] = args.row_struct
    _print_json(call_tool("create-asset", arguments))


# -- Setup/utility handlers ----------------------------------------------------

def _claude_md_section(cli_cmd: str) -> str:
    return (
        "## Unreal Engine control\n\n"
        f"`{cli_cmd}` controls this UE project via the SoftUEBridge plugin.\n"
        f"Run `{cli_cmd} --help` to see all available commands.\n"
        "The game or editor must be running with SoftUEBridge enabled before using UE commands.\n"
    )


def cmd_setup(args: argparse.Namespace) -> None:
    """Print an LLM-ready prompt to install SoftUEBridge into a UE project."""
    project_path = Path(args.project_path).expanduser().resolve() if args.project_path else Path.cwd()

    plugin_src = (
        Path(args.plugin_src).expanduser().resolve()
        if args.plugin_src
        else Path(__file__).parent / "plugin_data" / "SoftUEBridge"
    )

    uproject_files = list(project_path.glob("*.uproject"))
    uproject_hint = str(uproject_files[0]) if uproject_files else str(project_path / "<YourGame>.uproject")

    plugin_dest = project_path / "Plugins" / "SoftUEBridge"
    claude_md = project_path / "CLAUDE.md"
    cli_cmd = f"{sys.executable} -m soft_ue_cli"

    print(
        f"Install the SoftUEBridge UE plugin into the project at {project_path}:\n\n"
        f"1. Copy the directory\n"
        f"     {plugin_src}\n"
        f"   to\n"
        f"     {plugin_dest}\n\n"
        f'2. Edit {uproject_hint} - add to the "Plugins" array:\n'
        f'     {{"Name": "SoftUEBridge", "Enabled": true}}\n\n'
        f"3. Create or append to {claude_md}:\n\n"
        f"{_claude_md_section(cli_cmd)}\n"
        f"After the user rebuilds and launches UE, verify with:\n"
        f"  {cli_cmd} check-setup"
    )


def cmd_check_setup(args: argparse.Namespace) -> None:
    """Verify plugin files, project settings, and bridge server reachability."""
    project_path = (
        Path(args.project_path).expanduser().resolve()
        if args.project_path
        else Path.cwd()
    )

    ok = "[OK]  "
    fail = "[FAIL]"
    issues: list[str] = []

    # 1. Plugin files
    uplugin = project_path / "Plugins" / "SoftUEBridge" / "SoftUEBridge.uplugin"
    if uplugin.exists():
        print(f"{ok} Plugin files found: {uplugin.parent}.")
    else:
        print(f"{fail} Plugin not found at {uplugin.parent}.")
        issues.append("plugin files missing")

    # 2. .uproject plugin entry
    uproject_files = list(project_path.glob("*.uproject"))
    if not uproject_files:
        print(f"{fail} No .uproject file found in {project_path}.")
        issues.append("no .uproject found")
    else:
        if len(uproject_files) > 1:
            print(f"[WARN] Multiple .uproject files found; using {uproject_files[0].name}.")
        uproject_path = uproject_files[0]
        try:
            data = json.loads(uproject_path.read_text(encoding="utf-8"))
            plugins = data.get("Plugins", [])
            enabled = any(
                p.get("Name") == "SoftUEBridge" and p.get("Enabled", False)
                for p in plugins
            )
            if enabled:
                print(f"{ok} SoftUEBridge enabled in {uproject_path.name}.")
            else:
                print(f"{fail} SoftUEBridge not enabled in {uproject_path.name}.")
                issues.append("plugin not enabled in .uproject")
        except (json.JSONDecodeError, OSError) as exc:
            print(f"{fail} Could not read {uproject_path.name}: {exc}")
            issues.append("could not read .uproject")

    # 3. Bridge server
    info = health_check()
    if "error" in info:
        url = get_server_url()
        print(f"{fail} Bridge server unreachable at {url}: {info['error']}")
        issues.append("bridge server unreachable")
    else:
        print(f"{ok} Bridge server reachable.")
        print()
        _print_json(info)

    if issues:
        print(f"\nSetup incomplete: {', '.join(issues)}", file=sys.stderr)
        sys.exit(1)


def cmd_knowledge(args: argparse.Namespace) -> None:
    """Query the optional knowledge server (RAG)."""
    print("Coming soon. Follow https://github.com/softdaddy-o/soft-ue-cli for updates.")


# -- Argument parser -----------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="soft-ue-cli",
        description=(
            "Control a running Unreal Engine game or editor via the SoftUEBridge C++ plugin.\n"
            "The plugin must be enabled and UE must be running before using any UE commands.\n\n"
            "FIRST-TIME SETUP:\n"
            "  1. cd into your UE project and run 'soft-ue-cli setup' to install the plugin.\n"
            "  2. Rebuild your UE project and launch it.\n"
            "  3. Run 'soft-ue-cli check-setup' to verify the connection.\n\n"
            "All commands output JSON (except get-logs --raw). Exit code 0 = success, 1 = error."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--server", metavar="URL",
        help=(
            "Override the bridge server URL (e.g. http://127.0.0.1:9000). "
            "By default the URL is auto-discovered from the SOFT_UE_BRIDGE_URL env var, "
            "then SOFT_UE_BRIDGE_PORT, then .soft-ue-bridge/instance.json searched upward from cwd "
            "(written by the plugin at startup), then http://127.0.0.1:8080."
        ),
    )
    parser.add_argument(
        "--timeout", type=float, metavar="SEC",
        help=(
            "HTTP request timeout in seconds (default: 30). "
            "Increase for slow operations such as build-and-relaunch (e.g. --timeout 300)."
        ),
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # setup
    p_setup = sub.add_parser(
        "setup",
        help="Install SoftUEBridge plugin into a UE project and enable it in .uproject.",
        description=(
            "Copies the SoftUEBridge C++ plugin into <project-path>/Plugins/SoftUEBridge/\n"
            "and enables it in the .uproject file.\n\n"
            "After running setup:\n"
            "  1. Right-click .uproject -> Generate Visual Studio project files\n"
            "  2. Rebuild in VS or Rider\n"
            "  3. Launch UE -- you should see: LogSoftUEBridge: Bridge server started on port 8080\n"
            "  4. Run 'soft-ue-cli check-setup' to verify the connection\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli setup                  # installs into current directory\n"
            "  soft-ue-cli setup C:/dev/MyGame    # installs into specified path"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_setup.add_argument("project_path", nargs="?", default=None,
        help="Path to UE project root (default: current directory)")
    p_setup.add_argument(
        "--plugin-src", metavar="PATH",
        help="Path to SoftUEBridge plugin source (auto-detected from repo if not specified)",
    )
    p_setup.set_defaults(func=cmd_setup)

    # check-setup
    p_cs = sub.add_parser(
        "check-setup",
        help="Verify plugin files, .uproject settings, and bridge server reachability.",
        description=(
            "Checks plugin files, .uproject settings, and bridge server reachability.\n"
            "Run this after installing the plugin and launching UE.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli check-setup                  # check in current directory\n"
            "  soft-ue-cli check-setup C:/dev/MyGame    # check at specified path"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cs.add_argument("project_path", nargs="?", default=None,
        help="Path to UE project root (default: current directory)")
    p_cs.set_defaults(func=cmd_check_setup)

    # status
    p_status = sub.add_parser(
        "status",
        help="Quick health check -- returns {\"status\": \"ok\"} if the bridge is running.",
        description="Sends a GET request to the bridge server and returns its health status.",
    )
    p_status.set_defaults(func=cmd_status)

    # -------------------------------------------------------------------------
    # Runtime tools
    # -------------------------------------------------------------------------

    # spawn-actor
    p_spawn = sub.add_parser(
        "spawn-actor",
        help="Spawn an actor in the game world or editor level.",
        description=(
            "Spawns an actor of the given class at the specified location/rotation.\n"
            "In runtime (game running): spawns into the active game world.\n"
            "In editor: spawns into the editor level. Use --world pie to target PIE world.\n\n"
            "ACTOR CLASS formats accepted:\n"
            "  Native class:    PointLight, StaticMeshActor, Character\n"
            "  Blueprint path:  /Game/Blueprints/BP_Enemy  (with or without _C suffix)\n\n"
            "COORDINATES: Unreal units (cm). Default location is world origin (0,0,0).\n"
            "ROTATION: Pitch, Yaw, Roll in degrees."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_spawn.add_argument("actor_class", help="Native class name or Blueprint asset path")
    p_spawn.add_argument("--location", metavar="X,Y,Z", help="Spawn location in cm, e.g. 0,0,200")
    p_spawn.add_argument("--rotation", metavar="P,Y,R", help="Spawn rotation in degrees, e.g. 0,90,0")
    p_spawn.add_argument("--label", metavar="NAME", help="Actor label in the World Outliner (editor)")
    p_spawn.add_argument("--world", choices=["editor", "pie"], help="Target world: editor (default) or pie")
    p_spawn.set_defaults(func=cmd_spawn_actor)

    # query-level
    p_ql = sub.add_parser(
        "query-level",
        help="List actors in the running level with their transforms, tags, and components.",
        description=(
            "Returns a JSON array of actors in the current game world.\n"
            "Each actor entry includes: name, class, location, rotation, scale, tags.\n"
            "Use --components to also include the actor's component list.\n\n"
            "All filters are combined (AND logic). Wildcards (*) are supported in --search and --actor-name.\n\n"
            "EXAMPLES:\n"
            "  List all actors:                   soft-ue-cli query-level\n"
            "  Find all lights:                   soft-ue-cli query-level --class-filter PointLight\n"
            "  Find actor by name:                soft-ue-cli query-level --actor-name BP_Hero\n"
            "  Wildcard search:                   soft-ue-cli query-level --search \"Enemy*\"\n"
            "  Tagged actors with components:     soft-ue-cli query-level --tag hostile --components"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ql.add_argument("--actor-name",   metavar="PATTERN", help="Match actor by exact name or label (supports * wildcard)")
    p_ql.add_argument("--class-filter", metavar="CLASS",   help="Only return actors of this class (e.g. PointLight)")
    p_ql.add_argument("--search",       metavar="TEXT",    help="Search actor names/labels (supports * wildcard)")
    p_ql.add_argument("--tag",          metavar="TAG",     help="Only return actors with this gameplay tag")
    p_ql.add_argument("--components",   action="store_true", help="Include component list for each actor")
    p_ql.add_argument("--limit",        type=int, default=100, metavar="N", help="Max actors to return (default: 100)")
    p_ql.set_defaults(func=cmd_query_level)

    # call-function
    p_cf = sub.add_parser(
        "call-function",
        help="Call a BlueprintCallable function on an actor by name.",
        description=(
            "Calls a UFunction on the named actor using UE's reflection system.\n"
            "The function must be BlueprintCallable or marked with UFUNCTION().\n"
            "Returns any output parameters and the return value as JSON.\n\n"
            "ARGS format: JSON object mapping parameter names to values.\n"
            "  e.g. '{\"damage\": 25.0, \"cause\": \"fire\"}'\n\n"
            "EXAMPLES:\n"
            "  No args:    soft-ue-cli call-function BP_Hero Jump\n"
            "  With args:  soft-ue-cli call-function BP_Hero TakeDamage --args '{\"damage\": 25.0}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cf.add_argument("actor_name",    help="Actor name or label as shown in query-level output")
    p_cf.add_argument("function_name", help="Exact function name (case-sensitive)")
    p_cf.add_argument("--args",        metavar="JSON", help="Function arguments as a JSON object")
    p_cf.set_defaults(func=cmd_call_function)

    # set-property (runtime actors)
    p_sp = sub.add_parser(
        "set-property",
        help="Set a property on a runtime actor or component using UE reflection.",
        description=(
            "Sets a UPROPERTY value on the named actor via UE's reflection system.\n"
            "For component properties, use dot notation: ComponentName.PropertyName\n\n"
            "VALUE is always passed as a string; UE reflection handles type conversion.\n\n"
            "For setting properties on Blueprint/asset defaults, use 'set-asset-property'.\n\n"
            "EXAMPLES:\n"
            "  Actor property:     soft-ue-cli set-property PointLight_0 Intensity 5000\n"
            "  Component property: soft-ue-cli set-property BP_Hero LightComponent.Intensity 3000\n"
            "  Boolean:            soft-ue-cli set-property BP_Enemy bIsHostile true\n"
            "  Vector:             soft-ue-cli set-property BP_Hero RelativeScale3D \"2,2,2\""
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_sp.add_argument("actor_name",    help="Actor name or label as shown in query-level output")
    p_sp.add_argument("property_name", help="Property name, or ComponentName.PropertyName for component properties")
    p_sp.add_argument("value",         help="New value as a string (UE reflection converts the type)")
    p_sp.set_defaults(func=cmd_set_property)

    # get-logs
    p_gl = sub.add_parser(
        "get-logs",
        help="Read recent UE output log entries from the bridge's in-memory ring buffer.",
        description=(
            "Returns the most recent log lines captured by the SoftUEBridge plugin.\n"
            "The ring buffer holds the last 2000 lines since UE started.\n"
            "Each line is prefixed with [Category][Verbosity].\n\n"
            "Use --raw for plain text output (one line per entry) instead of JSON.\n"
            "Use --filter to search for errors, warnings, or specific keywords.\n\n"
            "EXAMPLES:\n"
            "  Last 50 lines:          soft-ue-cli get-logs --lines 50\n"
            "  Filter errors:          soft-ue-cli get-logs --filter error\n"
            "  Specific category:      soft-ue-cli get-logs --category LogAI\n"
            "  Plain text output:      soft-ue-cli get-logs --raw --lines 20"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_gl.add_argument("--lines",    type=int, default=100, metavar="N", help="Number of recent lines to return (default: 100)")
    p_gl.add_argument("--filter",   metavar="TEXT",  help="Case-insensitive substring filter on log text")
    p_gl.add_argument("--category", metavar="CAT",   help="Filter by log category (e.g. LogAI, LogTemp, LogEngine)")
    p_gl.add_argument("--raw",      action="store_true", help="Output plain text lines instead of JSON")
    p_gl.set_defaults(func=cmd_get_logs)

    # get-console-var
    p_gcv = sub.add_parser(
        "get-console-var",
        help="Read the current value of a UE console variable (CVar).",
        description=(
            "Reads a console variable value using IConsoleManager.\n"
            "Returns {\"name\": \"...\", \"value\": \"...\"}.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli get-console-var r.ScreenPercentage\n"
            "  soft-ue-cli get-console-var t.MaxFPS"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_gcv.add_argument("name", help="CVar name (case-sensitive), e.g. r.ScreenPercentage")
    p_gcv.set_defaults(func=cmd_get_console_var)

    # set-console-var
    p_scv = sub.add_parser(
        "set-console-var",
        help="Set a UE console variable (CVar) to a new value at runtime.",
        description=(
            "Sets a console variable using IConsoleManager::Set().\n"
            "Changes take effect immediately in the running game.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli set-console-var r.ScreenPercentage 75\n"
            "  soft-ue-cli set-console-var t.MaxFPS 60\n"
            "  soft-ue-cli set-console-var r.VSync 0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_scv.add_argument("name",  help="CVar name (case-sensitive)")
    p_scv.add_argument("value", help="New value as a string")
    p_scv.set_defaults(func=cmd_set_console_var)

    # -------------------------------------------------------------------------
    # Editor tools — Analysis
    # -------------------------------------------------------------------------

    p_ch = sub.add_parser(
        "class-hierarchy",
        help="Inspect UE class inheritance tree for a given class.",
        description=(
            "Walks the UE class reflection system to show ancestors and/or descendants\n"
            "of the given class. Works for both native C++ classes and Blueprints.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli class-hierarchy AActor\n"
            "  soft-ue-cli class-hierarchy Character --direction parents\n"
            "  soft-ue-cli class-hierarchy Actor --direction children --depth 2 --no-blueprints"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ch.add_argument("class_name", help="Class name to inspect (e.g. AActor, BP_MyCharacter)")
    p_ch.add_argument("--direction", choices=["parents", "children", "both"], help="Direction (default: both)")
    p_ch.add_argument("--depth", type=int, metavar="N", help="Max inheritance depth (default: 10)")
    p_ch.add_argument("--no-blueprints", action="store_true", help="Exclude Blueprint subclasses from children")
    p_ch.set_defaults(func=cmd_class_hierarchy)

    # -------------------------------------------------------------------------
    # Editor tools — Asset
    # -------------------------------------------------------------------------

    p_qa = sub.add_parser(
        "query-asset",
        help="Search for or inspect assets in the Content Browser.",
        description=(
            "Two modes:\n"
            "  Search mode (--query, --class, --path): find assets matching the filter.\n"
            "  Inspect mode (--asset-path): read properties of a specific asset.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli query-asset --query \"BP_*\" --class Blueprint\n"
            "  soft-ue-cli query-asset --asset-path /Game/Data/DT_Items\n"
            "  soft-ue-cli query-asset --path /Game/Characters --class SkeletalMesh"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qa.add_argument("--query",           metavar="PATTERN", help="Search pattern (supports * and ?)")
    p_qa.add_argument("--class",           metavar="CLASS", dest="asset_class", help="Filter by asset class (e.g. Blueprint, StaticMesh)")
    p_qa.add_argument("--path",            metavar="PATH", help="Filter by path prefix (e.g. /Game/Blueprints)")
    p_qa.add_argument("--limit",           type=int, metavar="N", help="Max search results (default: 100)")
    p_qa.add_argument("--asset-path",      metavar="PATH", help="Asset path to inspect")
    p_qa.add_argument("--depth",           type=int, metavar="N", help="Recursion depth for inspect (default: 2)")
    p_qa.add_argument("--include-defaults", action="store_true", help="Include default/empty properties")
    p_qa.add_argument("--property-filter", metavar="PATTERN", help="Filter properties by name")
    p_qa.add_argument("--category-filter", metavar="CAT", help="Filter properties by category")
    p_qa.add_argument("--row-filter",      metavar="PATTERN", help="Filter DataTable rows by name")
    p_qa.add_argument("--search",          metavar="TEXT", help="General search filter")
    p_qa.set_defaults(func=cmd_query_asset)

    p_da = sub.add_parser(
        "delete-asset",
        help="Delete an asset from the Content Browser.",
        description=(
            "Permanently deletes an asset from the project. This cannot be undone.\n"
            "The asset must not be referenced by other assets or the delete will fail.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli delete-asset /Game/Blueprints/BP_OldEnemy\n"
            "  soft-ue-cli delete-asset /Game/Textures/T_Unused"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_da.add_argument("asset_path", help="Asset path to delete (e.g. /Game/MyBlueprint)")
    p_da.set_defaults(func=cmd_delete_asset)

    p_gad = sub.add_parser(
        "get-asset-diff",
        help="Show SCM diff for an asset (git or Perforce).",
        description=(
            "Compares the current state of a .uasset file against source control.\n"
            "Supports git (binary diff via git show) and Perforce. Auto-detects SCM by default.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli get-asset-diff /Game/Blueprints/BP_Player\n"
            "  soft-ue-cli get-asset-diff /Game/Data/DT_Items --scm-type git\n"
            "  soft-ue-cli get-asset-diff /Game/Data/DT_Items --base-revision HEAD~1"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_gad.add_argument("asset_path", help="Asset path or absolute .uasset file path")
    p_gad.add_argument("--scm-type", choices=["git", "perforce", "auto"], help="SCM type (default: auto)")
    p_gad.add_argument("--base-revision", metavar="REV", help="Base revision (git commit hash or Perforce CL)")
    p_gad.set_defaults(func=cmd_get_asset_diff)

    p_gap = sub.add_parser(
        "get-asset-preview",
        help="Generate a thumbnail/preview image for an asset.",
        description=(
            "Renders a thumbnail for any Content Browser asset using UE's thumbnail system.\n"
            "Output can be saved to a file (default) or returned as a base64 string.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli get-asset-preview /Game/Textures/T_Player\n"
            "  soft-ue-cli get-asset-preview /Game/Meshes/SM_Rock --resolution 512 --format jpeg\n"
            "  soft-ue-cli get-asset-preview /Game/Blueprints/BP_Hero --output base64"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_gap.add_argument("asset_path", help="Asset path (e.g. /Game/Textures/T_Player)")
    p_gap.add_argument("--resolution", type=int, metavar="N", help="Output resolution in pixels, 64-1024 (default: 256)")
    p_gap.add_argument("--format", choices=["png", "jpeg"], help="Image format (default: png)")
    p_gap.add_argument("--output", choices=["file", "base64"], help="Output mode: file (default) or base64")
    p_gap.set_defaults(func=cmd_get_asset_preview)

    p_oa = sub.add_parser(
        "open-asset",
        help="Open an asset or editor window in the UE editor.",
        description=(
            "Opens an asset in its editor (Blueprint editor, Material editor, etc.) or\n"
            "brings a named editor panel to the front.\n"
            "Provide --asset-path, --window-name, or both.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli open-asset --asset-path /Game/Blueprints/BP_Player\n"
            "  soft-ue-cli open-asset --window-name OutputLog\n"
            "  soft-ue-cli open-asset --asset-path /Game/Materials/M_Rock --no-focus"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_oa.add_argument("--asset-path",  metavar="PATH", help="Asset path to open (e.g. /Game/Blueprints/BP_Player)")
    p_oa.add_argument("--window-name", metavar="NAME", help="Editor window to open (e.g. OutputLog, ContentBrowser)")
    p_oa.add_argument("--no-focus",    action="store_true", help="Do not bring the window to front")
    p_oa.set_defaults(func=cmd_open_asset)

    # -------------------------------------------------------------------------
    # Editor tools — Blueprint
    # -------------------------------------------------------------------------

    p_qb = sub.add_parser(
        "query-blueprint",
        help="Inspect a Blueprint asset (variables, functions, components, defaults).",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli query-blueprint /Game/Blueprints/BP_Character\n"
            "  soft-ue-cli query-blueprint /Game/Blueprints/BP_Character --include variables\n"
            "  soft-ue-cli query-blueprint /Game/Blueprints/BP_Character --search \"*Health*\""
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qb.add_argument("asset_path", help="Blueprint asset path")
    p_qb.add_argument("--include", metavar="SECTION",
        help="Sections to include: functions, variables, components, defaults, graph, component_overrides, all (default: all)")
    p_qb.add_argument("--no-detail",            action="store_true", help="Omit detailed flags and metadata")
    p_qb.add_argument("--property-filter",      metavar="PATTERN", help="Filter properties by name (wildcards)")
    p_qb.add_argument("--category-filter",      metavar="CAT", help="Filter properties by category")
    p_qb.add_argument("--include-inherited",    action="store_true", help="Include inherited properties")
    p_qb.add_argument("--component-filter",     metavar="PATTERN", help="Filter components by name (wildcards)")
    p_qb.add_argument("--include-non-overridden", action="store_true", help="Include non-overridden properties")
    p_qb.add_argument("--search",               metavar="TEXT", help="Filter items by name (wildcards)")
    p_qb.set_defaults(func=cmd_query_blueprint)

    p_qbg = sub.add_parser(
        "query-blueprint-graph",
        help="Inspect a Blueprint's event graph, functions, or macros.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli query-blueprint-graph /Game/Blueprints/BP_Character\n"
            "  soft-ue-cli query-blueprint-graph /Game/Blueprints/BP_Character --list-callables\n"
            "  soft-ue-cli query-blueprint-graph /Game/Blueprints/BP_Character --callable-name BeginPlay"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qbg.add_argument("asset_path", help="Blueprint asset path")
    p_qbg.add_argument("--node-guid",          metavar="GUID", help="Get a specific node by GUID")
    p_qbg.add_argument("--callable-name",      metavar="NAME", help="Get a specific event/function/macro graph")
    p_qbg.add_argument("--list-callables",     action="store_true", help="List all callables without full graph")
    p_qbg.add_argument("--graph-name",         metavar="NAME", help="Filter by graph name")
    p_qbg.add_argument("--graph-type",         metavar="TYPE", help="Filter by type: event, function, macro, anim_graph, etc.")
    p_qbg.add_argument("--include-positions",  action="store_true", help="Include node X/Y positions")
    p_qbg.add_argument("--search",             metavar="TEXT", help="Filter nodes by title (wildcards)")
    p_qbg.add_argument("--include-anim-props", action="store_true", help="Include AnimNode struct properties on AnimGraph nodes")
    p_qbg.set_defaults(func=cmd_query_blueprint_graph)

    # -------------------------------------------------------------------------
    # Editor tools — Build
    # -------------------------------------------------------------------------

    p_bar = sub.add_parser(
        "build-and-relaunch",
        help="Trigger a C++ build and relaunch the editor.",
        description=(
            "Initiates a full C++ build of the project and relaunches the UE editor.\n"
            "Build times vary but are typically 1-5 minutes. Use --timeout to increase\n"
            "the HTTP timeout so the request does not expire before the build finishes.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli --timeout 300 build-and-relaunch\n"
            "  soft-ue-cli --timeout 300 build-and-relaunch --config Debug\n"
            "  soft-ue-cli --timeout 300 build-and-relaunch --skip-relaunch"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_bar.add_argument("--config", choices=["Development", "Debug", "Shipping"],
        help="Build configuration (default: Development)")
    p_bar.add_argument("--skip-relaunch", action="store_true", help="Build only, do not relaunch the editor")
    p_bar.set_defaults(func=cmd_build_and_relaunch)

    p_tlc = sub.add_parser(
        "trigger-live-coding",
        help="Trigger Live Coding compilation (hot reload C++ changes).",
        description=(
            "Triggers the UE Live Coding compiler to pick up C++ source changes without\n"
            "a full editor restart. Requires Live Coding to be enabled in editor preferences.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli trigger-live-coding\n"
            "  soft-ue-cli --timeout 120 trigger-live-coding --wait"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_tlc.add_argument("--wait", action="store_true", help="Wait for compilation to finish before returning")
    p_tlc.set_defaults(func=cmd_trigger_live_coding)

    # -------------------------------------------------------------------------
    # Editor tools — Editor
    # -------------------------------------------------------------------------

    p_cs2 = sub.add_parser(
        "capture-screenshot",
        help="Capture a screenshot of the editor window or a specific panel.",
        description=(
            "Modes:\n"
            "  window  — capture the entire editor\n"
            "  tab     — capture a specific editor panel (use --window-name)\n"
            "  region  — capture a screen region (use --region X,Y,W,H)\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli capture-screenshot window\n"
            "  soft-ue-cli capture-screenshot tab --window-name Blueprint\n"
            "  soft-ue-cli capture-screenshot region --region 0,0,800,600 --output base64"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cs2.add_argument("mode", choices=["window", "tab", "region"], help="Capture mode")
    p_cs2.add_argument("--window-name", metavar="NAME", help="Editor panel name for 'tab' mode")
    p_cs2.add_argument("--region",      metavar="X,Y,W,H", help="Screen coordinates for 'region' mode")
    p_cs2.add_argument("--format",  choices=["png", "jpeg"], help="Image format (default: png)")
    p_cs2.add_argument("--output",  choices=["file", "base64"], help="Output mode: file (default) or base64")
    p_cs2.set_defaults(func=cmd_capture_screenshot)

    # -------------------------------------------------------------------------
    # Editor tools — Material
    # -------------------------------------------------------------------------

    p_qm = sub.add_parser(
        "query-material",
        help="Inspect a Material or MaterialInstance asset.",
        description=(
            "Returns the material graph nodes and/or scalar/vector/texture parameters.\n"
            "Works on both Material and MaterialInstance assets.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli query-material /Game/Materials/M_Rock\n"
            "  soft-ue-cli query-material /Game/Materials/MI_Rock --include parameters\n"
            "  soft-ue-cli query-material /Game/Materials/M_Rock --parameter-filter \"*Color*\""
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qm.add_argument("asset_path", help="Material or MaterialInstance asset path")
    p_qm.add_argument("--include", metavar="SECTION", help="What to include: graph, parameters, all (default: all)")
    p_qm.add_argument("--include-positions", action="store_true", help="Include expression X/Y positions")
    p_qm.add_argument("--no-defaults",       action="store_true", help="Exclude default parameter values")
    p_qm.add_argument("--parameter-filter",  metavar="PATTERN", help="Filter parameters by name (wildcards)")
    p_qm.set_defaults(func=cmd_query_material)

    # -------------------------------------------------------------------------
    # Editor tools — PIE
    # -------------------------------------------------------------------------

    p_ps = sub.add_parser(
        "pie-session",
        help="Control Play-In-Editor sessions (start, stop, pause, resume, wait-for).",
        description=(
            "Actions:\n"
            "  start      — launch PIE (use --mode, --map, --timeout)\n"
            "  stop       — end PIE session\n"
            "  pause      — pause the game\n"
            "  resume     — resume a paused game\n"
            "  get-state  — get current PIE state (use --include)\n"
            "  wait-for   — poll a property until condition is met (use --actor-name, --property, etc.)\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli pie-session start\n"
            "  soft-ue-cli pie-session start --map /Game/Maps/TestLevel --timeout 60\n"
            "  soft-ue-cli pie-session wait-for --actor-name BP_Hero --property Health --operator less_than --expected 0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ps.add_argument("action", choices=["start", "stop", "pause", "resume", "get-state", "wait-for"])
    p_ps.add_argument("--mode",         choices=["viewport", "new_window", "standalone"], help="PIE launch mode (for start)")
    p_ps.add_argument("--map",          metavar="PATH", help="Map to load (for start)")
    p_ps.add_argument("--timeout",      type=float, metavar="SEC", help="Timeout waiting for PIE ready (for start, default: 30)")
    p_ps.add_argument("--include",      metavar="LIST", help="Comma-separated: world,players (for get-state)")
    p_ps.add_argument("--actor-name",   metavar="NAME", help="Actor to monitor (for wait-for)")
    p_ps.add_argument("--property",     metavar="PROP", help="Property to check (for wait-for)")
    p_ps.add_argument("--operator",     choices=["equals", "not_equals", "less_than", "greater_than", "contains"],
        help="Comparison operator (for wait-for)")
    p_ps.add_argument("--expected",     metavar="JSON", help="Expected value as JSON (for wait-for)")
    p_ps.add_argument("--wait-timeout", type=float, metavar="SEC", help="Timeout for wait-for (default: 10)")
    p_ps.set_defaults(func=cmd_pie_session)

    p_pi = sub.add_parser(
        "pie-input",
        help="Send input events to PIE (key press, action, axis, move-to, look-at).",
        description=(
            "Actions:\n"
            "  key      — press/release a key (use --key)\n"
            "  action   — trigger an input action (use --action-name)\n"
            "  axis     — set an axis value (use --axis-name --value)\n"
            "  move-to  — move character to location (use --target X,Y,Z)\n"
            "  look-at  — rotate character toward target (use --target or --target-actor)\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli pie-input key --key Space\n"
            "  soft-ue-cli pie-input action --action-name Jump\n"
            "  soft-ue-cli pie-input axis --axis-name MoveForward --value 1.0\n"
            "  soft-ue-cli pie-input move-to --target 100,200,0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_pi.add_argument("action", choices=["key", "action", "axis", "move-to", "look-at"])
    p_pi.add_argument("--player-index",      type=int, metavar="N", help="Player index (default: 0)")
    p_pi.add_argument("--key",               metavar="KEY", help="Key name (e.g. W, Space, LeftMouseButton)")
    p_pi.add_argument("--action-name",       metavar="NAME", help="Input action name (e.g. Jump)")
    p_pi.add_argument("--axis-name",         metavar="NAME", help="Input axis name (e.g. MoveForward)")
    p_pi.add_argument("--value",             type=float, help="Axis value (-1.0 to 1.0)")
    p_pi.add_argument("--release",           action="store_true", help="Release (instead of press) for key/action")
    p_pi.add_argument("--target",            metavar="X,Y,Z", help="Target location for move-to/look-at")
    p_pi.add_argument("--target-actor",      metavar="NAME", help="Target actor name for look-at")
    p_pi.add_argument("--acceptance-radius", type=float, metavar="CM", help="Acceptance radius for move-to (default: 50)")
    p_pi.set_defaults(func=cmd_pie_input)

    # -------------------------------------------------------------------------
    # Editor tools — Performance
    # -------------------------------------------------------------------------

    p_ic = sub.add_parser(
        "insights-capture",
        help="Start, stop, or check status of an Unreal Insights trace capture.",
        description=(
            "Controls real-time trace capture for Unreal Insights profiling.\n"
            "Traces are saved as .utrace files in the project's Saved/Profiling directory.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli insights-capture start --channels cpu,gpu,memory\n"
            "  soft-ue-cli insights-capture start --output-file MyTrace\n"
            "  soft-ue-cli insights-capture status\n"
            "  soft-ue-cli insights-capture stop"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ic.add_argument("action", choices=["start", "stop", "status"])
    p_ic.add_argument("--channels",     metavar="LIST", help="Comma-separated trace channels (for start, e.g. cpu,gpu,memory)")
    p_ic.add_argument("--output-file",  metavar="FILE", help="Output filename for trace (for start)")
    p_ic.set_defaults(func=cmd_insights_capture)

    p_ilt = sub.add_parser(
        "insights-list-traces",
        help="List .utrace files in the profiling output directory.",
        description=(
            "Lists .utrace files in the project's Saved/Profiling directory (or a custom path).\n"
            "Returns file names, sizes, and timestamps.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli insights-list-traces\n"
            "  soft-ue-cli insights-list-traces --directory D:/Traces"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ilt.add_argument("--directory", metavar="PATH", help="Directory to search (default: Project/Saved/Profiling)")
    p_ilt.set_defaults(func=cmd_insights_list_traces)

    p_ia = sub.add_parser(
        "insights-analyze",
        help="Analyze an Unreal Insights trace file.",
        description=(
            "Parses a .utrace file and returns a structured summary.\n"
            "Use insights-list-traces to find available trace files.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli insights-analyze MyTrace.utrace\n"
            "  soft-ue-cli insights-analyze D:/Traces/MyTrace.utrace --analysis-type basic_info"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ia.add_argument("trace_file", help="Path to .utrace file")
    p_ia.add_argument("--analysis-type", choices=["basic_info"], help="Analysis type (default: basic_info)")
    p_ia.set_defaults(func=cmd_insights_analyze)

    # -------------------------------------------------------------------------
    # Editor tools — Project
    # -------------------------------------------------------------------------

    p_pinfo = sub.add_parser(
        "project-info",
        help="Get project and plugin info, optionally with settings sections.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli project-info\n"
            "  soft-ue-cli project-info --section input\n"
            "  soft-ue-cli project-info --section all"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_pinfo.add_argument("--section", choices=["input", "collision", "tags", "maps", "all"],
        help="Settings section to include")
    p_pinfo.set_defaults(func=cmd_project_info)

    # -------------------------------------------------------------------------
    # Editor tools — References
    # -------------------------------------------------------------------------

    p_fr = sub.add_parser(
        "find-references",
        help="Find asset references, variable usages, or Blueprint node usages.",
        description=(
            "Types:\n"
            "  asset    — find assets that reference the given asset\n"
            "  property — find where a Blueprint variable is used (requires --variable-name)\n"
            "  node     — find Blueprint nodes by class (requires --node-class)\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli find-references asset /Game/Textures/T_Player\n"
            "  soft-ue-cli find-references property /Game/Blueprints/BP_Hero --variable-name Health\n"
            "  soft-ue-cli find-references node /Game/Blueprints/BP_Hero --node-class K2Node_CallFunction"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_fr.add_argument("type", choices=["asset", "property", "node"])
    p_fr.add_argument("asset_path", help="Asset path to search from/within")
    p_fr.add_argument("--variable-name", metavar="NAME", help="Variable name to find usages of (for type=property)")
    p_fr.add_argument("--node-class",    metavar="CLASS", help="Node class to search for (for type=node)")
    p_fr.add_argument("--function-name", metavar="NAME", help="Function name filter for CallFunction nodes")
    p_fr.add_argument("--limit",         type=int, metavar="N", help="Max results (default: 100)")
    p_fr.add_argument("--search",        metavar="PATTERN", help="Filter results by asset name (wildcards)")
    p_fr.set_defaults(func=cmd_find_references)

    # -------------------------------------------------------------------------
    # Editor tools — Scripting
    # -------------------------------------------------------------------------

    p_rps = sub.add_parser(
        "run-python-script",
        help="Execute a Python script in Unreal Editor's Python environment.",
        description=(
            "Requires PythonScriptPlugin to be enabled in the project.\n"
            "Provide either --script (inline code) or --script-path (file path), not both.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli run-python-script --script \"import unreal; print(unreal.SystemLibrary.get_engine_version())\"\n"
            "  soft-ue-cli run-python-script --script-path /path/to/my_script.py\n"
            "  soft-ue-cli run-python-script --script-path my_script.py --arguments '{\"count\": 5}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rps.add_argument("--name",        metavar="NAME", help="Name of a saved script to run (see save-script / list-scripts)")
    p_rps.add_argument("--script",      metavar="CODE", help="Inline Python code to execute")
    p_rps.add_argument("--script-path", metavar="PATH", help="Path to a Python script file")
    p_rps.add_argument("--python-paths", metavar="PATH", nargs="+", help="Additional sys.path directories")
    p_rps.add_argument("--arguments",   metavar="JSON", help="Arguments as JSON object (accessible via unreal.get_mcp_args())")
    p_rps.set_defaults(func=cmd_run_python_script)

    p_ss = sub.add_parser(
        "save-script",
        help="Save a Python script locally for later reuse.",
        description=(
            "Saves a Python script to ~/.soft-ue-bridge/scripts/<name>.py.\n"
            "Provide either --script (inline code) or --script-path (file to copy), not both.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli save-script my_setup --script \"import unreal; print('hello')\"\n"
            "  soft-ue-cli save-script my_setup --script-path /path/to/script.py"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ss.add_argument("name", help="Name for the saved script (no .py extension)")
    p_ss.add_argument("--script",      metavar="CODE", help="Inline Python code to save")
    p_ss.add_argument("--script-path", metavar="PATH", help="Path to an existing Python script to copy")
    p_ss.set_defaults(func=cmd_save_script)

    p_ls = sub.add_parser(
        "list-scripts",
        help="List all locally saved Python scripts.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ls.set_defaults(func=cmd_list_scripts)

    p_ds = sub.add_parser(
        "delete-script",
        help="Delete a locally saved Python script.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ds.add_argument("name", help="Name of the saved script to delete")
    p_ds.set_defaults(func=cmd_delete_script)

    # -------------------------------------------------------------------------
    # Editor tools — StateTree
    # -------------------------------------------------------------------------

    p_qst = sub.add_parser(
        "query-statetree",
        help="Inspect a StateTree asset (states, transitions, tasks, evaluators).",
        description=(
            "Returns a structured view of a StateTree asset including its state hierarchy,\n"
            "transitions, tasks, and evaluators. Requires UE5.1+ StateTree plugin.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli query-statetree /Game/AI/ST_EnemyBehavior\n"
            "  soft-ue-cli query-statetree /Game/AI/ST_EnemyBehavior --include states,transitions"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qst.add_argument("asset_path", help="StateTree asset path (e.g. /Game/AI/ST_EnemyBehavior)")
    p_qst.add_argument("--include", metavar="SECTION",
        help="Sections to include: states, transitions, tasks, evaluators, parameters, all (default: all)")
    p_qst.add_argument("--no-detail", action="store_true", help="Omit detailed info")
    p_qst.set_defaults(func=cmd_query_statetree)

    p_asss = sub.add_parser(
        "add-statetree-state",
        help="Add a new state to a StateTree asset.",
        description=(
            "Adds a new state node to a StateTree asset and saves it.\n"
            "Use --parent-state to nest it; omit for a root-level state.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-statetree-state /Game/AI/ST_Enemy Patrol\n"
            "  soft-ue-cli add-statetree-state /Game/AI/ST_Enemy Attack --parent-state Combat\n"
            "  soft-ue-cli add-statetree-state /Game/AI/ST_Enemy Subtasks --state-type Group"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_asss.add_argument("asset_path", help="StateTree asset path")
    p_asss.add_argument("state_name", help="Name for the new state")
    p_asss.add_argument("--state-type", choices=["State", "Group", "Linked", "Subtree"],
        help="State type (default: State)")
    p_asss.add_argument("--parent-state", metavar="NAME", help="Parent state name (creates root state if omitted)")
    p_asss.add_argument("--selection-behavior",
        choices=["None", "TryEnterState", "TrySelectChildrenInOrder", "TryFollowTransitions"],
        help="Selection behavior (default: TryEnterState)")
    p_asss.set_defaults(func=cmd_add_statetree_state)

    p_asst = sub.add_parser(
        "add-statetree-task",
        help="Add a task to a state in a StateTree asset.",
        description=(
            "Appends a task instance to an existing state in a StateTree asset.\n"
            "The task class must be a valid UStateTreeTask subclass available in the project.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-statetree-task /Game/AI/ST_Enemy Patrol UStateTreeTask_MoveTo\n"
            "  soft-ue-cli add-statetree-task /Game/AI/ST_Enemy Attack MyTask_Attack --task-name AttackPlayer"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_asst.add_argument("asset_path",  help="StateTree asset path")
    p_asst.add_argument("state_name",  help="Name of the state to add the task to")
    p_asst.add_argument("task_class",  help="Task class name (e.g. UStateTreeTask_PlayAnimation)")
    p_asst.add_argument("--task-name", metavar="NAME", help="Optional display name for the task")
    p_asst.set_defaults(func=cmd_add_statetree_task)

    p_astr = sub.add_parser(
        "add-statetree-transition",
        help="Add a transition between states in a StateTree asset.",
        description=(
            "Adds a conditional transition from one state to another in a StateTree asset.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-statetree-transition /Game/AI/ST_Enemy Patrol Attack\n"
            "  soft-ue-cli add-statetree-transition /Game/AI/ST_Enemy Attack Idle --trigger OnTaskSucceeded\n"
            "  soft-ue-cli add-statetree-transition /Game/AI/ST_Enemy Patrol Attack --priority 1"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_astr.add_argument("asset_path",   help="StateTree asset path")
    p_astr.add_argument("source_state", help="Source state name")
    p_astr.add_argument("target_state", help="Target state name")
    p_astr.add_argument("--trigger",    metavar="TRIGGER", help="Transition trigger condition")
    p_astr.add_argument("--priority",   type=int, metavar="N", help="Transition priority")
    p_astr.set_defaults(func=cmd_add_statetree_transition)

    p_rsss = sub.add_parser(
        "remove-statetree-state",
        help="Remove a state from a StateTree asset.",
        description=(
            "Removes a state and all its children from a StateTree asset and saves it.\n"
            "Any transitions pointing to this state will also be removed.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli remove-statetree-state /Game/AI/ST_Enemy Patrol\n"
            "  soft-ue-cli remove-statetree-state /Game/AI/ST_Enemy OldCombatGroup"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rsss.add_argument("asset_path", help="StateTree asset path")
    p_rsss.add_argument("state_name", help="Name of the state to remove")
    p_rsss.set_defaults(func=cmd_remove_statetree_state)

    # -------------------------------------------------------------------------
    # Editor tools — Widget
    # -------------------------------------------------------------------------

    p_iwb = sub.add_parser(
        "inspect-widget-blueprint",
        help="Inspect a Widget Blueprint's widget hierarchy and bindings.",
        description=(
            "Returns the full widget tree of a Widget Blueprint (UMG), including slot\n"
            "properties, property bindings, and animations.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli inspect-widget-blueprint /Game/UI/WBP_MainMenu\n"
            "  soft-ue-cli inspect-widget-blueprint /Game/UI/WBP_HUD --include-defaults --depth-limit 3\n"
            "  soft-ue-cli inspect-widget-blueprint /Game/UI/WBP_HUD --no-bindings"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_iwb.add_argument("asset_path", help="Widget Blueprint asset path (e.g. /Game/UI/WBP_MainMenu)")
    p_iwb.add_argument("--include-defaults", action="store_true", help="Include widget property default values")
    p_iwb.add_argument("--depth-limit",      type=int, metavar="N", help="Max hierarchy depth (-1 = unlimited)")
    p_iwb.add_argument("--no-bindings",      action="store_true", help="Exclude property binding information")
    p_iwb.set_defaults(func=cmd_inspect_widget_blueprint)

    # -------------------------------------------------------------------------
    # Editor tools — Write
    # -------------------------------------------------------------------------

    p_sap = sub.add_parser(
        "set-asset-property",
        help="Set a property on a Blueprint or asset default using UE reflection.",
        description=(
            "Sets a UPROPERTY on a Blueprint CDO or asset using reflection.\n"
            "Supports nested paths (e.g. Stats.MaxHealth), array indices (e.g. Items[0]),\n"
            "TArray, TMap, TSet, object references, structs, and vectors.\n\n"
            "Use --component-name to target a specific Blueprint component.\n"
            "Use --clear-override to revert a component property to its default.\n\n"
            "For setting properties on runtime actors, use 'set-property'.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli set-asset-property /Game/BP_Player Health 100\n"
            "  soft-ue-cli set-asset-property /Game/BP_Player Stats.MaxHealth 200\n"
            "  soft-ue-cli set-asset-property /Game/BP_Player Intensity --component-name PointLight 5000\n"
            "  soft-ue-cli set-asset-property /Game/BP_Player Intensity --component-name PointLight --clear-override"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_sap.add_argument("asset_path",    help="Asset path (e.g. /Game/Blueprints/BP_Player)")
    p_sap.add_argument("property_path", help="Dot-separated property path (e.g. Health, Stats.MaxHealth)")
    p_sap.add_argument("value",         nargs="?", default=None,
        help="Value as JSON (number, string, boolean, array, object). Required unless --clear-override.")
    p_sap.add_argument("--component-name", metavar="NAME", help="Component name for component property overrides")
    p_sap.add_argument("--clear-override", action="store_true", help="Revert property to default value")
    p_sap.set_defaults(func=cmd_set_asset_property)

    p_ac = sub.add_parser(
        "add-component",
        help="Add a component to an actor in the running level.",
        description=(
            "Adds a new component instance to a runtime actor using UE reflection.\n"
            "The actor must exist in the current game world (use query-level to find actors).\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-component BP_Hero PointLightComponent\n"
            "  soft-ue-cli add-component BP_Hero StaticMeshComponent --component-name WeaponMesh\n"
            "  soft-ue-cli add-component BP_Hero AudioComponent --attach-to RootComponent"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ac.add_argument("actor_name",      help="Actor name or label")
    p_ac.add_argument("component_class", help="Component class (e.g. PointLightComponent, StaticMeshComponent)")
    p_ac.add_argument("--component-name", metavar="NAME", help="Name for the new component")
    p_ac.add_argument("--attach-to",      metavar="NAME", help="Parent component to attach to")
    p_ac.set_defaults(func=cmd_add_component)

    p_aw = sub.add_parser(
        "add-widget",
        help="Add a widget to a Widget Blueprint.",
        description=(
            "Adds a new widget (UMG element) to a Widget Blueprint's designer hierarchy.\n"
            "Use --parent-widget to nest it under an existing widget; omit to add at root.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-widget /Game/UI/WBP_HUD TextBlock HealthLabel\n"
            "  soft-ue-cli add-widget /Game/UI/WBP_HUD ProgressBar HealthBar --parent-widget HealthPanel\n"
            "  soft-ue-cli add-widget /Game/UI/WBP_HUD Button CloseButton"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_aw.add_argument("asset_path",   help="WidgetBlueprint asset path")
    p_aw.add_argument("widget_class", help="Widget class (e.g. Button, TextBlock, Image, ProgressBar)")
    p_aw.add_argument("widget_name",  help="Name for the new widget")
    p_aw.add_argument("--parent-widget", metavar="NAME", help="Parent widget name (adds to root if omitted)")
    p_aw.set_defaults(func=cmd_add_widget)

    p_adr = sub.add_parser(
        "add-datatable-row",
        help="Add a row to a DataTable asset.",
        description=(
            "Appends a new named row to a DataTable asset and saves it.\n"
            "The --row-data JSON object must have keys matching the row struct's property names.\n"
            "Use query-asset --asset-path to inspect the struct layout first.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli add-datatable-row /Game/Data/DT_Items Sword\n"
            "  soft-ue-cli add-datatable-row /Game/Data/DT_Items Sword --row-data '{\"Damage\": 50, \"Name\": \"Iron Sword\"}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_adr.add_argument("asset_path", help="DataTable asset path")
    p_adr.add_argument("row_name",   help="Name for the new row")
    p_adr.add_argument("--row-data", metavar="JSON",
        help="Row data as JSON object with property names matching the row struct")
    p_adr.set_defaults(func=cmd_add_datatable_row)

    p_agn = sub.add_parser(
        "add-graph-node",
        help="Add a node to a Blueprint event graph or Material graph.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli add-graph-node /Game/BP_Player K2Node_CallFunction\n"
            "  soft-ue-cli add-graph-node /Game/M_Rock MaterialExpressionAdd --position 100,200"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_agn.add_argument("asset_path", help="Blueprint or Material asset path")
    p_agn.add_argument("node_class", help="Node class (e.g. K2Node_CallFunction, MaterialExpressionAdd)")
    p_agn.add_argument("--graph-name",      metavar="NAME", help="Graph name for Blueprints (default: EventGraph)")
    p_agn.add_argument("--position",        metavar="X,Y", help="Node position as X,Y")
    p_agn.add_argument("--no-auto-position", action="store_true", help="Disable automatic positioning")
    p_agn.add_argument("--connect-to-node", metavar="GUID", help="Node GUID to position relative to")
    p_agn.add_argument("--connect-to-pin",  metavar="PIN", help="Pin name to position relative to")
    p_agn.add_argument("--properties",      metavar="JSON", help="Node properties as JSON object")
    p_agn.set_defaults(func=cmd_add_graph_node)

    p_rgn = sub.add_parser(
        "remove-graph-node",
        help="Remove a node from a Blueprint or Material graph.",
        description=(
            "Deletes a node from a Blueprint event graph or Material node graph.\n"
            "Use query-blueprint-graph to get the node GUID before removing.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli remove-graph-node /Game/Blueprints/BP_Player {node-guid}\n"
            "  soft-ue-cli remove-graph-node /Game/Materials/M_Rock Multiply_0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rgn.add_argument("asset_path", help="Blueprint or Material asset path")
    p_rgn.add_argument("node_id",    help="Node GUID (Blueprint) or expression name (Material)")
    p_rgn.set_defaults(func=cmd_remove_graph_node)

    p_cgp = sub.add_parser(
        "connect-graph-pins",
        help="Connect two pins in a Blueprint or Material graph.",
        description=(
            "Creates a wire between an output pin on one node and an input pin on another.\n"
            "Use query-blueprint-graph to find node GUIDs and pin names.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli connect-graph-pins /Game/BP_Player {src-guid} then {dst-guid} execute\n"
            "  soft-ue-cli connect-graph-pins /Game/Materials/M_Rock Add_0 Output Multiply_0 A"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cgp.add_argument("asset_path",  help="Blueprint or Material asset path")
    p_cgp.add_argument("source_node", help="Source node GUID or expression name")
    p_cgp.add_argument("source_pin",  help="Source pin name")
    p_cgp.add_argument("target_node", help="Target node GUID or expression name")
    p_cgp.add_argument("target_pin",  help="Target pin name")
    p_cgp.set_defaults(func=cmd_connect_graph_pins)

    p_dgp = sub.add_parser(
        "disconnect-graph-pin",
        help="Disconnect all connections from a pin in a Blueprint or Material graph.",
        description=(
            "Removes all wires connected to a specific pin on a node.\n"
            "Use query-blueprint-graph to find node GUIDs and pin names.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli disconnect-graph-pin /Game/Blueprints/BP_Player {node-guid} execute\n"
            "  soft-ue-cli disconnect-graph-pin /Game/Materials/M_Rock Add_0 A"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_dgp.add_argument("asset_path", help="Blueprint or Material asset path")
    p_dgp.add_argument("node_id",    help="Node GUID or expression name")
    p_dgp.add_argument("pin_name",   help="Pin name to disconnect")
    p_dgp.set_defaults(func=cmd_disconnect_graph_pin)

    p_snp = sub.add_parser(
        "set-node-position",
        help="Batch-set editor positions for nodes in a Material, Blueprint, or AnimBlueprint graph.",
        description=(
            "Set node positions by GUID. All moves happen in a single undo transaction.\n"
            "Use query-material or query-blueprint-graph to get node GUIDs.\n\n"
            "EXAMPLES:\n"
            '  soft-ue-cli set-node-position /Game/Materials/M_Rock --positions \'[{"guid":"AABB...","x":-400,"y":0}]\'\n'
            '  soft-ue-cli set-node-position /Game/BP_Player --graph-name EventGraph --positions \'[{"guid":"1122...","x":100,"y":200}]\''
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_snp.add_argument("asset_path", help="Material, Blueprint, or AnimBlueprint asset path")
    p_snp.add_argument("--positions", required=True, metavar="JSON", help="JSON array of {guid, x, y} entries")
    p_snp.add_argument("--graph-name", metavar="NAME", help="Graph name for Blueprint/Anim (default: EventGraph)")
    p_snp.set_defaults(func=cmd_set_node_position)

    p_cas = sub.add_parser(
        "create-asset",
        help="Create a new asset in the Content Browser.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli create-asset /Game/Blueprints/BP_NewActor Blueprint --parent-class Actor\n"
            "  soft-ue-cli create-asset /Game/Materials/M_NewMat Material\n"
            "  soft-ue-cli create-asset /Game/Data/DT_Items DataTable --row-struct /Game/Structs/S_Item"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cas.add_argument("asset_path",  help="Full asset path including name (e.g. /Game/Blueprints/BP_NewActor)")
    p_cas.add_argument("asset_class", help="Asset class (e.g. Blueprint, Material, DataTable, WidgetBlueprint)")
    p_cas.add_argument("--parent-class", metavar="CLASS", help="Parent class for Blueprints (e.g. Actor, Character)")
    p_cas.add_argument("--row-struct",   metavar="PATH", help="Row struct path for DataTables")
    p_cas.set_defaults(func=cmd_create_asset)

    # -------------------------------------------------------------------------
    # Knowledge
    # -------------------------------------------------------------------------

    p_k = sub.add_parser(
        "query-ue-knowledge",
        help="Query the knowledge server for UE API docs, tutorials, and workflow skills.",
        description="Coming soon. Follow https://github.com/softdaddy-o/soft-ue-cli for updates.",
    )
    p_k.add_argument("query", nargs="?", default=None, help="Natural language question about UE API or behavior")
    p_k.set_defaults(func=cmd_knowledge)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    if args.server:
        os.environ["SOFT_UE_BRIDGE_URL"] = args.server
    if args.timeout:
        os.environ["SOFT_UE_BRIDGE_TIMEOUT"] = str(args.timeout)

    args.func(args)


if __name__ == "__main__":
    main()
