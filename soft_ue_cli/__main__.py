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


def _fix_msys_asset_path(path: str) -> str:
    """Reverse MSYS/Git Bash path mangling for UE asset paths.

    Git Bash on Windows converts /Game/Foo to C:/Program Files/Git/Game/Foo.
    Detect and reverse this for common UE mount points.
    """
    if not path or "/" not in path:
        return path
    # Known UE asset mount points that MSYS will mangle
    mount_points = ("/Game/", "/Engine/", "/Script/", "/Temp/", "/Niagara/", "/Paper2D/")
    for mp in mount_points:
        # MSYS converts /Game/ → C:/Program Files/Git/Game/ (or similar Git install path)
        idx = path.find(mp[1:])  # Find "Game/" anywhere in the mangled path
        if idx > 0 and path[0] != "/":
            return mp + path[idx + len(mp) - 1:]
    return path


def _print_json(data: object) -> None:
    print(json.dumps(data, indent=2, ensure_ascii=False))


def _run_tool(tool_name: str, arguments: dict) -> dict:
    """Call a bridge tool, handle errors with nudge hints, and track streak.

    On success: records the daily streak, prints testimonial nudge if earned, returns result.
    On error: prints to stderr, appends bug nudge for unexpected errors, exits.
    """
    from .errors import BridgeError, ErrorKind, format_bug_nudge

    try:
        result = call_tool(tool_name, arguments)
    except BridgeError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        if exc.kind == ErrorKind.UNEXPECTED:
            print(format_bug_nudge(exc.tool_name, exc.message), file=sys.stderr)
        sys.exit(1)

    # Record daily streak on success (best-effort, never fail the command)
    try:
        from .streak import record_success, should_nudge_testimonial, mark_nudged

        record_success(tool_name)
        if should_nudge_testimonial():
            print(
                '\nEnjoying soft-ue-cli? Share your experience:'
                ' soft-ue-cli submit-testimonial --message "..."',
                file=sys.stderr,
            )
            mark_nudged()
    except Exception:
        pass

    return result


def _parse_vector(csv: str) -> list[float]:
    """Parse a comma-separated string like '1.0,2.0,3.0' into a list of floats."""
    try:
        return [float(x) for x in csv.split(",")]
    except ValueError:
        print(f"error: expected comma-separated numbers, got '{csv}'", file=sys.stderr)
        sys.exit(1)


def _parse_int_list(csv: str) -> list[int]:
    """Parse a comma-separated string like '0,0,1920,1080' into a list of ints."""
    try:
        return [int(x) for x in csv.split(",")]
    except ValueError:
        print(f"error: expected comma-separated integers, got '{csv}'", file=sys.stderr)
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
    _print_json(_run_tool("spawn-actor", arguments))


def cmd_set_viewport_camera(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.preset:
        arguments["preset"] = args.preset
    if args.location:
        arguments["location"] = _parse_vector(args.location)
    if args.rotation:
        arguments["rotation"] = _parse_vector(args.rotation)
    if args.ortho_width is not None:
        arguments["ortho_width"] = args.ortho_width
    _print_json(_run_tool("set-viewport-camera", arguments))


def cmd_batch_spawn_actors(args: argparse.Namespace) -> None:
    arguments: dict = {"actors": _parse_json_arg(args.actors, "--actors")}
    _print_json(_run_tool("batch-spawn-actors", arguments))


def cmd_batch_modify_actors(args: argparse.Namespace) -> None:
    arguments: dict = {"modifications": _parse_json_arg(args.modifications, "--modifications")}
    _print_json(_run_tool("batch-modify-actors", arguments))


def cmd_batch_delete_actors(args: argparse.Namespace) -> None:
    arguments: dict = {"actors": _parse_json_arg(args.actors, "--actors")}
    _print_json(_run_tool("batch-delete-actors", arguments))


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
    if args.include_properties or args.property_filter:
        arguments["include_properties"] = True
    if args.property_filter:
        arguments["property_filter"] = args.property_filter
    if args.include_foliage:
        arguments["include_foliage"] = True
    if args.include_grass:
        arguments["include_grass"] = True
    _print_json(_run_tool("query-level", arguments))


def cmd_call_function(args: argparse.Namespace) -> None:
    arguments: dict = {
        "actor_name": args.actor_name,
        "function_name": args.function_name,
    }
    if args.args:
        arguments["args"] = _parse_json_arg(args.args, "--args")
    _print_json(_run_tool("call-function", arguments))


def cmd_set_property(args: argparse.Namespace) -> None:
    _print_json(
        _run_tool(
            "set-property",
            {
                "actor_name": args.actor_name,
                "property_name": args.property_name,
                "value": args.value,
            },
        )
    )


def cmd_get_property(args: argparse.Namespace) -> None:
    _print_json(
        _run_tool(
            "get-property",
            {
                "actor_name": args.actor_name,
                "property_name": args.property_name,
            },
        )
    )


def cmd_get_logs(args: argparse.Namespace) -> None:
    arguments: dict = {"lines": args.lines}
    if args.filter:
        arguments["filter"] = args.filter
    if args.category:
        arguments["category"] = args.category
    result = _run_tool("get-logs", arguments)
    if args.raw:
        for line in result.get("lines", []):
            print(line)
    else:
        _print_json(result)


def cmd_get_console_var(args: argparse.Namespace) -> None:
    _print_json(_run_tool("get-console-var", {"name": args.name}))


def cmd_set_console_var(args: argparse.Namespace) -> None:
    _print_json(_run_tool("set-console-var", {"name": args.name, "value": args.value}))


# -- Editor tool handlers ------------------------------------------------------


def cmd_class_hierarchy(args: argparse.Namespace) -> None:
    arguments: dict = {"class_name": args.class_name}
    if args.direction:
        arguments["direction"] = args.direction
    if args.depth is not None:
        arguments["depth"] = args.depth
    if args.no_blueprints:
        arguments["include_blueprints"] = False
    _print_json(_run_tool("get-class-hierarchy", arguments))


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
    _print_json(_run_tool("query-asset", arguments))


def cmd_delete_asset(args: argparse.Namespace) -> None:
    _print_json(_run_tool("delete-asset", {"asset_path": args.asset_path}))


def cmd_get_asset_diff(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.scm_type:
        arguments["scm_type"] = args.scm_type
    if args.base_revision:
        arguments["base_revision"] = args.base_revision
    _print_json(_run_tool("get-asset-diff", arguments))


def cmd_get_asset_preview(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.resolution is not None:
        arguments["resolution"] = args.resolution
    if args.format:
        arguments["format"] = args.format
    if args.output:
        arguments["output"] = args.output
    _print_json(_run_tool("get-asset-preview", arguments))


def cmd_open_asset(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.asset_path:
        arguments["asset_path"] = args.asset_path
    if args.window_name:
        arguments["window_name"] = args.window_name
    if args.no_focus:
        arguments["bring_to_front"] = False
    _print_json(_run_tool("open-asset", arguments))


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
    _print_json(_run_tool("query-blueprint", arguments))


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
    _print_json(_run_tool("query-blueprint-graph", arguments))


def cmd_inspect_uasset(args: argparse.Namespace) -> None:
    from .uasset import UAssetError, inspect_uasset

    sections_arg = getattr(args, "sections", "summary")
    sections = [part.strip() for part in sections_arg.split(",") if part.strip()]

    try:
        result = inspect_uasset(args.file_path, sections=sections)
    except (FileNotFoundError, UAssetError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)

    if args.format == "table":
        _print_inspect_table(result)
        return

    _print_json(result)


def cmd_diff_uasset(args: argparse.Namespace) -> None:
    from .uasset import UAssetError, diff_uasset

    sections_arg = getattr(args, "sections", "summary")
    sections = [part.strip() for part in sections_arg.split(",") if part.strip()]

    try:
        result = diff_uasset(args.left_file, args.right_file, sections=sections)
    except (FileNotFoundError, UAssetError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)

    if args.format == "table":
        _print_uasset_diff_table(result)
        return

    _print_json(result)


def _print_inspect_table(data: dict) -> None:
    print(f"Asset: {data.get('name', 'Unknown')}")
    print(f"  File:      {data.get('file', '')}")
    print(f"  UE:        {data.get('ue_version', '?')}")
    print(f"  Class:     {data.get('asset_class', '?')}")
    print(f"  Parent:    {data.get('parent_class', '?')}")
    print(f"  Type:      {data.get('blueprint_type', '?')}")

    for section in ("variables", "functions", "components"):
        if section not in data:
            continue
        payload = data[section]
        print(f"\n  {section.title()} ({payload.get('count', 0)}) [fidelity: {payload.get('fidelity', '?')}]")
        for item in payload.get("items", []):
            extra = item.get("type", item.get("class", ""))
            line = f"    - {item.get('name', '?')}"
            if extra:
                line += f" ({extra})"
            print(line)

    if "events" in data:
        payload = data["events"]
        total = payload.get("event_count", 0) + payload.get("custom_event_count", 0)
        print(f"\n  Events ({total}) [fidelity: {payload.get('fidelity', '?')}]")
        for item in payload.get("events", []):
            print(f"    - {item.get('name', '?')}")
        for item in payload.get("custom_events", []):
            print(f"    - {item.get('name', '?')} (custom)")


def _print_uasset_diff_table(data: dict) -> None:
    print(f"Left:  {data.get('left_file', '')}")
    print(f"Right: {data.get('right_file', '')}")
    print(f"Has changes: {data.get('has_changes', False)}")
    print(f"Total changes: {data.get('total_changes', 0)}")

    changes = data.get("changes", {})
    if "summary" in changes:
        summary = changes["summary"]
        print(f"\n  Summary ({summary.get('change_count', 0)})")
        for field, payload in summary.get("modified", {}).items():
            print(f"    - {field}: {payload.get('old')} -> {payload.get('new')}")

    for section in ("variables", "functions", "components"):
        if section not in changes:
            continue
        payload = changes[section]
        print(f"\n  {section.title()} ({payload.get('change_count', 0)})")
        for item in payload.get("added", []):
            print(f"    + {item.get('name', '?')}")
        for item in payload.get("removed", []):
            print(f"    - {item.get('name', '?')}")
        for item in payload.get("modified", []):
            print(f"    ~ {item.get('name', '?')}")

    if "events" in changes:
        payload = changes["events"]
        print(f"\n  Events ({payload.get('change_count', 0)})")
        for item in payload.get("added", []):
            print(f"    + {item.get('name', '?')}")
        for item in payload.get("removed", []):
            print(f"    - {item.get('name', '?')}")
        for item in payload.get("added_custom", []):
            print(f"    + {item.get('name', '?')} (custom)")
        for item in payload.get("removed_custom", []):
            print(f"    - {item.get('name', '?')} (custom)")


def cmd_build_and_relaunch(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.config:
        arguments["build_config"] = args.config
    if args.skip_relaunch:
        arguments["skip_relaunch"] = True
    result = _run_tool("build-and-relaunch", arguments)

    if not args.wait:
        _print_json(result)
        return

    _wait_for_build_and_relaunch(result, skip_relaunch=args.skip_relaunch)


def _wait_for_build_and_relaunch(
    initiation_result: dict,
    *,
    skip_relaunch: bool,
    poll_interval: float = 3.0,
    build_timeout: float = 600.0,
    relaunch_timeout: float = 120.0,
) -> None:
    """Poll for build completion and (optionally) editor relaunch."""
    import json as _json
    import time
    from pathlib import Path

    from .client import health_check

    status_path = Path(initiation_result.get("build_status_path", ""))
    log_path = Path(initiation_result.get("build_log_path", ""))

    if not status_path.name:
        # Fallback: old plugin without status file support
        _print_json(initiation_result)
        return

    project = initiation_result.get("project", "")
    print(f"Build initiated for {project}. Waiting for completion...", file=sys.stderr)

    # --- Phase 1: wait for build to finish (status file appears) ---
    start = time.monotonic()
    build_status: dict = {}
    while time.monotonic() - start < build_timeout:
        time.sleep(poll_interval)
        if status_path.exists():
            try:
                build_status = _json.loads(status_path.read_text().strip())
            except (_json.JSONDecodeError, OSError):
                continue
            break
    else:
        print(
            f"error: build did not finish within {build_timeout:.0f}s",
            file=sys.stderr,
        )
        sys.exit(1)

    build_elapsed = time.monotonic() - start
    build_success = build_status.get("success", False)

    if not build_success:
        # Read compiler output from log
        errors = ""
        if log_path.exists():
            try:
                errors = log_path.read_text(errors="replace")
            except OSError:
                pass
        result: dict = {
            "success": False,
            "status": "build_failed",
            "exit_code": build_status.get("exit_code", 1),
            "build_time_seconds": round(build_elapsed, 1),
            "message": "Build failed. See build_output for compiler errors.",
        }
        if errors:
            result["build_output"] = errors
        _print_json(result)
        sys.exit(1)

    print(
        f"Build succeeded in {build_elapsed:.0f}s.",
        file=sys.stderr,
    )

    if skip_relaunch:
        _print_json({
            "success": True,
            "status": "build_succeeded",
            "build_time_seconds": round(build_elapsed, 1),
            "message": "Build completed successfully (editor not relaunched).",
        })
        return

    # --- Phase 2: wait for editor to come back (bridge health) ---
    print("Waiting for editor to relaunch...", file=sys.stderr)
    relaunch_start = time.monotonic()
    while time.monotonic() - relaunch_start < relaunch_timeout:
        time.sleep(poll_interval)
        hc = health_check()
        if "error" not in hc and hc.get("running"):
            total = time.monotonic() - start
            _print_json({
                "success": True,
                "status": "ready",
                "build_time_seconds": round(build_elapsed, 1),
                "total_time_seconds": round(total, 1),
                "message": f"Build succeeded and editor is ready ({total:.0f}s total).",
            })
            return

    total = time.monotonic() - start
    _print_json({
        "success": True,
        "status": "build_succeeded_relaunch_pending",
        "build_time_seconds": round(build_elapsed, 1),
        "total_time_seconds": round(total, 1),
        "message": f"Build succeeded but editor did not respond within {relaunch_timeout:.0f}s. It may still be loading.",
    })


def cmd_trigger_live_coding(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if not args.no_wait:
        arguments["wait_for_completion"] = True
    _print_json(_run_tool("trigger-live-coding", arguments))


def cmd_capture_screenshot(args: argparse.Namespace) -> None:
    arguments: dict = {"mode": args.mode}
    if args.window_name:
        arguments["window_name"] = args.window_name
    if args.region:
        arguments["region"] = _parse_int_list(args.region)
    if args.format:
        arguments["format"] = args.format
    if args.output:
        arguments["output"] = args.output
    _print_json(_run_tool("capture-screenshot", arguments))


def cmd_capture_viewport(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.source:
        arguments["source"] = args.source
    if args.format:
        arguments["format"] = args.format
    if args.output:
        arguments["output"] = args.output
    _print_json(_run_tool("capture-viewport", arguments))


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
    if args.parent_chain:
        arguments["parent_chain"] = True
    _print_json(_run_tool("query-material", arguments))


def cmd_query_mpc(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.action:
        arguments["action"] = args.action
    if args.parameter_name:
        arguments["parameter_name"] = args.parameter_name
    if args.value is not None:
        val = args.value.strip()
        if val.startswith("["):
            arguments["value"] = _parse_json_arg(val, "--value")
        else:
            try:
                arguments["value"] = float(val)
            except ValueError:
                print(f"error: expected a number or JSON array, got '{val}'", file=sys.stderr)
                sys.exit(1)
    if args.world:
        arguments["world"] = args.world
    _print_json(_run_tool("query-mpc", arguments))


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
    _print_json(_run_tool("pie-session", arguments))


def cmd_trigger_input(args: argparse.Namespace) -> None:
    arguments: dict = {"action": args.action}
    if args.player_index is not None:
        arguments["player_index"] = args.player_index
    if args.key:
        arguments["key"] = args.key
    if args.action_name:
        arguments["action_name"] = args.action_name
    if args.release:
        arguments["pressed"] = False
    if args.target:
        arguments["target"] = _parse_vector(args.target)
    if args.target_actor:
        arguments["target_actor"] = args.target_actor
    _print_json(_run_tool("trigger-input", arguments))


def cmd_insights_capture(args: argparse.Namespace) -> None:
    arguments: dict = {"action": args.action}
    if args.channels:
        arguments["channels"] = args.channels.split(",")
    if args.output_file:
        arguments["output_file"] = args.output_file
    _print_json(_run_tool("insights-capture", arguments))


def cmd_insights_list_traces(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.directory:
        arguments["directory"] = args.directory
    _print_json(_run_tool("insights-list-traces", arguments))


def cmd_insights_analyze(args: argparse.Namespace) -> None:
    arguments: dict = {"trace_file": args.trace_file}
    if args.analysis_type:
        arguments["analysis_type"] = args.analysis_type
    _print_json(_run_tool("insights-analyze", arguments))


def cmd_project_info(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.section:
        arguments["section"] = args.section
    _print_json(_run_tool("get-project-info", arguments))


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
    _print_json(_run_tool("find-references", arguments))


_SCRIPTS_DIR = Path.home() / ".soft-ue-bridge" / "scripts"

_VALID_SCRIPT_NAME_CHARS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")


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
    _print_json(_run_tool("run-python-script", arguments))


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
            scripts.append(
                {
                    "name": f.stem,
                    "path": str(f),
                    "size": stat.st_size,
                    "modified": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(stat.st_mtime)),
                }
            )
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
    _print_json(_run_tool("query-statetree", arguments))


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
    _print_json(_run_tool("add-statetree-state", arguments))


def cmd_add_statetree_task(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "state_name": args.state_name,
        "task_class": args.task_class,
    }
    if args.task_name:
        arguments["task_name"] = args.task_name
    _print_json(_run_tool("add-statetree-task", arguments))


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
    _print_json(_run_tool("add-statetree-transition", arguments))


def cmd_remove_statetree_state(args: argparse.Namespace) -> None:
    _print_json(
        _run_tool(
            "remove-statetree-state",
            {
                "asset_path": args.asset_path,
                "state_name": args.state_name,
            },
        )
    )


def cmd_inspect_widget_blueprint(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.include_defaults:
        arguments["include_defaults"] = True
    if args.depth_limit is not None:
        arguments["depth_limit"] = args.depth_limit
    if args.no_bindings:
        arguments["include_bindings"] = False
    _print_json(_run_tool("inspect-widget-blueprint", arguments))


def cmd_inspect_runtime_widgets(args: argparse.Namespace) -> None:
    arguments: dict = {}
    if args.filter:
        arguments["filter"] = args.filter
    if args.class_filter:
        arguments["class_filter"] = args.class_filter
    if args.depth_limit is not None:
        arguments["depth_limit"] = args.depth_limit
    if args.include_slate:
        arguments["include_slate"] = True
    if args.pie_index is not None:
        arguments["pie_index"] = args.pie_index
    if args.no_geometry:
        arguments["include_geometry"] = False
    if args.no_properties:
        arguments["include_properties"] = False
    if args.root_widget:
        arguments["root_widget"] = args.root_widget
    _print_json(_run_tool("inspect-runtime-widgets", arguments))


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
    _print_json(_run_tool("set-asset-property", arguments))


def cmd_add_component(args: argparse.Namespace) -> None:
    arguments: dict = {
        "actor_name": args.actor_name,
        "component_class": args.component_class,
    }
    if args.component_name:
        arguments["component_name"] = args.component_name
    if args.attach_to:
        arguments["attach_to"] = args.attach_to
    _print_json(_run_tool("add-component", arguments))


def cmd_add_widget(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "widget_class": args.widget_class,
        "widget_name": args.widget_name,
    }
    if args.parent_widget:
        arguments["parent_widget"] = args.parent_widget
    _print_json(_run_tool("add-widget", arguments))


def cmd_add_datatable_row(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "row_name": args.row_name,
    }
    if args.row_data:
        arguments["row_data"] = _parse_json_arg(args.row_data, "--row-data")
    _print_json(_run_tool("add-datatable-row", arguments))


def cmd_add_graph_node(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "node_class": args.node_class,
    }
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    if args.position:
        arguments["position"] = _parse_int_list(args.position)
    if args.no_auto_position:
        arguments["auto_position"] = False
    if args.connect_to_node:
        arguments["connect_to_node"] = args.connect_to_node
    if args.connect_to_pin:
        arguments["connect_to_pin"] = args.connect_to_pin
    if args.properties:
        arguments["properties"] = _parse_json_arg(args.properties, "--properties")
    _print_json(_run_tool("add-graph-node", arguments))


def cmd_modify_interface(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "action": args.action,
        "interface_class": args.interface_class,
    }
    _print_json(_run_tool("modify-interface", arguments))


def cmd_remove_graph_node(args: argparse.Namespace) -> None:
    _print_json(
        _run_tool(
            "remove-graph-node",
            {
                "asset_path": args.asset_path,
                "node_id": args.node_id,
            },
        )
    )


def cmd_connect_graph_pins(args: argparse.Namespace) -> None:
    _print_json(
        _run_tool(
            "connect-graph-pins",
            {
                "asset_path": args.asset_path,
                "source_node": args.source_node,
                "source_pin": args.source_pin,
                "target_node": args.target_node,
                "target_pin": args.target_pin,
            },
        )
    )


def cmd_disconnect_graph_pin(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "node_id": args.node_id,
        "pin_name": args.pin_name,
    }
    if args.target_node:
        arguments["target_node"] = args.target_node
    if args.target_pin:
        arguments["target_pin"] = args.target_pin
    _print_json(_run_tool("disconnect-graph-pin", arguments))


def cmd_set_node_position(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "positions": _parse_json_arg(args.positions, "--positions"),
    }
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    _print_json(_run_tool("set-node-position", arguments))


def cmd_create_asset(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "asset_class": args.asset_class,
    }
    if args.parent_class:
        arguments["parent_class"] = args.parent_class
    if args.skeleton:
        arguments["skeleton"] = args.skeleton
    if args.row_struct:
        arguments["row_struct"] = args.row_struct
    if args.template:
        arguments["template_path"] = args.template
    _print_json(_run_tool("create-asset", arguments))


def cmd_save_asset(args: argparse.Namespace) -> None:
    arguments: dict = {"asset_path": args.asset_path}
    if args.checkout:
        arguments["checkout"] = True
    _print_json(_run_tool("save-asset", arguments))


def cmd_compile_blueprint(args: argparse.Namespace) -> None:
    _print_json(_run_tool("compile-blueprint", {"asset_path": args.asset_path}))


def cmd_compile_material(args: argparse.Namespace) -> None:
    _print_json(_run_tool("compile-material", {"asset_path": args.asset_path}))


def cmd_insert_graph_node(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "node_class": args.node_class,
        "source_node": args.source_node,
        "source_pin": args.source_pin,
        "target_node": args.target_node,
        "target_pin": args.target_pin,
    }
    if args.graph_name:
        arguments["graph_name"] = args.graph_name
    if args.new_input_pin:
        arguments["new_input_pin"] = args.new_input_pin
    if args.new_output_pin:
        arguments["new_output_pin"] = args.new_output_pin
    if args.properties:
        arguments["properties"] = _parse_json_arg(args.properties, "--properties")
    _print_json(_run_tool("insert-graph-node", arguments))


def cmd_set_node_property(args: argparse.Namespace) -> None:
    arguments: dict = {
        "asset_path": args.asset_path,
        "node_guid": args.node_guid,
        "properties": _parse_json_arg(args.properties, "--properties"),
    }
    _print_json(_run_tool("set-node-property", arguments))


def _gather_system_info() -> str:
    """Collect CLI version, Python, OS, and bridge status for bug reports."""
    import importlib.metadata
    import platform

    try:
        cli_version = importlib.metadata.version("soft-ue-cli")
    except importlib.metadata.PackageNotFoundError:
        cli_version = "unknown"
    try:
        bridge = "reachable" if "error" not in health_check() else "unreachable"
    except Exception:
        bridge = "unreachable"

    return (
        "## System Information\n"
        f"- CLI version: {cli_version}\n"
        f"- Python: {platform.python_version()}\n"
        f"- OS: {platform.platform()}\n"
        f"- Bridge: {bridge}"
    )


def cmd_report_bug(args: argparse.Namespace) -> None:
    from .github import create_issue

    sections = [f"## Description\n{args.description}"]
    if args.steps:
        sections.append(f"## Steps to Reproduce\n{args.steps}")
    if args.expected:
        sections.append(f"## Expected Behavior\n{args.expected}")
    if args.actual:
        sections.append(f"## Actual Behavior\n{args.actual}")
    if not args.no_system_info:
        sections.append(_gather_system_info())

    body = "\n\n".join(sections)
    labels = ["bug"]
    if args.severity:
        labels.append(args.severity)

    result = create_issue(args.title, body, labels)
    _print_json(result)


def cmd_request_feature(args: argparse.Namespace) -> None:
    from .github import create_issue

    sections = [f"## Description\n{args.description}"]
    if args.use_case:
        sections.append(f"## Use Case\n{args.use_case}")

    body = "\n\n".join(sections)
    labels = [args.priority]

    result = create_issue(args.title, body, labels)
    _print_json(result)


def _cmd_submit_testimonial(args: argparse.Namespace) -> None:
    from .testimonial import cmd_submit_testimonial
    cmd_submit_testimonial(args)


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
    project_path = Path(args.project_path).expanduser().resolve() if args.project_path else Path.cwd()

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
            enabled = any(p.get("Name") == "SoftUEBridge" and p.get("Enabled", False) for p in plugins)
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

    import httpx

    server_url = os.environ.get("SOFT_UE_EXPERT_SERVER_URL", "http://localhost:8000")
    api_key = os.environ.get("SOFT_UE_EXPERT_API_KEY", "dev")
    headers = {"Authorization": f"Bearer {api_key}"}

    try:
        if args.list_skills:
            resp = httpx.get(
                f"{server_url}/query/skills",
                headers=headers,
                timeout=30.0,
            )
        else:
            body: dict = {"query": args.query, "max_results": args.max_results}
            if args.type:
                body["type"] = args.type
            resp = httpx.post(
                f"{server_url}/query",
                json=body,
                headers=headers,
                timeout=30.0,
            )
        resp.raise_for_status()
        _print_json(resp.json())
    except httpx.ConnectError:
        print(
            f"error: cannot connect to knowledge server at {server_url}\n"
            "Start the knowledge server with: docker compose up",
            file=sys.stderr,
        )
        sys.exit(1)
    except httpx.HTTPStatusError as exc:
        print(f"error: HTTP {exc.response.status_code}", file=sys.stderr)
        sys.exit(1)


def cmd_skills(args: argparse.Namespace) -> None:
    from .skills import get_skill, list_skills

    if args.skills_action == "list":
        for skill in list_skills():
            print(f"{skill['name']:<24}{skill['description']}")
        return

    content = get_skill(args.skill_name)
    if content is None:
        print(f"error: skill '{args.skill_name}' not found", file=sys.stderr)
        sys.exit(1)
    print(content)


def cmd_mcp_serve(args: argparse.Namespace) -> None:
    try:
        from .mcp_server import run_server
    except ImportError:
        print(
            "error: MCP support requires the 'mcp' extra.\n"
            "Install with: pip install soft-ue-cli[mcp]",
            file=sys.stderr,
        )
        sys.exit(1)
    run_server()


def cmd_config(args: argparse.Namespace) -> None:
    action = args.config_action

    if action == "tree":
        _cmd_config_tree(args)
    elif action == "get":
        _cmd_config_get(args)
    elif action == "set":
        _cmd_config_set(args)
    elif action == "diff":
        _cmd_config_diff(args)
    elif action == "audit":
        _cmd_config_audit(args)


def _cmd_config_tree(args: argparse.Namespace) -> None:
    disc = _make_config_discovery(args)
    fmt = getattr(args, "config_format", None)
    cfg_type = getattr(args, "config_type", "Engine")
    exists_only = getattr(args, "exists_only", False)
    platform_name = getattr(args, "platform", None)

    result: dict = {"layers": []}

    if fmt in (None, "ini"):
        for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name):
            if exists_only and not layer.exists:
                continue
            result["layers"].append(
                {
                    "format": "ini",
                    "name": layer.name,
                    "path": str(layer.path),
                    "exists": layer.exists,
                    "size": layer.size,
                    "mtime": layer.mtime,
                },
            )

    if fmt in (None, "xml"):
        for layer in disc.xml_layers():
            if exists_only and not layer.exists:
                continue
            result["layers"].append(
                {
                    "format": "xml",
                    "name": layer.name,
                    "path": str(layer.path),
                    "exists": layer.exists,
                    "size": layer.size,
                    "mtime": layer.mtime,
                },
            )

    if fmt in (None, "project"):
        for project_file in disc.project_json_files():
            stat = project_file.path.stat()
            result["layers"].append(
                {
                    "format": "project",
                    "name": project_file.file_type,
                    "path": str(project_file.path),
                    "exists": True,
                    "size": stat.st_size,
                    "mtime": stat.st_mtime,
                },
            )

    _print_json(result)


def _cmd_config_get(args: argparse.Namespace) -> None:
    from .config import BuildConfigXml, ProjectJson, UeIniFile, merge_ini_layers, parse_ini_key, trace_key

    disc = _make_config_discovery(args)
    source = getattr(args, "source", None)
    layer_name = getattr(args, "layer", None)
    do_trace = getattr(args, "trace", False)
    search = getattr(args, "search", None)
    key_str = getattr(args, "key", None)
    cfg_type = getattr(args, "config_type", "Engine")
    platform_name = getattr(args, "platform", None)

    if search:
        _print_json({"matches": _search_config(disc, search, cfg_type, platform_name)})
        return

    if not key_str:
        print("error: key is required (or use --search)", file=sys.stderr)
        sys.exit(1)

    if source == "project":
        for project_file in _ordered_project_json_files(disc):
            project_json = ProjectJson.from_file(project_file.path)
            value = project_json.get(key_str)
            if value is not None:
                _print_json({"key": key_str, "value": value, "source": str(project_file.path)})
                return
        _print_json({"key": key_str, "value": None, "error": "not found"})
        return

    if source == "xml":
        section, key = _split_xml_key(key_str)
        for xml_layer in disc.xml_layers():
            if not xml_layer.exists:
                continue
            xml = BuildConfigXml.from_file(xml_layer.path)
            value = xml.get(section, key)
            if value is not None:
                _print_json({"key": key_str, "value": value, "source": str(xml_layer.path)})
                return
        _print_json({"key": key_str, "value": None, "error": "not found"})
        return

    section, key = parse_ini_key(key_str)
    ini_layers = disc.ini_layers(config_type=cfg_type, platform=platform_name)
    loaded: list[UeIniFile] = []
    for layer in ini_layers:
        if not layer.exists:
            continue
        ini = UeIniFile.from_file(layer.path)
        ini.path = layer.path
        loaded.append(ini)

    if do_trace:
        if not key:
            print("error: --trace requires [Section]Key with a key name", file=sys.stderr)
            sys.exit(1)
        _print_json({"key": key_str, "trace": trace_key(loaded, section, key)})
        return

    if layer_name:
        for layer in ini_layers:
            if layer.name != layer_name:
                continue
            if not layer.exists:
                print(f"error: layer '{layer_name}' exists in hierarchy but has no file on disk", file=sys.stderr)
                sys.exit(1)
            ini = UeIniFile.from_file(layer.path)
            if key:
                _print_json({"key": key_str, "value": ini.get(section, key), "layer": layer.name, "path": str(layer.path)})
            else:
                _print_json({"section": section, "keys": ini.items(section), "layer": layer.name, "path": str(layer.path)})
            return
        print(f"error: layer '{layer_name}' not found", file=sys.stderr)
        sys.exit(1)

    bridge_result = _try_bridge_config_get(section, key, cfg_type)
    if bridge_result is not None:
        _print_json(bridge_result)
        return

    merged = merge_ini_layers(loaded)
    if key:
        _print_json({"key": key_str, "value": merged.get(section, key), "source": "offline-merge"})
    else:
        _print_json({"section": section, "keys": merged.items(section), "source": "offline-merge"})


def _cmd_config_set(args: argparse.Namespace) -> None:
    from .config import BuildConfigXml, ProjectJson, UeIniFile, parse_ini_key

    disc = _make_config_discovery(args)
    source = getattr(args, "source", None)
    layer_name = getattr(args, "layer", None)
    key_str = args.key
    value = args.value
    cfg_type = getattr(args, "config_type", "Engine")
    platform_name = getattr(args, "platform", None)

    if source == "project":
        project_files = _ordered_project_json_files(disc)
        if not project_files:
            print("error: no .uproject or .uplugin file found", file=sys.stderr)
            sys.exit(1)
        project_json = ProjectJson.from_file(project_files[0].path)
        project_json.set(key_str, value)
        project_json.write()
        _print_json({"status": "ok", "key": key_str, "value": value, "path": str(project_files[0].path)})
        return

    if source == "xml":
        if not layer_name:
            print("error: --layer is required for XML writes", file=sys.stderr)
            sys.exit(1)
        section, key = _split_xml_key(key_str)
        for xml_layer in disc.xml_layers():
            if xml_layer.name != layer_name:
                continue
            if xml_layer.exists:
                xml = BuildConfigXml.from_file(xml_layer.path)
            else:
                import xml.etree.ElementTree as ET

                root = ET.Element("{https://www.unrealengine.com/BuildConfiguration}Configuration")
                xml = BuildConfigXml(root, path=xml_layer.path)
            xml.set(section, key, value)
            xml.write()
            _print_json({"status": "ok", "key": key_str, "value": value, "path": str(xml_layer.path)})
            return
        print(f"error: XML layer '{layer_name}' not found", file=sys.stderr)
        sys.exit(1)

    if not layer_name:
        print("error: --layer is required for INI writes", file=sys.stderr)
        sys.exit(1)

    section, key = parse_ini_key(key_str)
    if not key:
        print("error: key is required for set (got section only)", file=sys.stderr)
        sys.exit(1)

    _try_bridge_validate(section, key, cfg_type)

    for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name):
        if layer.name != layer_name:
            continue
        ini = UeIniFile.from_file(layer.path) if layer.exists else UeIniFile(path=layer.path)
        ini.set(section, key, value)
        try:
            ini.write(layer.path)
        except PermissionError:
            print(f"error: permission denied writing '{layer.path}' (file may be read-only or locked by source control)", file=sys.stderr)
            sys.exit(1)
        _print_json({"status": "ok", "key": key_str, "value": value, "layer": layer_name, "path": str(layer.path)})
        return

    print(f"error: layer '{layer_name}' not found", file=sys.stderr)
    sys.exit(1)


def _cmd_config_diff(args: argparse.Namespace) -> None:
    from .config import BuildConfigXml, UeIniFile, diff_ini_files, diff_xml_files, merge_ini_layers

    disc = _make_config_discovery(args)
    cfg_type = getattr(args, "config_type", "Engine")
    platform_name = getattr(args, "platform", None)
    layers_arg = getattr(args, "layers", None)
    platforms_arg = getattr(args, "platforms", None)
    snapshot_arg = getattr(args, "snapshot", None)
    files_arg = getattr(args, "files", None)

    if getattr(args, "audit", False):
        _cmd_config_audit(args)
        return

    if layers_arg and len(layers_arg) == 2:
        left_ini = right_ini = None
        for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name):
            if not layer.exists:
                continue
            if layer.name == layers_arg[0]:
                left_ini = UeIniFile.from_file(layer.path)
            elif layer.name == layers_arg[1]:
                right_ini = UeIniFile.from_file(layer.path)
        if left_ini is None or right_ini is None:
            missing = layers_arg[0] if left_ini is None else layers_arg[1]
            print(f"error: layer '{missing}' not found or empty", file=sys.stderr)
            sys.exit(1)
        result = diff_ini_files(left_ini, right_ini)
        result["left"] = layers_arg[0]
        result["right"] = layers_arg[1]
        _print_json(result)
        return

    if platforms_arg and len(platforms_arg) == 2:
        left_layers = disc.ini_layers(config_type=cfg_type, platform=platforms_arg[0])
        right_layers = disc.ini_layers(config_type=cfg_type, platform=platforms_arg[1])
        left_merged = merge_ini_layers([UeIniFile.from_file(layer.path) for layer in left_layers if layer.exists])
        right_merged = merge_ini_layers([UeIniFile.from_file(layer.path) for layer in right_layers if layer.exists])
        result = diff_ini_files(left_merged, right_merged)
        result["left"] = platforms_arg[0]
        result["right"] = platforms_arg[1]
        _print_json(result)
        return

    if snapshot_arg:
        import subprocess

        result_data: dict = {"snapshot": snapshot_arg, "changes": []}
        for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name):
            if not layer.exists:
                continue
            try:
                old_text = subprocess.check_output(
                    ["git", "show", f"{snapshot_arg}:{layer.path}"],
                    stderr=subprocess.DEVNULL,
                    text=True,
                    cwd=str(Path.cwd()),
                )
            except subprocess.CalledProcessError:
                continue
            diff = diff_ini_files(UeIniFile.from_string(old_text), UeIniFile.from_file(layer.path))
            if diff["total_changes"] > 0:
                diff["file"] = str(layer.path)
                result_data["changes"].append(diff)
        _print_json(result_data)
        return

    if files_arg and len(files_arg) == 2:
        left_path = _resolve_config_file_arg(disc, files_arg[0], cfg_type, platform_name)
        right_path = _resolve_config_file_arg(disc, files_arg[1], cfg_type, platform_name)
        if left_path.suffix.lower() == ".xml" and right_path.suffix.lower() == ".xml":
            result = diff_xml_files(BuildConfigXml.from_file(left_path), BuildConfigXml.from_file(right_path))
        else:
            result = diff_ini_files(UeIniFile.from_file(left_path), UeIniFile.from_file(right_path))
        result["left"] = str(left_path)
        result["right"] = str(right_path)
        _print_json(result)
        return

    print("error: specify --layers, --platforms, --snapshot, --files, or --audit", file=sys.stderr)
    sys.exit(1)


def _cmd_config_audit(args: argparse.Namespace) -> None:
    from .config import BuildConfigXml, UeIniFile, diff_ini_files, diff_xml_files, merge_ini_layers

    disc = _make_config_discovery(args)
    fmt = getattr(args, "config_format", None)
    cfg_type = getattr(args, "config_type", "Engine")
    platform_name = getattr(args, "platform", None)

    result: dict = {"overrides": []}

    if fmt in (None, "ini"):
        ini_layers = disc.ini_layers(config_type=cfg_type, platform=platform_name)
        engine_layers = [layer for layer in ini_layers if layer.name in ("AbsoluteBase", "Base", "BasePlatform") and layer.exists]
        project_layers = [
            layer
            for layer in ini_layers
            if layer.exists and (layer.name.startswith("Project") or layer.name in ("CustomConfig", "CustomConfigPlatform", "Saved"))
        ]
        if engine_layers and project_layers:
            engine_merged = merge_ini_layers([UeIniFile.from_file(layer.path) for layer in engine_layers])
            project_merged = merge_ini_layers(
                [UeIniFile.from_file(layer.path) for layer in engine_layers + project_layers],
            )
            diff = diff_ini_files(engine_merged, project_merged)
            diff["format"] = "ini"
            diff["config_type"] = cfg_type
            result["overrides"].append(diff)

    if fmt in (None, "xml"):
        existing_layers = [layer for layer in disc.xml_layers() if layer.exists]
        if len(existing_layers) >= 2:
            diff = diff_xml_files(
                BuildConfigXml.from_file(existing_layers[0].path),
                BuildConfigXml.from_file(existing_layers[-1].path),
            )
            diff["format"] = "xml"
            result["overrides"].append(diff)

    _print_json(result)


def _make_config_discovery(args: argparse.Namespace):
    from .config import ConfigDiscovery

    engine_path = getattr(args, "engine_path", None)
    project_path = getattr(args, "project_path", None)

    if not project_path:
        cwd = Path.cwd()
        for candidate in [cwd, *cwd.parents]:
            if list(candidate.glob("*.uproject")):
                project_path = str(candidate)
                break

    return ConfigDiscovery(engine_path=engine_path, project_path=project_path)


def _try_bridge_config_get(section: str, key: str | None, config_type: str) -> dict | None:
    if not key:
        return None
    try:
        result = call_tool("get-config-value", {"section": section, "key": key, "config_type": config_type})
    except Exception:
        return None
    return {"key": f"[{section}]{key}", "value": result.get("value"), "source": "bridge"}


def _try_bridge_validate(section: str, key: str, config_type: str) -> None:
    try:
        result = call_tool("validate-config-key", {"section": section, "key": key, "config_type": config_type})
    except Exception:
        return
    if not result.get("valid", True):
        print(f"warning: bridge says key '{key}' in section '{section}' is not recognized", file=sys.stderr)


def _search_config(disc, pattern: str, cfg_type: str, platform_name: str | None) -> list[dict]:
    import fnmatch

    from .config import UeIniFile

    results: list[dict] = []
    for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name):
        if not layer.exists:
            continue
        ini = UeIniFile.from_file(layer.path)
        for section in ini.sections():
            for key in ini.keys(section):
                if not fnmatch.fnmatch(key.lower(), f"*{pattern.lower()}*"):
                    continue
                results.append(
                    {
                        "section": section,
                        "key": key,
                        "value": ini.get(section, key),
                        "layer": layer.name,
                        "path": str(layer.path),
                    },
                )
    return results


def _ordered_project_json_files(disc) -> list:
    files = disc.project_json_files()
    return sorted(files, key=lambda item: (item.file_type != "uproject", str(item.path)))


def _split_xml_key(key_str: str) -> tuple[str, str]:
    parts = key_str.split(".", 1)
    if len(parts) == 1:
        return "BuildConfiguration", parts[0]
    return parts[0], parts[1]


def _resolve_config_file_arg(disc, file_arg: str, cfg_type: str, platform_name: str | None) -> Path:
    candidate = Path(file_arg)
    if candidate.exists():
        return candidate

    search_pool = [layer.path for layer in disc.ini_layers(config_type=cfg_type, platform=platform_name)]
    search_pool.extend(layer.path for layer in disc.xml_layers())
    search_pool.extend(item.path for item in disc.project_json_files())
    for path in search_pool:
        if str(path) == file_arg or path.name == file_arg:
            return path

    print(f"error: config file '{file_arg}' not found", file=sys.stderr)
    sys.exit(1)


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
        "--server",
        metavar="URL",
        help=(
            "Override the bridge server URL (e.g. http://127.0.0.1:9000). "
            "By default the URL is auto-discovered from the SOFT_UE_BRIDGE_URL env var, "
            "then SOFT_UE_BRIDGE_PORT, then .soft-ue-bridge/instance.json searched upward from cwd "
            "(written by the plugin at startup), then http://127.0.0.1:8080."
        ),
    )
    parser.add_argument(
        "--timeout",
        type=float,
        metavar="SEC",
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
    p_setup.add_argument(
        "project_path", nargs="?", default=None, help="Path to UE project root (default: current directory)"
    )
    p_setup.add_argument(
        "--plugin-src",
        metavar="PATH",
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
    p_cs.add_argument(
        "project_path", nargs="?", default=None, help="Path to UE project root (default: current directory)"
    )
    p_cs.set_defaults(func=cmd_check_setup)

    # status
    p_status = sub.add_parser(
        "status",
        help='Quick health check -- returns {"status": "ok"} if the bridge is running.',
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

    # set-viewport-camera
    p_vpcam = sub.add_parser(
        "set-viewport-camera",
        help="Set the editor viewport camera position, rotation, or view preset.",
        description=(
            "Set the editor viewport camera. Use presets for quick views or\n"
            "specify location/rotation manually.\n\n"
            "Presets: top, bottom, front, back, left, right, perspective\n\n"
            "Examples:\n"
            "  soft-ue-cli set-viewport-camera --preset top\n"
            "  soft-ue-cli set-viewport-camera --preset top --ortho-width 10000\n"
            "  soft-ue-cli set-viewport-camera --location 0,0,2000 --rotation -90,0,0\n"
            "  soft-ue-cli set-viewport-camera --preset perspective --location 500,500,500"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_vpcam.add_argument("--preset", choices=["top", "bottom", "front", "back", "left", "right", "perspective"],
                         help="Camera preset view")
    p_vpcam.add_argument("--location", metavar="X,Y,Z", help="Camera position in cm")
    p_vpcam.add_argument("--rotation", metavar="P,Y,R", help="Camera rotation in degrees")
    p_vpcam.add_argument("--ortho-width", type=float, dest="ortho_width",
                         help="Orthographic view width (zoom). Larger = more zoomed out")
    p_vpcam.set_defaults(func=cmd_set_viewport_camera)

    # batch-spawn-actors
    p_batch_spawn = sub.add_parser(
        "batch-spawn-actors",
        help="Batch-spawn multiple actors in a single undo transaction.",
        description=(
            "Spawns multiple actors in the editor level within a single undo transaction.\n"
            "Pass a JSON array of actor entries, each with class, optional mesh, location,\n"
            "rotation, scale, and label.\n\n"
            "Example:\n"
            '  soft-ue-cli batch-spawn-actors --actors \'[{"class":"StaticMeshActor",\n'
            '    "mesh":"/Game/Meshes/SM_Rock","location":[100,0,0],"label":"Rock_01"}]\''
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_batch_spawn.add_argument("--actors", required=True, help="JSON array of actor entries")
    p_batch_spawn.set_defaults(func=cmd_batch_spawn_actors)

    # batch-modify-actors
    p_batch_modify = sub.add_parser(
        "batch-modify-actors",
        help="Batch-modify transforms of existing actors in a single undo transaction.",
        description=(
            "Modifies location, rotation, and/or scale of multiple actors in a single\n"
            "undo transaction. Only specified fields are changed.\n\n"
            "Example:\n"
            '  soft-ue-cli batch-modify-actors --modifications \'[{"actor":"Rock_01",\n'
            '    "scale":[2,2,2],"rotation":[0,90,0]}]\''
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_batch_modify.add_argument("--modifications", required=True, help="JSON array of modification entries")
    p_batch_modify.set_defaults(func=cmd_batch_modify_actors)

    # batch-delete-actors
    p_batch_delete = sub.add_parser(
        "batch-delete-actors",
        help="Batch-delete multiple actors in a single undo transaction.",
        description=(
            "Deletes multiple actors from the editor level in a single undo transaction.\n"
            "Actors are identified by name or label.\n\n"
            "Example:\n"
            '  soft-ue-cli batch-delete-actors --actors \'["Rock_01","Tree_03"]\''
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_batch_delete.add_argument("--actors", required=True, help="JSON array of actor names or labels")
    p_batch_delete.set_defaults(func=cmd_batch_delete_actors)

    # query-level
    p_ql = sub.add_parser(
        "query-level",
        help="List actors in the running level with their transforms, tags, and components.",
        description=(
            "Returns a JSON array of actors in the current game world.\n"
            "Each actor entry includes: name, class, location, rotation, scale, tags.\n"
            "Use --components to also include the actor's component list.\n"
            "Use --include-properties to inspect actor and component property values.\n\n"
            "All filters are combined (AND logic). Wildcards (*) are supported in --search and --actor-name.\n\n"
            "EXAMPLES:\n"
            "  List all actors:                   soft-ue-cli query-level\n"
            "  Find all lights:                   soft-ue-cli query-level --class-filter PointLight\n"
            "  Find actor by name:                soft-ue-cli query-level --actor-name BP_Hero\n"
            '  Wildcard search:                   soft-ue-cli query-level --search "Enemy*"\n'
            "  Tagged actors with components:     soft-ue-cli query-level --tag hostile --components\n"
            "  Actor with health properties:      soft-ue-cli query-level --actor-name BP_Hero --include-properties --property-filter '*Health*'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ql.add_argument(
        "--actor-name", metavar="PATTERN", help="Match actor by exact name or label (supports * wildcard)"
    )
    p_ql.add_argument("--class-filter", metavar="CLASS", help="Only return actors of this class or subclasses (e.g. PointLight, Character)")
    p_ql.add_argument("--search", metavar="TEXT", help="Search actor names/labels (supports * wildcard)")
    p_ql.add_argument("--tag", metavar="TAG", help="Only return actors with this gameplay tag")
    p_ql.add_argument("--components", action="store_true", help="Include component list for each actor")
    p_ql.add_argument(
        "--include-properties", action="store_true",
        help="Include actor and component properties (automatically enables --components)",
    )
    p_ql.add_argument(
        "--property-filter", metavar="PATTERN",
        help="Filter properties by name (wildcards supported, e.g., '*Health*'). Requires --include-properties.",
    )
    p_ql.add_argument("--limit", type=int, default=100, metavar="N", help="Max actors to return (default: 100)")
    p_ql.add_argument("--include-foliage", action="store_true", help="Include FoliageType instance counts")
    p_ql.add_argument("--include-grass", action="store_true", help="Include LandscapeProxy grass/material info")
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
            '  e.g. \'{"damage": 25.0, "cause": "fire"}\'\n\n'
            "EXAMPLES:\n"
            "  No args:    soft-ue-cli call-function BP_Hero Jump\n"
            "  With args:  soft-ue-cli call-function BP_Hero TakeDamage --args '{\"damage\": 25.0}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cf.add_argument("actor_name", help="Actor name or label as shown in query-level output")
    p_cf.add_argument("function_name", help="Exact function name (case-sensitive)")
    p_cf.add_argument("--args", metavar="JSON", help="Function arguments as a JSON object")
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
            '  Vector:             soft-ue-cli set-property BP_Hero RelativeScale3D "2,2,2"'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_sp.add_argument("actor_name", help="Actor name or label as shown in query-level output")
    p_sp.add_argument("property_name", help="Property name, or ComponentName.PropertyName for component properties")
    p_sp.add_argument("value", help="New value as a string (UE reflection converts the type)")
    p_sp.set_defaults(func=cmd_set_property)

    # get-property (runtime actors)
    p_gp = sub.add_parser(
        "get-property",
        help="Get a property value from a runtime actor or component using UE reflection.",
        description=(
            "Reads a UPROPERTY value from the named actor via UE's reflection system.\n"
            "For component properties, use dot notation: ComponentName.PropertyName\n\n"
            "Returns the property value as a string along with its type.\n\n"
            "EXAMPLES:\n"
            "  Actor property:     soft-ue-cli get-property PointLight_0 Intensity\n"
            "  Component property: soft-ue-cli get-property BP_Hero LightComponent.Intensity\n"
            "  Boolean:            soft-ue-cli get-property BP_Enemy bIsHostile\n"
            "  Vector:             soft-ue-cli get-property BP_Hero RelativeScale3D"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_gp.add_argument("actor_name", help="Actor name or label as shown in query-level output")
    p_gp.add_argument("property_name", help="Property name, or ComponentName.PropertyName for component properties")
    p_gp.set_defaults(func=cmd_get_property)

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
    p_gl.add_argument(
        "--lines", type=int, default=100, metavar="N", help="Number of recent lines to return (default: 100)"
    )
    p_gl.add_argument("--filter", metavar="TEXT", help="Case-insensitive substring filter on log text")
    p_gl.add_argument("--category", metavar="CAT", help="Filter by log category (e.g. LogAI, LogTemp, LogEngine)")
    p_gl.add_argument("--raw", action="store_true", help="Output plain text lines instead of JSON")
    p_gl.set_defaults(func=cmd_get_logs)

    # get-console-var
    p_gcv = sub.add_parser(
        "get-console-var",
        help="Read the current value of a UE console variable (CVar).",
        description=(
            "Reads a console variable value using IConsoleManager.\n"
            'Returns {"name": "...", "value": "..."}.\n\n'
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
    p_scv.add_argument("name", help="CVar name (case-sensitive)")
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
            '  soft-ue-cli query-asset --query "BP_*" --class Blueprint\n'
            "  soft-ue-cli query-asset --asset-path /Game/Data/DT_Items\n"
            "  soft-ue-cli query-asset --path /Game/Characters --class SkeletalMesh"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qa.add_argument("--query", metavar="PATTERN", help="Search pattern (supports * and ?)")
    p_qa.add_argument(
        "--class", metavar="CLASS", dest="asset_class", help="Filter by asset class (e.g. Blueprint, StaticMesh)"
    )
    p_qa.add_argument("--path", metavar="PATH", help="Filter by path prefix (e.g. /Game/Blueprints)")
    p_qa.add_argument("--limit", type=int, metavar="N", help="Max search results (default: 100)")
    p_qa.add_argument("--asset-path", metavar="PATH", help="Asset path to inspect")
    p_qa.add_argument("--depth", type=int, metavar="N", help="Recursion depth for inspect (default: 2)")
    p_qa.add_argument("--include-defaults", action="store_true", help="Include default/empty properties")
    p_qa.add_argument("--property-filter", metavar="PATTERN", help="Filter properties by name")
    p_qa.add_argument("--category-filter", metavar="CAT", help="Filter properties by category")
    p_qa.add_argument("--row-filter", metavar="PATTERN", help="Filter DataTable rows by name")
    p_qa.add_argument("--search", metavar="TEXT", help="General search filter")
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
    p_gap.add_argument(
        "--resolution", type=int, metavar="N", help="Output resolution in pixels, 64-1024 (default: 256)"
    )
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
    p_oa.add_argument("--asset-path", metavar="PATH", help="Asset path to open (e.g. /Game/Blueprints/BP_Player)")
    p_oa.add_argument("--window-name", metavar="NAME", help="Editor window to open (e.g. OutputLog, ContentBrowser)")
    p_oa.add_argument("--no-focus", action="store_true", help="Do not bring the window to front")
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
            '  soft-ue-cli query-blueprint /Game/Blueprints/BP_Character --search "*Health*"'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qb.add_argument("asset_path", help="Blueprint asset path")
    p_qb.add_argument(
        "--include",
        metavar="SECTION",
        help="Sections to include: functions, variables, components, defaults, graph, component_overrides, all (default: all)",
    )
    p_qb.add_argument("--no-detail", action="store_true", help="Omit detailed flags and metadata")
    p_qb.add_argument("--property-filter", metavar="PATTERN", help="Filter properties by name (wildcards)")
    p_qb.add_argument("--category-filter", metavar="CAT", help="Filter properties by category")
    p_qb.add_argument("--include-inherited", action="store_true", help="Include inherited properties")
    p_qb.add_argument("--component-filter", metavar="PATTERN", help="Filter components by name (wildcards)")
    p_qb.add_argument("--include-non-overridden", action="store_true", help="Include non-overridden properties")
    p_qb.add_argument("--search", metavar="TEXT", help="Filter items by name (wildcards)")
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
    p_qbg.add_argument("--node-guid", metavar="GUID", help="Get a specific node by GUID")
    p_qbg.add_argument("--callable-name", metavar="NAME", help="Get a specific event/function/macro graph")
    p_qbg.add_argument("--list-callables", action="store_true", help="List all callables without full graph")
    p_qbg.add_argument("--graph-name", metavar="NAME", help="Filter by graph name")
    p_qbg.add_argument("--graph-type", metavar="TYPE", help="Filter by type: event, function, macro, anim_graph, etc.")
    p_qbg.add_argument("--include-positions", action="store_true", help="Include node X/Y positions")
    p_qbg.add_argument("--search", metavar="TEXT", help="Filter nodes by title (wildcards)")
    p_qbg.add_argument(
        "--include-anim-props", action="store_true", help="Include AnimNode struct properties on AnimGraph nodes"
    )
    p_qbg.set_defaults(func=cmd_query_blueprint_graph)

    # -------------------------------------------------------------------------
    # Editor tools — Build
    # -------------------------------------------------------------------------

    p_bar = sub.add_parser(
        "build-and-relaunch",
        help="Trigger a C++ build and relaunch the editor.",
        description=(
            "Initiates a full C++ build of the project and relaunches the UE editor.\n\n"
            "With --wait, the CLI monitors build progress and waits for the editor to\n"
            "come back up. Returns build result, duration, and compiler errors on failure.\n"
            "Without --wait, returns immediately after initiating the workflow.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli build-and-relaunch --wait\n"
            "  soft-ue-cli build-and-relaunch --wait --config Debug\n"
            "  soft-ue-cli build-and-relaunch --wait --skip-relaunch\n"
            "  soft-ue-cli --timeout 300 build-and-relaunch"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_bar.add_argument(
        "--config", choices=["Development", "Debug", "Shipping"], help="Build configuration (default: Development)"
    )
    p_bar.add_argument("--skip-relaunch", action="store_true", help="Build only, do not relaunch the editor")
    p_bar.add_argument("--wait", action="store_true", help="Wait for build to complete and editor to relaunch, then return the result")
    p_bar.set_defaults(func=cmd_build_and_relaunch)

    p_tlc = sub.add_parser(
        "trigger-live-coding",
        help="Trigger Live Coding compilation (hot reload C++ changes).",
        description=(
            "Triggers the UE Live Coding compiler to pick up C++ source changes without\n"
            "a full editor restart. Requires Live Coding to be enabled in editor preferences.\n\n"
            "By default, waits for compilation to complete and returns the result\n"
            "(success, failure, cancelled, etc.). Use --no-wait for fire-and-forget.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli trigger-live-coding\n"
            "  soft-ue-cli trigger-live-coding --no-wait"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_tlc.add_argument("--no-wait", action="store_true", help="Return immediately without waiting for compilation result")
    p_tlc.set_defaults(func=cmd_trigger_live_coding)

    # -------------------------------------------------------------------------
    # Editor tools — Editor
    # -------------------------------------------------------------------------

    p_cs2 = sub.add_parser(
        "capture-screenshot",
        help="Capture a screenshot of the editor window or a specific panel.",
        description=(
            "Modes:\n"
            "  window    — capture the entire editor\n"
            "  tab       — capture a specific editor panel (use --window-name)\n"
            "  region    — capture a screen region (use --region X,Y,W,H)\n"
            "  viewport  — capture the PIE game screen\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli capture-screenshot window\n"
            "  soft-ue-cli capture-screenshot tab --window-name Blueprint\n"
            "  soft-ue-cli capture-screenshot region --region 0,0,800,600 --output base64\n"
            "  soft-ue-cli capture-screenshot viewport"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cs2.add_argument("mode", choices=["window", "tab", "region", "viewport"], help="Capture mode")
    p_cs2.add_argument("--window-name", metavar="NAME", help="Editor panel name for 'tab' mode")
    p_cs2.add_argument("--region", metavar="X,Y,W,H", help="Screen coordinates for 'region' mode")
    p_cs2.add_argument("--format", choices=["png", "jpeg"], help="Image format (default: png)")
    p_cs2.add_argument("--output", choices=["file", "base64"], help="Output mode: file (default) or base64")
    p_cs2.set_defaults(func=cmd_capture_screenshot)

    # ---- capture-viewport (runtime — works in PIE and standalone) ----

    p_cv = sub.add_parser(
        "capture-viewport",
        help="Capture the current viewport (auto-detects PIE, standalone, or editor).",
        description=(
            "Capture a viewport screenshot.\n"
            "By default, auto-detects: tries game viewport (PIE/standalone) first,\n"
            "falls back to editor viewport. Use --source to force a specific target.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli capture-viewport\n"
            "  soft-ue-cli capture-viewport --source editor\n"
            "  soft-ue-cli capture-viewport --format jpeg\n"
            "  soft-ue-cli capture-viewport --output base64"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cv.add_argument("--source", choices=["auto", "game", "editor"],
                       help="Viewport source: auto (default), game (PIE/standalone), editor")
    p_cv.add_argument("--format", choices=["png", "jpeg"], help="Image format (default: png)")
    p_cv.add_argument("--output", choices=["file", "base64"], help="Output mode: file (default) or base64")
    p_cv.set_defaults(func=cmd_capture_viewport)

    # -------------------------------------------------------------------------
    # Editor tools — Material
    # -------------------------------------------------------------------------

    p_qm = sub.add_parser(
        "query-material",
        help="Inspect a Material, MaterialInstance, or MaterialFunction asset.",
        description=(
            "Returns the material graph nodes and/or scalar/vector/texture parameters.\n"
            "Works on Material, MaterialInstance, and MaterialFunction assets.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli query-material /Game/Materials/M_Rock\n"
            "  soft-ue-cli query-material /Game/Materials/MI_Rock --include parameters\n"
            '  soft-ue-cli query-material /Game/Materials/M_Rock --parameter-filter "*Color*"\n'
            "  soft-ue-cli query-material /Game/Functions/MF_DistanceFade --include graph"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qm.add_argument("asset_path", help="Material, MaterialInstance, or MaterialFunction asset path")
    p_qm.add_argument("--include", metavar="SECTION", help="What to include: graph, parameters, all (default: all)")
    p_qm.add_argument("--include-positions", action="store_true", help="Include expression X/Y positions")
    p_qm.add_argument("--no-defaults", action="store_true", help="Exclude default parameter values")
    p_qm.add_argument("--parameter-filter", metavar="PATTERN", help="Filter parameters by name (wildcards)")
    p_qm.add_argument("--parent-chain", action="store_true", help="Include full parent material chain from leaf to root")
    p_qm.set_defaults(func=cmd_query_material)

    p_qmpc = sub.add_parser(
        "query-mpc",
        help="Read or write Material Parameter Collection values.",
        description=(
            "Read or write MPC scalar/vector parameter values.\n"
            "Read mode returns both default (asset) and runtime (world) values.\n"
            "Write mode sets runtime values in the current world.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli query-mpc /Game/Materials/MPC_GlobalParams\n"
            '  soft-ue-cli query-mpc /Game/Materials/MPC_Wind --action write --parameter-name WindIntensity --value 0.5\n'
            '  soft-ue-cli query-mpc /Game/Materials/MPC_Wind --action write --parameter-name WindColor --value "[1.0,0.5,0.0,1.0]"'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_qmpc.add_argument("asset_path", help="MPC asset path")
    p_qmpc.add_argument("--action", choices=["read", "write"], help="Action (default: read)")
    p_qmpc.add_argument("--parameter-name", metavar="NAME", dest="parameter_name", help="Parameter name (required for write)")
    p_qmpc.add_argument("--value", metavar="VALUE", help="Value for write: number or JSON array [r,g,b,a]")
    p_qmpc.add_argument("--world", choices=["editor", "pie", "game"], help="World context")
    p_qmpc.set_defaults(func=cmd_query_mpc)

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
    p_ps.add_argument("--mode", choices=["viewport", "new_window", "standalone"], help="PIE launch mode (for start)")
    p_ps.add_argument("--map", metavar="PATH", help="Map to load (for start)")
    p_ps.add_argument(
        "--timeout", type=float, metavar="SEC", help="Timeout waiting for PIE ready (for start, default: 30)"
    )
    p_ps.add_argument("--include", metavar="LIST", help="Comma-separated: world,players (for get-state)")
    p_ps.add_argument("--actor-name", metavar="NAME", help="Actor to monitor (for wait-for)")
    p_ps.add_argument("--property", metavar="PROP", help="Property to check (for wait-for)")
    p_ps.add_argument(
        "--operator",
        choices=["equals", "not_equals", "less_than", "greater_than", "contains"],
        help="Comparison operator (for wait-for)",
    )
    p_ps.add_argument("--expected", metavar="JSON", help="Expected value as JSON (for wait-for)")
    p_ps.add_argument("--wait-timeout", type=float, metavar="SEC", help="Timeout for wait-for (default: 10)")
    p_ps.set_defaults(func=cmd_pie_session)

    p_ti = sub.add_parser(
        "trigger-input",
        help="Send input events to a running game (PIE or packaged build).",
        description=(
            "Actions:\n"
            "  key      — press/release a key (use --key)\n"
            "  action   — trigger an input action (use --action-name)\n"
            "  move-to  — move character to location (use --target X,Y,Z)\n"
            "  look-at  — rotate character toward target (use --target or --target-actor)\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli trigger-input key --key Space\n"
            "  soft-ue-cli trigger-input action --action-name Jump\n"
            "  soft-ue-cli trigger-input move-to --target 100,200,0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ti.add_argument("action", choices=["key", "action", "move-to", "look-at"])
    p_ti.add_argument("--player-index", type=int, metavar="N", help="Player index (default: 0)")
    p_ti.add_argument("--key", metavar="KEY", help="Key name (e.g. W, Space, LeftMouseButton)")
    p_ti.add_argument("--action-name", metavar="NAME", help="Input action name (e.g. Jump)")
    p_ti.add_argument("--release", action="store_true", help="Release (instead of press) for key/action")
    p_ti.add_argument("--target", metavar="X,Y,Z", help="Target location for move-to/look-at")
    p_ti.add_argument("--target-actor", metavar="NAME", help="Target actor name for look-at")
    p_ti.set_defaults(func=cmd_trigger_input)

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
    p_ic.add_argument(
        "--channels", metavar="LIST", help="Comma-separated trace channels (for start, e.g. cpu,gpu,memory)"
    )
    p_ic.add_argument("--output-file", metavar="FILE", help="Output filename for trace (for start)")
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
    p_pinfo.add_argument(
        "--section", choices=["input", "collision", "tags", "maps", "all"], help="Settings section to include"
    )
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
    p_fr.add_argument("--node-class", metavar="CLASS", help="Node class to search for (for type=node)")
    p_fr.add_argument("--function-name", metavar="NAME", help="Function name filter for CallFunction nodes")
    p_fr.add_argument("--limit", type=int, metavar="N", help="Max results (default: 100)")
    p_fr.add_argument("--search", metavar="PATTERN", help="Filter results by asset name (wildcards)")
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
            '  soft-ue-cli run-python-script --script "import unreal; print(unreal.SystemLibrary.get_engine_version())"\n'
            "  soft-ue-cli run-python-script --script-path /path/to/my_script.py\n"
            "  soft-ue-cli run-python-script --script-path my_script.py --arguments '{\"count\": 5}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rps.add_argument("--name", metavar="NAME", help="Name of a saved script to run (see save-script / list-scripts)")
    p_rps.add_argument("--script", metavar="CODE", help="Inline Python code to execute")
    p_rps.add_argument("--script-path", metavar="PATH", help="Path to a Python script file")
    p_rps.add_argument("--python-paths", metavar="PATH", nargs="+", help="Additional sys.path directories")
    p_rps.add_argument(
        "--arguments", metavar="JSON", help="Arguments as JSON object (accessible via unreal.get_mcp_args())"
    )
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
    p_ss.add_argument("--script", metavar="CODE", help="Inline Python code to save")
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
    p_qst.add_argument(
        "--include",
        metavar="SECTION",
        help="Sections to include: states, transitions, tasks, evaluators, parameters, all (default: all)",
    )
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
    p_asss.add_argument(
        "--state-type", choices=["State", "Group", "Linked", "Subtree"], help="State type (default: State)"
    )
    p_asss.add_argument("--parent-state", metavar="NAME", help="Parent state name (creates root state if omitted)")
    p_asss.add_argument(
        "--selection-behavior",
        choices=["None", "TryEnterState", "TrySelectChildrenInOrder", "TryFollowTransitions"],
        help="Selection behavior (default: TryEnterState)",
    )
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
    p_asst.add_argument("asset_path", help="StateTree asset path")
    p_asst.add_argument("state_name", help="Name of the state to add the task to")
    p_asst.add_argument("task_class", help="Task class name (e.g. UStateTreeTask_PlayAnimation)")
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
    p_astr.add_argument("asset_path", help="StateTree asset path")
    p_astr.add_argument("source_state", help="Source state name")
    p_astr.add_argument("target_state", help="Target state name")
    p_astr.add_argument("--trigger", metavar="TRIGGER", help="Transition trigger condition")
    p_astr.add_argument("--priority", type=int, metavar="N", help="Transition priority")
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
    p_iwb.add_argument("--depth-limit", type=int, metavar="N", help="Max hierarchy depth (-1 = unlimited)")
    p_iwb.add_argument("--no-bindings", action="store_true", help="Exclude property binding information")
    p_iwb.set_defaults(func=cmd_inspect_widget_blueprint)

    p_irw = sub.add_parser(
        "inspect-runtime-widgets",
        help="Inspect live UMG widget geometry during a PIE session.",
        description=(
            "Walk the live UMG widget tree during a PIE session and return computed\n"
            "geometry, slot info, properties, and optionally Slate widget data.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli inspect-runtime-widgets\n"
            "  soft-ue-cli inspect-runtime-widgets --filter HealthBar\n"
            "  soft-ue-cli inspect-runtime-widgets --class-filter TextBlock --include-slate\n"
            "  soft-ue-cli inspect-runtime-widgets --pie-index 1 --root-widget WBP_HUD_C_0"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_irw.add_argument("--filter", help="Keyword search (substring match against name, class)")
    p_irw.add_argument("--class-filter", help="Filter by widget class name (substring match)")
    p_irw.add_argument("--depth-limit", type=int, metavar="N", help="Max hierarchy depth (-1 = unlimited)")
    p_irw.add_argument("--include-slate", action="store_true", help="Include underlying Slate widget data")
    p_irw.add_argument("--pie-index", type=int, metavar="N", help="PIE instance index (default: 0)")
    p_irw.add_argument("--no-geometry", action="store_true", help="Exclude computed geometry data")
    p_irw.add_argument("--no-properties", action="store_true", help="Exclude widget properties")
    p_irw.add_argument("--root-widget", metavar="NAME", help="Start traversal from a specific widget")
    p_irw.set_defaults(func=cmd_inspect_runtime_widgets)

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
    p_sap.add_argument("asset_path", help="Asset path (e.g. /Game/Blueprints/BP_Player)")
    p_sap.add_argument("property_path", help="Dot-separated property path (e.g. Health, Stats.MaxHealth)")
    p_sap.add_argument(
        "value",
        nargs="?",
        default=None,
        help="Value as JSON (number, string, boolean, array, object). Required unless --clear-override.",
    )
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
    p_ac.add_argument("actor_name", help="Actor name or label")
    p_ac.add_argument("component_class", help="Component class (e.g. PointLightComponent, StaticMeshComponent)")
    p_ac.add_argument("--component-name", metavar="NAME", help="Name for the new component")
    p_ac.add_argument("--attach-to", metavar="NAME", help="Parent component to attach to")
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
    p_aw.add_argument("asset_path", help="WidgetBlueprint asset path")
    p_aw.add_argument("widget_class", help="Widget class (e.g. Button, TextBlock, Image, ProgressBar)")
    p_aw.add_argument("widget_name", help="Name for the new widget")
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
            '  soft-ue-cli add-datatable-row /Game/Data/DT_Items Sword --row-data \'{"Damage": 50, "Name": "Iron Sword"}\''
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_adr.add_argument("asset_path", help="DataTable asset path")
    p_adr.add_argument("row_name", help="Name for the new row")
    p_adr.add_argument(
        "--row-data", metavar="JSON", help="Row data as JSON object with property names matching the row struct"
    )
    p_adr.set_defaults(func=cmd_add_datatable_row)

    p_agn = sub.add_parser(
        "add-graph-node",
        help="Add a node to a Blueprint event graph, Material graph, or AnimLayerInterface.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli add-graph-node /Game/BP_Player K2Node_CallFunction\n"
            "  soft-ue-cli add-graph-node /Game/M_Rock MaterialExpressionAdd --position 100,200\n"
            "\n"
            "ANIM LAYER FUNCTIONS:\n"
            "  To add an anim layer function to an AnimLayerInterface, use\n"
            "  node_class 'AnimLayerFunction' with --graph-name:\n"
            "\n"
            "  soft-ue-cli add-graph-node /Game/Animation/ALI_MyInterface AnimLayerFunction \\\n"
            "      --graph-name SecondaryMotion\n"
            "\n"
            "  This creates an AnimationGraph with Root (output pose) and\n"
            "  LinkedInputPose (input pose) nodes wired as a passthrough."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_agn.add_argument("asset_path", help="Blueprint, Material, or AnimLayerInterface asset path")
    p_agn.add_argument("node_class", help="Node class (e.g. K2Node_CallFunction, MaterialExpressionAdd, AnimLayerFunction)")
    p_agn.add_argument("--graph-name", metavar="NAME", help="Graph name for Blueprints (default: EventGraph)")
    p_agn.add_argument("--position", metavar="X,Y", help="Node position as X,Y")
    p_agn.add_argument("--no-auto-position", action="store_true", help="Disable automatic positioning")
    p_agn.add_argument("--connect-to-node", metavar="GUID", help="Node GUID to position relative to")
    p_agn.add_argument("--connect-to-pin", metavar="PIN", help="Pin name to position relative to")
    p_agn.add_argument("--properties", metavar="JSON", help="Node properties as JSON object")
    p_agn.set_defaults(func=cmd_add_graph_node)

    p_mi = sub.add_parser(
        "modify-interface",
        help="Add or remove an implemented interface on a Blueprint.",
        description=(
            "EXAMPLES:\n"
            "  soft-ue-cli modify-interface /Game/ABP_Character add ALI_Locomotion\n"
            "  soft-ue-cli modify-interface /Game/ABP_Character remove ALI_Locomotion\n"
            "  soft-ue-cli modify-interface /Game/ABP_Character add /Game/Animation/ALI_Locomotion.ALI_Locomotion_C"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_mi.add_argument("asset_path", help="Blueprint or AnimBlueprint asset path")
    p_mi.add_argument("action", choices=["add", "remove"], help="Action: add or remove")
    p_mi.add_argument("interface_class", help="Interface class name or full path")
    p_mi.set_defaults(func=cmd_modify_interface)

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
    p_rgn.add_argument("node_id", help="Node GUID (Blueprint) or expression name (Material)")
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
    p_cgp.add_argument("asset_path", help="Blueprint or Material asset path")
    p_cgp.add_argument("source_node", help="Source node GUID or expression name")
    p_cgp.add_argument("source_pin", help="Source pin name")
    p_cgp.add_argument("target_node", help="Target node GUID or expression name")
    p_cgp.add_argument("target_pin", help="Target pin name")
    p_cgp.set_defaults(func=cmd_connect_graph_pins)

    p_dgp = sub.add_parser(
        "disconnect-graph-pin",
        help="Disconnect connections from a pin in a Blueprint or Material graph.",
        description=(
            "Removes wires connected to a specific pin on a node.\n"
            "By default, disconnects ALL connections from the pin.\n"
            "With --target-node and --target-pin, disconnects only the specific\n"
            "connection to that target, preserving other wires.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli disconnect-graph-pin /Game/BP_Player {guid} execute\n"
            "  soft-ue-cli disconnect-graph-pin /Game/BP_Player {src-guid} OutputPose --target-node {tgt-guid} --target-pin InputPose\n"
            "  soft-ue-cli disconnect-graph-pin /Game/Materials/M_Rock Add_0 A"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_dgp.add_argument("asset_path", help="Blueprint or Material asset path")
    p_dgp.add_argument("node_id", help="Node GUID or expression name")
    p_dgp.add_argument("pin_name", help="Pin name to disconnect")
    p_dgp.add_argument("--target-node", metavar="GUID", help="Target node GUID (disconnect only this specific connection)")
    p_dgp.add_argument("--target-pin", metavar="PIN", help="Target pin name (required with --target-node)")
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
            "  soft-ue-cli create-asset /Game/Data/DT_Items DataTable --row-struct /Game/Structs/S_Item\n"
            "  soft-ue-cli create-asset /Game/Anim/ABP_Hero AnimBlueprint --skeleton /Game/Characters/SK_Mannequin\n"
            "  soft-ue-cli create-asset /Game/Maps/LV_NewLevel World\n"
            "  soft-ue-cli create-asset /Game/Maps/LV_Copy World --template /Game/Maps/LV_Template"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cas.add_argument("asset_path", help="Full asset path including name (e.g. /Game/Blueprints/BP_NewActor)")
    p_cas.add_argument("asset_class", help="Asset class (e.g. Blueprint, Material, DataTable, WidgetBlueprint, World)")
    p_cas.add_argument("--parent-class", metavar="CLASS", help="Parent class for Blueprints (e.g. Actor, Character)")
    p_cas.add_argument("--skeleton", metavar="PATH", help="Skeleton asset path for AnimBlueprint (e.g. /Game/Characters/SK_Mannequin)")
    p_cas.add_argument("--row-struct", metavar="PATH", help="Row struct path for DataTables")
    p_cas.add_argument("--template", metavar="PATH", help="Template level path for World assets (e.g. /Game/Maps/LV_Template)")
    p_cas.set_defaults(func=cmd_create_asset)

    p_sa = sub.add_parser(
        "save-asset",
        help="Save a modified asset to disk.",
        description=(
            "Saves an in-memory asset to its .uasset file on disk.\n"
            "Use after mutation commands (add-graph-node, modify-interface, etc.)\n"
            "to persist changes and prevent data loss from editor crashes.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli save-asset /Game/Blueprints/BP_Player\n"
            "  soft-ue-cli save-asset /Game/Animation/ABP_Hero"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_sa.add_argument("asset_path", help="Asset path to save")
    p_sa.add_argument("--checkout", action="store_true", help="Check out from source control before saving")
    p_sa.set_defaults(func=cmd_save_asset)

    p_cb = sub.add_parser(
        "compile-blueprint",
        help="Compile a Blueprint or AnimBlueprint.",
        description=(
            "Triggers compilation of a Blueprint and returns the result.\n"
            "Use after graph modifications to validate changes and generate\n"
            "CDO properties for runtime.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli compile-blueprint /Game/Blueprints/BP_Player\n"
            "  soft-ue-cli compile-blueprint /Game/Animation/ABP_Hero"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cb.add_argument("asset_path", help="Blueprint or AnimBlueprint asset path")
    p_cb.set_defaults(func=cmd_compile_blueprint)

    p_cm = sub.add_parser(
        "compile-material",
        help="Compile a Material, MaterialInstance, or MaterialFunction.",
        description=(
            "Triggers recompilation of a material asset and returns the result.\n"
            "Use after modifying material graphs or parameters.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli compile-material /Game/Materials/M_Rock\n"
            "  soft-ue-cli compile-material /Game/Functions/MF_DistanceFade"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_cm.add_argument("asset_path", help="Material, MaterialInstance, or MaterialFunction asset path")
    p_cm.set_defaults(func=cmd_compile_material)

    p_ign = sub.add_parser(
        "insert-graph-node",
        help="Insert a node between two connected nodes in a Blueprint graph.",
        description=(
            "Atomically inserts a new node between two already-connected nodes.\n"
            "Disconnects source→target, creates the new node, and wires\n"
            "source→new→target in a single undo transaction.\n\n"
            "Pin auto-detection: if --new-input-pin and --new-output-pin are\n"
            "not specified, the tool finds the first compatible pins.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli insert-graph-node /Game/ABP_Hero AnimGraphNode_LinkedAnimLayer \\\n"
            "    {src-guid} OutputPose {tgt-guid} InputPose --graph-name AnimGraph\n"
            "  soft-ue-cli insert-graph-node /Game/BP_Player K2Node_CallFunction \\\n"
            "    {src-guid} then {tgt-guid} execute"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_ign.add_argument("asset_path", help="Blueprint or AnimBlueprint asset path")
    p_ign.add_argument("node_class", help="Node class to insert (e.g. AnimGraphNode_LinkedAnimLayer)")
    p_ign.add_argument("source_node", help="Source (upstream) node GUID")
    p_ign.add_argument("source_pin", help="Source node output pin name")
    p_ign.add_argument("target_node", help="Target (downstream) node GUID")
    p_ign.add_argument("target_pin", help="Target node input pin name")
    p_ign.add_argument("--graph-name", metavar="NAME", help="Graph name (default: EventGraph)")
    p_ign.add_argument("--new-input-pin", metavar="PIN", help="Input pin on the new node (auto-detected if omitted)")
    p_ign.add_argument("--new-output-pin", metavar="PIN", help="Output pin on the new node (auto-detected if omitted)")
    p_ign.add_argument("--properties", metavar="JSON", help="JSON object of properties to set on the new node")
    p_ign.set_defaults(func=cmd_insert_graph_node)

    p_snpr = sub.add_parser(
        "set-node-property",
        help="Set properties on a graph node by GUID.",
        description=(
            "Set properties on an existing graph node identified by GUID.\n"
            "Supports UPROPERTY members, inner anim node struct properties,\n"
            "and pin defaults (e.g. Alpha on SpringBone).\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli set-node-property /Game/ABP_Hero {node-guid} '{\"SpringStiffness\": 450}'\n"
            "  soft-ue-cli set-node-property /Game/ABP_Hero {node-guid} '{\"Alpha\": 0.08}'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_snpr.add_argument("asset_path", help="Blueprint or AnimBlueprint asset path")
    p_snpr.add_argument("node_guid", help="Node GUID (from query-blueprint-graph)")
    p_snpr.add_argument("properties", help="Properties as JSON object")
    p_snpr.set_defaults(func=cmd_set_node_property)

    # -------------------------------------------------------------------------
    # Offline inspection
    # -------------------------------------------------------------------------

    p_iu = sub.add_parser(
        "inspect-uasset",
        help="Inspect a local .uasset Blueprint file offline.",
        description=(
            "Parse a local Blueprint .uasset file without requiring a running editor.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli inspect-uasset D:/Project/Content/Blueprints/BP_Character.uasset\n"
            "  soft-ue-cli inspect-uasset BP_Character.uasset --sections all\n"
            "  soft-ue-cli inspect-uasset BP_Character.uasset --sections variables,functions --format table"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_iu.add_argument("file_path", help="Path to the local .uasset file")
    p_iu.add_argument(
        "--sections",
        metavar="SECTIONS",
        default="summary",
        help=(
            "Comma-separated sections to extract: summary, variables, functions, "
            "components, events, all (default: summary)"
        ),
    )
    p_iu.add_argument(
        "--format",
        choices=["json", "table"],
        default="json",
        help="Output format (default: json)",
    )
    p_iu.set_defaults(func=cmd_inspect_uasset)

    p_du = sub.add_parser(
        "diff-uasset",
        help="Diff two local .uasset Blueprint files offline.",
        description=(
            "Inspect two local Blueprint .uasset files and diff their extracted metadata.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli diff-uasset BP_Old.uasset BP_New.uasset\n"
            "  soft-ue-cli diff-uasset BP_Old.uasset BP_New.uasset --sections all\n"
            "  soft-ue-cli diff-uasset BP_Old.uasset BP_New.uasset --sections variables,functions --format table"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_du.add_argument("left_file", help="Path to the base .uasset file")
    p_du.add_argument("right_file", help="Path to the updated .uasset file")
    p_du.add_argument(
        "--sections",
        metavar="SECTIONS",
        default="summary",
        help=(
            "Comma-separated sections to diff: summary, variables, functions, "
            "components, events, all (default: summary)"
        ),
    )
    p_du.add_argument(
        "--format",
        choices=["json", "table"],
        default="json",
        help="Output format (default: json)",
    )
    p_du.set_defaults(func=cmd_diff_uasset)

    # -------------------------------------------------------------------------
    # Knowledge
    # -------------------------------------------------------------------------

    p_k = sub.add_parser(
        "query-ue-knowledge",
        help="Query the knowledge server for UE API docs, tutorials, and workflow skills.",
        description="Coming soon. Follow https://github.com/softdaddy-o/soft-ue-cli for updates.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_k.add_argument("query", nargs="?", default=None, help="Natural language question about UE API or behavior")
    p_k.add_argument("--max-results", type=int, default=5, metavar="N", help="Max results to return (default: 5)")
    p_k.add_argument("--type", choices=["skill"], metavar="TYPE", help="Filter by type: 'skill' for workflow skills")
    p_k.add_argument("--list-skills", action="store_true", help="List all available workflow skills")
    p_k.set_defaults(func=cmd_knowledge)

    # -------------------------------------------------------------------------
    # Feedback
    # -------------------------------------------------------------------------

    p_rb = sub.add_parser(
        "report-bug",
        help="Report a bug by creating a GitHub issue on the soft-ue-cli repo.",
        description=(
            "Creates a GitHub issue with structured bug report fields.\n"
            "Auto-enriches with system info (CLI version, Python, OS, bridge status)\n"
            "unless --no-system-info is passed.\n\n"
            "AUTHENTICATION:\n"
            "  Set GITHUB_TOKEN env var or run 'gh auth login'.\n"
            "  Required scope: 'public_repo' (public repos) or 'repo' (private).\n\n"
            "EXAMPLES:\n"
            '  soft-ue-cli report-bug --title "crash on spawn" --description "Editor crashes"\n'
            '  soft-ue-cli report-bug --title "crash" --description "desc" --severity critical\n'
            '  soft-ue-cli report-bug --title "bug" --description "desc" --no-system-info'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rb.add_argument("--title", required=True, help="Short bug summary")
    p_rb.add_argument(
        "--description", required=True,
        help="Detailed description. Do not include secrets, tokens, passwords, or private credentials.",
    )
    p_rb.add_argument("--steps", help="Steps to reproduce")
    p_rb.add_argument("--expected", help="Expected behavior")
    p_rb.add_argument("--actual", help="Actual behavior")
    p_rb.add_argument("--severity", choices=["critical", "major", "minor"], help="Bug severity label")
    p_rb.add_argument("--no-system-info", action="store_true", help="Opt out of auto-enriched system information")
    p_rb.set_defaults(func=cmd_report_bug)

    p_rf = sub.add_parser(
        "request-feature",
        help="Request a feature by creating a GitHub issue on the soft-ue-cli repo.",
        description=(
            "Creates a GitHub issue with structured feature request fields.\n\n"
            "AUTHENTICATION:\n"
            "  Set GITHUB_TOKEN env var or run 'gh auth login'.\n"
            "  Required scope: 'public_repo' (public repos) or 'repo' (private).\n\n"
            "EXAMPLES:\n"
            '  soft-ue-cli request-feature --title "add undo" --description "Support undo for spawn"\n'
            '  soft-ue-cli request-feature --title "feat" --description "desc" --use-case "motivation"\n'
            '  soft-ue-cli request-feature --title "feat" --description "desc" --priority nice-to-have'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_rf.add_argument("--title", required=True, help="Short feature summary")
    p_rf.add_argument(
        "--description", required=True,
        help="What the feature should do. Do not include secrets, tokens, passwords, or private credentials.",
    )
    p_rf.add_argument("--use-case", help="Motivation or use case")
    p_rf.add_argument(
        "--priority", choices=["enhancement", "nice-to-have"], default="enhancement",
        help="Priority label (default: enhancement)",
    )
    p_rf.set_defaults(func=cmd_request_feature)

    p_st = sub.add_parser(
        "submit-testimonial",
        help="Share a testimonial about soft-ue-cli via GitHub Discussions.",
        description=(
            "Posts a testimonial to the soft-ue-cli GitHub Discussions page.\n"
            "Auto-collects metadata (CLI version, usage streak, top tools).\n"
            "Prompts for user consent before posting unless --yes is passed.\n\n"
            "AUTHENTICATION:\n"
            "  Set GITHUB_TOKEN env var or run 'gh auth login'.\n"
            "  Required scope: 'repo' (private) or 'public_repo' (public).\n\n"
            "EXAMPLES:\n"
            '  soft-ue-cli submit-testimonial --message "Great tool for UE automation!"\n'
            '  soft-ue-cli submit-testimonial --message "Loved it" --agent-name "Claude Code" --rating 5\n'
            '  soft-ue-cli submit-testimonial --message "Helpful" --yes'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_st.add_argument("--message", required=True, help="Your testimonial message")
    p_st.add_argument("--agent-name", help="Name of the LLM agent submitting (e.g. 'Claude Code')")
    p_st.add_argument("--rating", type=int, choices=[1, 2, 3, 4, 5], help="Rating from 1-5")
    p_st.add_argument("--yes", "-y", action="store_true", help="Skip consent prompt")
    p_st.set_defaults(func=_cmd_submit_testimonial)

    # config
    p_config = sub.add_parser(
        "config",
        help="Read, write, and audit UE configuration files (INI, XML, JSON).",
        description=(
            "Unified access to all Unreal Engine configuration files with full\n"
            "hierarchy awareness. Supports INI (.ini), XML (BuildConfiguration.xml),\n"
            "and JSON (.uproject/.uplugin).\n\n"
            "EXAMPLES:\n"
            '  soft-ue-cli config tree\n'
            '  soft-ue-cli config get "[/Script/Engine.RendererSettings]r.Bloom"\n'
            '  soft-ue-cli config get "[/Script/Engine.RendererSettings]r.Bloom" --trace\n'
            '  soft-ue-cli config set "[/Script/Engine.RendererSettings]r.Bloom" "False" --layer ProjectDefault\n'
            '  soft-ue-cli config diff --layers ProjectDefault Base\n'
            '  soft-ue-cli config audit'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_config.add_argument("--engine-path", metavar="PATH", help="Path to UE engine directory")
    p_config.add_argument("--project-path", metavar="PATH", help="Path to UE project directory (auto-detected from cwd)")
    p_config.add_argument("--platform", metavar="PLATFORM", help="Target platform (e.g., Windows, Linux)")
    config_sub = p_config.add_subparsers(dest="config_action", required=True)

    p_ct = config_sub.add_parser("tree", help="Display config file hierarchy")
    p_ct.add_argument("--format", dest="config_format", choices=["ini", "xml", "project"], help="Filter by format")
    p_ct.add_argument("--type", dest="config_type", default="Engine", help="Config category (default: Engine)")
    p_ct.add_argument("--exists-only", action="store_true", help="Only show files that exist on disk")

    p_cg = config_sub.add_parser("get", help="Query config values")
    p_cg.add_argument("key", nargs="?", help="Config key in [Section]Key format (INI), dot.path (XML/JSON)")
    p_cg.add_argument("--layer", metavar="LAYER", help="Read from a specific hierarchy layer")
    p_cg.add_argument("--trace", action="store_true", help="Show value at every layer in the hierarchy")
    p_cg.add_argument("--source", choices=["ini", "xml", "project"], help="Force config source type")
    p_cg.add_argument("--search", metavar="PATTERN", help="Search all keys matching pattern")
    p_cg.add_argument("--type", dest="config_type", default="Engine", help="Config category (default: Engine)")

    p_cs = config_sub.add_parser("set", help="Write config values")
    p_cs.add_argument("key", help="Config key in [Section]Key format (INI), dot.path (XML/JSON)")
    p_cs.add_argument("value", help="Value to set")
    p_cs.add_argument("--layer", metavar="LAYER", help="Target hierarchy layer (required for INI/XML)")
    p_cs.add_argument("--source", choices=["ini", "xml", "project"], help="Force config source type")
    p_cs.add_argument("--type", dest="config_type", default="Engine", help="Config category (default: Engine)")

    p_cd = config_sub.add_parser("diff", help="Compare config across layers, platforms, or snapshots")
    p_cd.add_argument("--layers", nargs=2, metavar="LAYER", help="Compare two layers")
    p_cd.add_argument("--platforms", nargs=2, metavar="PLATFORM", help="Compare two platforms")
    p_cd.add_argument("--snapshot", metavar="GIT_REF", help="Compare current config against a git ref")
    p_cd.add_argument("--files", nargs=2, metavar="FILE", help="Compare two specific config files")
    p_cd.add_argument("--audit", action="store_true", help="Show all project overrides vs engine defaults")
    p_cd.add_argument("--type", dest="config_type", default="Engine", help="Config category (default: Engine)")

    p_ca = config_sub.add_parser("audit", help="Show all project overrides vs engine defaults")
    p_ca.add_argument("--format", dest="config_format", choices=["ini", "xml"], help="Filter by format")
    p_ca.add_argument("--type", dest="config_type", default="Engine", help="Config category (default: Engine)")

    p_config.set_defaults(func=cmd_config)

    # skills
    p_skills = sub.add_parser(
        "skills",
        help="Discover and retrieve LLM skill prompts shipped with the CLI.",
        description=(
            "Skills are markdown prompts that teach an LLM client how to perform\n"
            "complex workflows using soft-ue-cli commands. They include step-by-step\n"
            "instructions, type mapping tables, and pre-filled CLI commands.\n\n"
            "EXAMPLES:\n"
            "  soft-ue-cli skills list\n"
            "  soft-ue-cli skills get blueprint-to-cpp"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    skills_sub = p_skills.add_subparsers(dest="skills_action", required=True)
    skills_sub.add_parser("list", help="List all available skills")
    p_skills_get = skills_sub.add_parser("get", help="Print a skill's full content")
    p_skills_get.add_argument("skill_name", help="Skill name (e.g. blueprint-to-cpp)")
    p_skills.set_defaults(func=cmd_skills)

    # mcp-serve
    p_mcp = sub.add_parser(
        "mcp-serve",
        help="Run as an MCP server over stdio for AI editor integration.",
        description=(
            "Starts an MCP (Model Context Protocol) server over stdio transport.\n"
            "Exposes all soft-ue-cli commands as MCP tools and skills as MCP prompts.\n\n"
            "Requires the 'mcp' extra: pip install soft-ue-cli[mcp]\n\n"
            "USAGE IN MCP CLIENT CONFIG:\n"
            '  {\n'
            '    "mcpServers": {\n'
            '      "soft-ue-cli": {\n'
            '        "command": "soft-ue-cli",\n'
            '        "args": ["mcp-serve"]\n'
            '      }\n'
            '    }\n'
            '  }'
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p_mcp.set_defaults(func=cmd_mcp_serve)

    return parser


def main() -> None:
    # Ensure stdout/stderr can handle all Unicode (fixes cp949 crash on Korean Windows)
    import io
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
        sys.stderr.reconfigure(errors="replace")

    parser = build_parser()
    args = parser.parse_args()

    if args.server:
        os.environ["SOFT_UE_BRIDGE_URL"] = args.server
    if args.timeout:
        os.environ["SOFT_UE_BRIDGE_TIMEOUT"] = str(args.timeout)

    # Reverse MSYS/Git Bash path mangling for asset paths
    if hasattr(args, "asset_path") and args.asset_path:
        args.asset_path = _fix_msys_asset_path(args.asset_path)

    args.func(args)


if __name__ == "__main__":
    main()
