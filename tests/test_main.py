"""Tests for cli/soft_ue_cli/__main__.py ??argument parsing and cmd_setup output."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

from soft_ue_cli import __main__ as main_mod
from soft_ue_cli.__main__ import (
    _SCRIPTS_DIR,
    _claude_md_section,
    _default_build_and_relaunch_build_timeout,
    _parse_int_list,
    _parse_vector,
    _wait_for_build_and_relaunch,
    _validate_script_name,
    _wait_for_build_and_relaunch,
    build_parser,
    cmd_add_datatable_row,
    cmd_apply_widget_tree,
    cmd_wire_widget_navigation,
    cmd_add_co_group_child,
    cmd_add_co_mesh_option,
    cmd_add_co_node,
    cmd_add_co_parameter,
    cmd_add_graph_node,
    cmd_add_anim_state,
    cmd_add_anim_state_machine,
    cmd_add_anim_transition,
    cmd_batch_call,
    cmd_build_and_relaunch,
    cmd_call_function,
    cmd_capture_pie_screenshot,
    cmd_capture_screenshot,
    cmd_commands,
    cmd_compile_co,
    cmd_capture_viewport,
    cmd_compare_umg_screenshot,
    cmd_delete_script,
    cmd_exec_console_command,
    cmd_inspect_anim_instance,
    cmd_inspect_customizable_object_graph,
    cmd_inspect_mutable_diagnostics,
    cmd_inspect_mutable_parameters,
    cmd_inspect_pawn_possession,
    cmd_list_scripts,
    cmd_pie_session,
    cmd_pie_tick,
    cmd_query_enum,
    cmd_query_blueprint_graph,
    cmd_query_mpc,
    cmd_query_struct,
    cmd_release_asset_lock,
    cmd_reload_bridge_module,
    cmd_run_python_script,
    cmd_save_script,
    cmd_setup,
    cmd_status,
    cmd_wait_for_ready,
    cmd_set_co_base_mesh,
    cmd_set_co_layout_blocks,
    cmd_set_co_node_property,
    cmd_connect_co_pins,
    cmd_create_co_from_spec,
    cmd_query_asset,
    cmd_set_node_position,
    cmd_trigger_input,
    cmd_validate_class_path,
    cmd_trigger_live_coding,
    cmd_verify_umg_workflow,
    cmd_wire_co_slot_from_table,
)


# -- _parse_vector -------------------------------------------------------------


def test_commands_json_prints_command_metadata(capsys):
    parser = build_parser()
    args = parser.parse_args(["commands", "--json"])

    cmd_commands(args)

    payload = json.loads(capsys.readouterr().out)
    names = {entry["name"] for entry in payload["commands"]}
    assert payload["schema"] == "soft-ue.commands.v1"
    assert "umg layout" in names
    assert "compare-umg-layout" not in names


def test_commands_include_removed_prints_migration_metadata(capsys):
    parser = build_parser()
    args = parser.parse_args(["commands", "--include-removed", "--json"])

    cmd_commands(args)

    payload = json.loads(capsys.readouterr().out)
    removed = next(entry for entry in payload["commands"] if entry["name"] == "query-blueprint")
    assert removed["status"] == "removed"
    assert removed["canonical_command"] == "blueprint inspect"


def test_commands_filter_by_category_prints_human_rows(capsys):
    parser = build_parser()
    args = parser.parse_args(["commands", "--category", "compare"])

    cmd_commands(args)

    out = capsys.readouterr().out
    assert "umg layout" in out
    assert "compare-umg-layout" not in out
    assert "removed" not in out


def test_cmd_status_adds_diagnostics_for_stale_or_wrong_bridge_endpoint(capsys, monkeypatch):
    parser = build_parser()
    args = parser.parse_args(["status"])

    monkeypatch.setattr(
        "soft_ue_cli.__main__.health_check",
        lambda: {"error": "Client error '404 Not Found' for url 'http://127.0.0.1:8080/bridge'"},
    )
    monkeypatch.setattr("soft_ue_cli.__main__._detect_blocking_editor_modal", lambda: None)

    cmd_status(args)

    payload = json.loads(capsys.readouterr().out)
    assert payload["success"] is False
    assert payload["status"] == "not_ready"
    assert payload["diagnostics"]["lifecycle_state"] == "stale_endpoint_or_wrong_service"
    assert payload["diagnostics"]["error_code"] == "http_404_not_bridge"
    assert "SOFT_UE_BRIDGE_PORT" in payload["diagnostics"]["recovery_hint"]


def test_parse_vector_three_components():
    assert _parse_vector("1.0,2.0,3.0") == [1.0, 2.0, 3.0]


def test_parse_vector_integers():
    assert _parse_vector("0,100,200") == [0.0, 100.0, 200.0]


def test_parse_vector_negative():
    assert _parse_vector("-1.5,0,1.5") == [-1.5, 0.0, 1.5]


def test_parse_vector_invalid_exits():
    with pytest.raises(SystemExit) as exc:
        _parse_vector("a,b,c")
    assert exc.value.code == 1


def test_parse_vector_single_value():
    assert _parse_vector("42") == [42.0]


def test_parse_int_list_valid():
    assert _parse_int_list("0,100,200") == [0, 100, 200]


def test_parse_int_list_invalid_exits():
    with pytest.raises(SystemExit) as exc:
        _parse_int_list("a,b,c")
    assert exc.value.code == 1


# -- _claude_md_section --------------------------------------------------------


def test_claude_md_section_contains_cli_cmd():
    section = _claude_md_section("python -m soft_ue_cli")
    assert "python -m soft_ue_cli" in section
    assert "python -m soft_ue_cli --help" in section


def test_claude_md_section_has_heading():
    section = _claude_md_section("soft-ue-cli")
    assert "## Unreal Engine control" in section


# -- build_parser --------------------------------------------------------------


def test_parser_requires_command():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args([])


def test_parser_setup_no_args():
    parser = build_parser()
    args = parser.parse_args(["setup"])
    assert args.command == "setup"
    assert args.project_path is None
    assert args.plugin_src is None


def test_parser_setup_with_project_path():
    parser = build_parser()
    args = parser.parse_args(["setup", "/tmp/MyGame"])
    assert args.project_path == "/tmp/MyGame"


def test_parser_setup_with_plugin_src():
    parser = build_parser()
    args = parser.parse_args(["setup", "--plugin-src", "/opt/plugin"])
    assert args.plugin_src == "/opt/plugin"


def test_parser_spawn_actor():
    parser = build_parser()
    args = parser.parse_args(["spawn-actor", "PointLight"])
    assert args.actor_class == "PointLight"
    assert args.location is None
    assert args.rotation is None


def test_parser_spawn_actor_with_location():
    parser = build_parser()
    args = parser.parse_args(["spawn-actor", "PointLight", "--location", "0,0,200"])
    assert args.location == "0,0,200"


def test_parser_query_level_defaults():
    parser = build_parser()
    args = parser.parse_args(["query-level"])
    assert args.limit == 100
    assert args.components is False
    assert args.world is None


def test_parser_query_level_world():
    parser = build_parser()
    args = parser.parse_args(["query-level", "--world", "pie"])
    assert args.world == "pie"


def test_cmd_query_level_forwards_world():
    parser = build_parser()
    args = parser.parse_args(["query-level", "--world", "pie", "--search", "BP_Player*"])
    with patch("soft_ue_cli.__main__._run_tool", return_value={}) as mock_run:
        with patch("soft_ue_cli.__main__._print_json"):
            args.func(args)
    mock_run.assert_called_once_with("query-level", {"limit": 100, "search": "BP_Player*", "world": "pie"})


def test_parser_get_logs_defaults():
    parser = build_parser()
    args = parser.parse_args(["get-logs"])
    assert args.lines == 100
    assert args.raw is False
    assert args.contains is None
    assert args.since is None
    assert args.tail_follow is False


def test_parser_set_console_var():
    parser = build_parser()
    args = parser.parse_args(["set-console-var", "r.VSync", "0"])
    assert args.name == "r.VSync"
    assert args.value == "0"


def test_parser_get_console_var():
    parser = build_parser()
    args = parser.parse_args(["get-console-var", "t.MaxFPS"])
    assert args.name == "t.MaxFPS"


def test_parser_build_and_relaunch_flags():
    parser = build_parser()
    args = parser.parse_args([
        "build-and-relaunch",
        "--config",
        "Debug",
        "--skip-relaunch",
        "--wait",
        "--build-timeout",
        "1200",
        "--relaunch-timeout",
        "180",
        "--startup-recovery",
        "skip",
        "--remember-startup-recovery",
        "--startup-marker-timeout",
        "30",
    ])
    assert args.config == "Debug"
    assert args.skip_relaunch is True
    assert args.wait is True
    assert args.build_timeout == 1200
    assert args.relaunch_timeout == 180
    assert args.startup_recovery == "skip"
    assert args.remember_startup_recovery is True
    assert args.startup_marker_timeout == 30


def test_cmd_build_and_relaunch_forwards_startup_marker_timeout():
    parser = build_parser()
    args = parser.parse_args(["build-and-relaunch", "--startup-marker-timeout", "45"])
    with patch("soft_ue_cli.__main__.health_check", return_value={"running": True}), patch(
        "soft_ue_cli.__main__._run_tool", return_value={"success": True}
    ) as mock_run:
        with patch("soft_ue_cli.__main__._print_json"):
            args.func(args)
    mock_run.assert_called_once_with("build-and-relaunch", {"startup_marker_timeout": 45})


def test_parser_wait_for_ready_alias_and_timeout():
    parser = build_parser()
    args = parser.parse_args(["await-bridge", "--timeout", "5", "--poll-interval", "0.25"])
    assert args.func == cmd_wait_for_ready
    assert args.timeout == 5.0
    assert args.poll_interval == 0.25


def test_parser_trigger_live_coding_scope_flags():
    parser = build_parser()
    args = parser.parse_args([
        "trigger-live-coding",
        "--module",
        "SoftUEBridgeEditor",
        "--plugin",
        "SoftUEBridge",
        "--no-wait",
    ])
    assert args.module == "SoftUEBridgeEditor"
    assert args.plugin == "SoftUEBridge"
    assert args.no_wait is True


def test_parser_reload_bridge_module_defaults():
    parser = build_parser()
    args = parser.parse_args(["reload-bridge-module"])
    assert args.module == "SoftUEBridgeEditor"


def test_parser_get_logs_follow_args():
    parser = build_parser()
    args = parser.parse_args(["get-logs", "--contains", "warning", "--since", "42", "--tail-follow"])
    assert args.contains == "warning"
    assert args.since == "42"
    assert args.tail_follow is True


def test_parser_inspect_uasset():
    parser = build_parser()
    args = parser.parse_args(["asset", "inspect-file", "BP_Player.uasset", "--sections", "summary,properties", "--format", "json"])
    assert args.file_path == "BP_Player.uasset"
    assert args.sections == "summary,properties"
    assert args.format == "json"


def test_parser_diff_uasset():
    parser = build_parser()
    args = parser.parse_args(["asset", "diff-file", "BP_Old.uasset", "BP_New.uasset", "--sections", "properties"])
    assert args.left_file == "BP_Old.uasset"
    assert args.right_file == "BP_New.uasset"
    assert args.sections == "properties"


def test_parser_get_property_world():
    parser = build_parser()
    args = parser.parse_args(["get-property", "BP_Player_C_0", "Health", "--world", "pie"])
    assert args.world == "pie"


def test_cmd_get_property_forwards_world():
    parser = build_parser()
    args = parser.parse_args(["get-property", "BP_Player_C_0", "Health", "--world", "pie"])
    with patch("soft_ue_cli.__main__._run_tool", return_value={}) as mock_run:
        with patch("soft_ue_cli.__main__._print_json"):
            args.func(args)
    mock_run.assert_called_once_with(
        "get-property",
        {"actor_name": "BP_Player_C_0", "property_name": "Health", "world": "pie"},
    )


def test_parser_metasound_inspect():
    parser = build_parser()
    args = parser.parse_args(["metasound", "inspect", "/Game/Audio/MS_Foo"])
    assert args.asset_path == "/Game/Audio/MS_Foo"


def test_cmd_metasound_inspect_forwards_asset_path():
    parser = build_parser()
    args = parser.parse_args(["metasound", "inspect", "/Game/Audio/MS_Foo"])
    with patch("soft_ue_cli.__main__._run_tool", return_value={}) as mock_run:
        with patch("soft_ue_cli.__main__._print_json"):
            args.func(args)
    mock_run.assert_called_once_with("metasound-inspect", {"asset_path": "/Game/Audio/MS_Foo"})


def test_parser_call_function_no_args():
    parser = build_parser()
    args = parser.parse_args(["call-function", "BP_Hero", "Jump"])
    assert args.legacy_actor_name == "BP_Hero"
    assert args.legacy_function_name == "Jump"
    assert args.args is None


def test_parser_server_override():
    parser = build_parser()
    args = parser.parse_args(["--server", "http://remote:9000", "status"])
    assert args.server == "http://remote:9000"


# -- cmd_setup output ----------------------------------------------------------


def test_cmd_setup_uses_cwd_by_default(tmp_path, capsys, monkeypatch):
    (tmp_path / "MyGame.uproject").write_text("{}")
    monkeypatch.chdir(tmp_path)
    parser = build_parser()
    args = parser.parse_args(["setup"])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "MyGame.uproject" in out
    assert "SoftUEBridge" in out
    assert "CLAUDE.md" in out


def test_cmd_setup_uses_given_path(tmp_path, capsys):
    (tmp_path / "TestGame.uproject").write_text("{}")
    parser = build_parser()
    args = parser.parse_args(["setup", str(tmp_path)])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "TestGame.uproject" in out


def test_cmd_setup_no_uproject_shows_placeholder(tmp_path, capsys, monkeypatch):
    monkeypatch.chdir(tmp_path)
    parser = build_parser()
    args = parser.parse_args(["setup"])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "<YourGame>.uproject" in out


def test_cmd_setup_contains_check_setup_command(tmp_path, capsys, monkeypatch):
    monkeypatch.chdir(tmp_path)
    parser = build_parser()
    args = parser.parse_args(["setup"])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "check-setup" in out
    assert sys.executable in out


def test_cmd_setup_contains_plugin_src(tmp_path, capsys, monkeypatch):
    monkeypatch.chdir(tmp_path)
    parser = build_parser()
    args = parser.parse_args(["setup", "--plugin-src", "/custom/plugin"])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "/custom/plugin" in out or "custom" in out


def test_cmd_setup_warns_to_refresh_source_timestamps(tmp_path, capsys, monkeypatch):
    monkeypatch.chdir(tmp_path)
    parser = build_parser()
    args = parser.parse_args(["setup"])
    cmd_setup(args)
    out = capsys.readouterr().out
    assert "Refresh the copied plugin Source timestamps" in out
    assert "UnrealHeaderTool" in out


# -- script management (save / list / delete / run --name) ---------------------

import soft_ue_cli.__main__ as _main_mod


@pytest.fixture()
def scripts_home(tmp_path, monkeypatch):
    """Redirect _SCRIPTS_DIR to a temp directory."""
    fake_dir = tmp_path / ".soft-ue-bridge" / "scripts"
    monkeypatch.setattr(_main_mod, "_SCRIPTS_DIR", fake_dir)
    return fake_dir


def test_save_script_inline(scripts_home, capsys):
    parser = build_parser()
    args = parser.parse_args(["save-script", "hello", "--script", "print('hi')"])
    cmd_save_script(args)
    saved = scripts_home / "hello.py"
    assert saved.exists()
    assert saved.read_text(encoding="utf-8") == "print('hi')"
    out = json.loads(capsys.readouterr().out)
    assert out["status"] == "ok"
    assert out["name"] == "hello"


def test_save_script_from_file(tmp_path, scripts_home, capsys):
    src = tmp_path / "my_script.py"
    src.write_text("import unreal", encoding="utf-8")
    parser = build_parser()
    args = parser.parse_args(["save-script", "mymod", "--script-path", str(src)])
    cmd_save_script(args)
    assert (scripts_home / "mymod.py").read_text(encoding="utf-8") == "import unreal"
    out = json.loads(capsys.readouterr().out)
    assert out["status"] == "ok"


def test_save_script_no_source_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["save-script", "empty"])
    with pytest.raises(SystemExit) as exc:
        cmd_save_script(args)
    assert exc.value.code == 1


def test_save_script_both_sources_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["save-script", "x", "--script", "pass", "--script-path", "/tmp/f.py"])
    with pytest.raises(SystemExit) as exc:
        cmd_save_script(args)
    assert exc.value.code == 1


def test_save_script_missing_file_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["save-script", "x", "--script-path", "/nonexistent/file.py"])
    with pytest.raises(SystemExit) as exc:
        cmd_save_script(args)
    assert exc.value.code == 1


def test_save_script_invalid_name_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["save-script", "../evil", "--script", "pass"])
    with pytest.raises(SystemExit) as exc:
        cmd_save_script(args)
    assert exc.value.code == 1


def test_list_scripts_empty(scripts_home, capsys):
    parser = build_parser()
    args = parser.parse_args(["list-scripts"])
    cmd_list_scripts(args)
    out = json.loads(capsys.readouterr().out)
    assert out["scripts"] == []
    assert out["count"] == 0


def test_list_scripts_no_dir_created(tmp_path, monkeypatch, capsys):
    """list-scripts must not create the scripts directory if it doesn't exist."""
    fake_dir = tmp_path / "no-scripts-here"
    monkeypatch.setattr(_main_mod, "_SCRIPTS_DIR", fake_dir)
    parser = build_parser()
    args = parser.parse_args(["list-scripts"])
    cmd_list_scripts(args)
    assert not fake_dir.exists()


def test_list_scripts_shows_saved(scripts_home, capsys):
    scripts_home.mkdir(parents=True, exist_ok=True)
    (scripts_home / "alpha.py").write_text("pass", encoding="utf-8")
    (scripts_home / "beta.py").write_text("pass", encoding="utf-8")
    parser = build_parser()
    args = parser.parse_args(["list-scripts"])
    cmd_list_scripts(args)
    out = json.loads(capsys.readouterr().out)
    names = [s["name"] for s in out["scripts"]]
    assert "alpha" in names
    assert "beta" in names
    assert out["count"] == 2


def test_delete_script(scripts_home, capsys):
    scripts_home.mkdir(parents=True, exist_ok=True)
    (scripts_home / "todelete.py").write_text("pass", encoding="utf-8")
    parser = build_parser()
    args = parser.parse_args(["delete-script", "todelete"])
    cmd_delete_script(args)
    assert not (scripts_home / "todelete.py").exists()
    out = json.loads(capsys.readouterr().out)
    assert out["status"] == "ok"
    assert out["name"] == "todelete"


def test_delete_script_not_found_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["delete-script", "ghost"])
    with pytest.raises(SystemExit) as exc:
        cmd_delete_script(args)
    assert exc.value.code == 1


def test_delete_script_invalid_name_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["delete-script", "../etc/passwd"])
    with pytest.raises(SystemExit) as exc:
        cmd_delete_script(args)
    assert exc.value.code == 1


def test_run_python_script_by_name(scripts_home, capsys):
    scripts_home.mkdir(parents=True, exist_ok=True)
    (scripts_home / "runner.py").write_text("print('run')", encoding="utf-8")
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--name", "runner"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"output": "run"}) as mock_call:
        cmd_run_python_script(args)
    mock_call.assert_called_once_with("run-python-script", {"script_path": str((scripts_home / "runner.py").resolve())})


def test_run_python_script_by_name_not_found_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--name", "missing"])
    with pytest.raises(SystemExit) as exc:
        cmd_run_python_script(args)
    assert exc.value.code == 1


def test_run_python_script_no_args_exits(scripts_home):
    parser = build_parser()
    args = parser.parse_args(["run-python-script"])
    with pytest.raises(SystemExit) as exc:
        cmd_run_python_script(args)
    assert exc.value.code == 1


def test_run_python_script_path_reads_file(tmp_path):
    script_path = tmp_path / "runtime_check.py"
    script_path.write_text("print('ok')", encoding="utf-8")

    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--script-path", str(script_path), "--world", "pie"])

    with patch("soft_ue_cli.__main__._ensure_pie_running") as mock_ensure, patch(
        "soft_ue_cli.__main__.call_tool", return_value={"output": "ok"}
    ) as mock_call:
        cmd_run_python_script(args)

    mock_ensure.assert_called_once()
    mock_call.assert_called_once_with(
        "run-python-script",
        {
            "script_path": str(script_path.resolve()),
            "world": "pie",
        },
    )


def test_run_python_script_allow_unsafe_python_calls_routes_to_bridge_tool():
    parser = build_parser()
    script = "import unreal; unreal.IKRetargetBatchOperation.duplicate_and_retarget([])"
    args = parser.parse_args([
        "run-python-script",
        "--script",
        script,
        "--allow-unsafe-python-calls",
    ])

    with patch("soft_ue_cli.__main__.call_tool", return_value={"output": "ok"}) as mock_call:
        cmd_run_python_script(args)

    mock_call.assert_called_once_with(
        "run-python-script",
        {
            "script": script,
            "allow_unsafe_python_calls": True,
        },
    )


def test_run_python_script_args_route_to_bridge_tool_sys_argv(tmp_path):
    script_path = tmp_path / "argv_check.py"
    script_path.write_text("import sys; print(sys.argv)", encoding="utf-8")

    parser = build_parser()
    args = parser.parse_args([
        "run-python-script",
        "--script-path",
        str(script_path),
        "--args",
        "alpha",
        "--flag=value",
    ])

    with patch("soft_ue_cli.__main__.call_tool", return_value={"output": "ok"}) as mock_call:
        cmd_run_python_script(args)

    mock_call.assert_called_once_with(
        "run-python-script",
        {
            "script_path": str(script_path.resolve()),
            "script_args": ["alpha", "--flag=value"],
        },
    )


def test_run_python_script_world_pie_auto_start():
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--script", "print('ok')", "--world", "pie", "--auto-start-pie"])

    with patch("soft_ue_cli.__main__._ensure_pie_running") as mock_ensure, patch(
        "soft_ue_cli.__main__.call_tool", return_value={"output": "ok"}
    ):
        cmd_run_python_script(args)

    mock_ensure.assert_called_once()


def test_run_python_script_path_missing_exits(tmp_path):
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--script-path", str(tmp_path / "missing.py")])

    with pytest.raises(SystemExit) as exc:
        cmd_run_python_script(args)

    assert exc.value.code == 1


# -- _validate_script_name -----------------------------------------------------


def test_validate_script_name_valid():
    _validate_script_name("my-script_01")  # should not raise


def test_validate_script_name_path_traversal_exits():
    with pytest.raises(SystemExit) as exc:
        _validate_script_name("../evil")
    assert exc.value.code == 1


def test_validate_script_name_empty_exits():
    with pytest.raises(SystemExit) as exc:
        _validate_script_name("")
    assert exc.value.code == 1


def test_validate_script_name_slash_exits():
    with pytest.raises(SystemExit) as exc:
        _validate_script_name("foo/bar")
    assert exc.value.code == 1


# -- parser tests for new subcommands ------------------------------------------


def test_query_blueprint_graph_parses_recursive_and_node_class_filters():
    args = build_parser().parse_args([
        "blueprint",
        "graph",
        "inspect",
        "/Game/Animation/ABP_Player",
        "--recursive",
        "--node-class",
        "AnimGraphNode_StateMachine,AnimGraphNode_BlendStack",
        "--include-anim-props",
    ])

    assert args.recursive is True
    assert args.node_class == "AnimGraphNode_StateMachine,AnimGraphNode_BlendStack"


def test_query_blueprint_graph_forwards_recursive_and_node_class_filters():
    ns = argparse.Namespace(
        asset_path="/Game/Animation/ABP_Player",
        node_guid=None,
        callable_name=None,
        list_callables=False,
        graph_name=None,
        graph_type=None,
        include_positions=False,
        search=None,
        include_anim_props=True,
        recursive=True,
        node_class="AnimGraphNode_StateMachine,AnimGraphNode_BlendStack",
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"graphs": []}) as mock_run, patch(
        "soft_ue_cli.__main__._print_json"
    ):
        cmd_query_blueprint_graph(ns)

    mock_run.assert_called_once_with(
        "query-blueprint-graph",
        {
            "asset_path": "/Game/Animation/ABP_Player",
            "include_anim_node_properties": True,
            "recursive": True,
            "node_class": "AnimGraphNode_StateMachine,AnimGraphNode_BlendStack",
        },
    )


def test_parser_save_script():
    parser = build_parser()
    args = parser.parse_args(["save-script", "myscript", "--script", "pass"])
    assert args.name == "myscript"
    assert args.script == "pass"
    assert args.script_path is None


def test_parser_save_script_path():
    parser = build_parser()
    args = parser.parse_args(["save-script", "myscript", "--script-path", "/tmp/s.py"])
    assert args.script_path == "/tmp/s.py"


def test_parser_list_scripts():
    parser = build_parser()
    args = parser.parse_args(["list-scripts"])
    assert args.func == cmd_list_scripts


def test_parser_delete_script():
    parser = build_parser()
    args = parser.parse_args(["delete-script", "foo"])
    assert args.name == "foo"


def test_parser_run_python_script_name():
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--name", "myscript"])
    assert args.name == "myscript"


def test_parser_run_python_script_world():
    parser = build_parser()
    args = parser.parse_args(["run-python-script", "--script", "print('x')", "--world", "pie"])
    assert args.world == "pie"


def test_cmd_build_and_relaunch_forwards_args(capsys):
    parser = build_parser()
    args = parser.parse_args(["build-and-relaunch", "--config", "Debug", "--skip-relaunch"])

    with patch("soft_ue_cli.__main__.health_check", return_value={"running": True}), patch(
        "soft_ue_cli.__main__._run_tool", return_value={"success": True}
    ) as mock_run:
        cmd_build_and_relaunch(args)

    mock_run.assert_called_once_with(
        "build-and-relaunch",
        {"build_config": "Debug", "skip_relaunch": True},
    )


def test_cmd_build_and_relaunch_forwards_toolchain_overrides(capsys):
    parser = build_parser()
    args = parser.parse_args([
        "build-and-relaunch",
        "--config",
        "Debug",
        "--compiler",
        "VisualStudio2022",
        "--compiler-version",
        "14.38.33130",
        "--toolchain",
        "14.38.33130",
    ])

    with patch("soft_ue_cli.__main__.health_check", return_value={"running": True}), patch(
        "soft_ue_cli.__main__._run_tool", return_value={"success": True}
    ) as mock_run:
        cmd_build_and_relaunch(args)

    mock_run.assert_called_once_with(
        "build-and-relaunch",
        {
            "build_config": "Debug",
            "compiler": "VisualStudio2022",
            "compiler_version": "14.38.33130",
            "toolchain": "14.38.33130",
        },
    )


def test_wait_for_build_and_relaunch_reads_utf8_bom_status_file(tmp_path, monkeypatch, capsys):
    status_path = tmp_path / "build_status.json"
    status_path.write_text('{"success": true}', encoding="utf-8-sig")

    original_read_text = Path.read_text

    def read_text_with_ansi_default_failure(self, *args, **kwargs):
        if self == status_path and kwargs.get("encoding") is None:
            raise UnicodeDecodeError("cp949", b"\xef\xbb\xbf", 2, 3, "illegal multibyte sequence")
        return original_read_text(self, *args, **kwargs)

    monkeypatch.setattr(Path, "read_text", read_text_with_ansi_default_failure)

    _wait_for_build_and_relaunch(
        {
            "build_status_path": str(status_path),
            "project": "ExampleProject",
        },
        skip_relaunch=True,
        poll_interval=0.0,
        build_timeout=0.1,
    )

    result = json.loads(capsys.readouterr().out)
    assert result["success"] is True
    assert result["status"] == "build_succeeded"


def test_build_and_relaunch_default_build_timeout_uses_bridge_timeout(monkeypatch):
    monkeypatch.setenv("SOFT_UE_BRIDGE_TIMEOUT", "1200")

    assert _default_build_and_relaunch_build_timeout() == 1200.0


def test_cmd_build_and_relaunch_wait_forwards_timeout_overrides():
    parser = build_parser()
    args = parser.parse_args([
        "build-and-relaunch",
        "--wait",
        "--build-timeout",
        "12",
        "--relaunch-timeout",
        "3",
    ])
    result = {
        "success": True,
        "build_status_path": "BuildAndRelaunch.status.json",
        "build_log_path": "BuildAndRelaunch.log",
    }

    with patch("soft_ue_cli.__main__.health_check", return_value={"running": True}), patch(
        "soft_ue_cli.__main__._run_tool", return_value=result
    ), patch(
        "soft_ue_cli.__main__._wait_for_build_and_relaunch"
    ) as mock_wait:
        cmd_build_and_relaunch(args)

    mock_wait.assert_called_once_with(
        result,
        skip_relaunch=False,
        startup_recovery="ask",
        remember_startup_recovery=None,
        build_timeout=12,
        relaunch_timeout=3,
    )


def test_parser_build_and_relaunch_offline_fallback_flags():
    parser = build_parser()
    args = parser.parse_args([
        "build-and-relaunch",
        "--wait",
        "--project",
        "D:/Project/Game.uproject",
        "--editor-exe",
        "D:/UE/Engine/Binaries/Win64/UnrealEditor.exe",
        "--build-bat",
        "D:/UE/Engine/Build/BatchFiles/Build.bat",
        "--no-offline-fallback",
    ])

    assert args.project == "D:/Project/Game.uproject"
    assert args.editor_exe == "D:/UE/Engine/Binaries/Win64/UnrealEditor.exe"
    assert args.build_bat == "D:/UE/Engine/Build/BatchFiles/Build.bat"
    assert args.offline_fallback is False


def test_cmd_build_and_relaunch_uses_offline_fallback_when_bridge_unavailable(capsys):
    parser = build_parser()
    args = parser.parse_args(["build-and-relaunch", "--wait", "--project", "Game.uproject"])

    with patch("soft_ue_cli.__main__.health_check", return_value={"error": "connection refused"}), patch(
        "soft_ue_cli.__main__._run_offline_build_and_relaunch",
        return_value={"success": True, "status": "ready"},
    ) as mock_offline, patch("soft_ue_cli.__main__._run_tool") as mock_run:
        cmd_build_and_relaunch(args)

    mock_offline.assert_called_once_with(args)
    mock_run.assert_not_called()
    assert json.loads(capsys.readouterr().out)["status"] == "ready"


def test_offline_build_and_relaunch_builds_launches_and_waits(tmp_path, monkeypatch):
    project = tmp_path / "MyGame.uproject"
    project.write_text("{}", encoding="utf-8")
    engine_dir = tmp_path / "UE" / "Engine"
    build_bat = engine_dir / "Build" / "BatchFiles" / "Build.bat"
    editor_exe = engine_dir / "Binaries" / "Win64" / "UnrealEditor.exe"
    build_bat.parent.mkdir(parents=True)
    editor_exe.parent.mkdir(parents=True)
    build_bat.write_text("@echo off", encoding="utf-8")
    editor_exe.write_text("", encoding="utf-8")

    run_calls = []
    popen_calls = []

    def fake_run(command, **kwargs):
        run_calls.append((command, kwargs))
        return subprocess.CompletedProcess(command, 0, stdout="build ok", stderr="")

    def fake_popen(command, **kwargs):
        popen_calls.append((command, kwargs))
        return object()

    health_responses = iter([{"error": "not ready"}, {"running": True}])
    monkeypatch.setattr(main_mod.subprocess, "run", fake_run)
    monkeypatch.setattr(main_mod.subprocess, "Popen", fake_popen)
    monkeypatch.setattr(main_mod, "health_check", lambda **_kwargs: next(health_responses))
    monkeypatch.setattr(main_mod.time, "sleep", lambda _seconds: None)

    args = argparse.Namespace(
        project=str(project),
        editor_exe=str(editor_exe),
        build_bat=str(build_bat),
        config="Debug",
        wait=True,
        skip_relaunch=False,
        build_timeout=30,
        relaunch_timeout=5,
    )

    result = main_mod._run_offline_build_and_relaunch(args)

    assert result["success"] is True
    assert result["status"] == "ready"
    assert run_calls[0][0][0] == str(build_bat)
    assert "MyGameEditor" in run_calls[0][0]
    assert "Debug" in run_calls[0][0]
    assert f"-Project={project}" in run_calls[0][0]
    assert popen_calls[0][0] == [str(editor_exe), str(project)]


def test_offline_build_and_relaunch_passes_toolchain_overrides(tmp_path, monkeypatch):
    project = tmp_path / "MyGame.uproject"
    project.write_text("{}", encoding="utf-8")
    build_bat = tmp_path / "Build.bat"
    editor_exe = tmp_path / "UnrealEditor.exe"
    build_bat.write_text("@echo off", encoding="utf-8")
    editor_exe.write_text("", encoding="utf-8")
    run_calls = []

    def fake_run(command, **kwargs):
        run_calls.append(command)
        return subprocess.CompletedProcess(command, 0, stdout="ok", stderr="")

    monkeypatch.setattr(main_mod.subprocess, "run", fake_run)
    monkeypatch.setattr(main_mod.subprocess, "Popen", lambda *_args, **_kwargs: None)

    args = argparse.Namespace(
        project=str(project),
        editor_exe=str(editor_exe),
        build_bat=str(build_bat),
        config="Development",
        wait=True,
        skip_relaunch=True,
        build_timeout=30,
        relaunch_timeout=5,
        compiler="VisualStudio2022",
        compiler_version="14.38.33130",
        toolchain="14.38.33130",
    )

    result = main_mod._run_offline_build_and_relaunch(args)

    assert result["success"] is True
    assert "-Compiler=VisualStudio2022" in run_calls[0]
    assert "-CompilerVersion=14.38.33130" in run_calls[0]
    assert result["compiler"] == "VisualStudio2022"
    assert result["compiler_version"] == "14.38.33130"


def test_offline_build_discovers_engine_from_uproject_engine_association(tmp_path, monkeypatch):
    project = tmp_path / "MyGame.uproject"
    project.write_text('{"EngineAssociation": "5.6"}', encoding="utf-8")
    program_files = tmp_path / "Program Files"
    engine_dir = program_files / "Epic Games" / "UE_5.6" / "Engine"
    build_bat = engine_dir / "Build" / "BatchFiles" / "Build.bat"
    editor_exe = engine_dir / "Binaries" / "Win64" / "UnrealEditor.exe"
    build_bat.parent.mkdir(parents=True)
    editor_exe.parent.mkdir(parents=True)
    build_bat.write_text("@echo off", encoding="utf-8")
    editor_exe.write_text("", encoding="utf-8")
    monkeypatch.setenv("ProgramFiles", str(program_files))
    monkeypatch.delenv("UNREAL_ENGINE_DIR", raising=False)
    monkeypatch.delenv("UE_ENGINE_DIR", raising=False)

    args = argparse.Namespace(editor_exe=None, build_bat=None)

    assert main_mod._discover_unreal_build_tools(args, project) == (editor_exe.resolve(), build_bat.resolve())


def test_offline_build_and_relaunch_reports_build_failure(tmp_path, monkeypatch):
    project = tmp_path / "MyGame.uproject"
    project.write_text("{}", encoding="utf-8")
    build_bat = tmp_path / "Build.bat"
    editor_exe = tmp_path / "UnrealEditor.exe"
    build_bat.write_text("@echo off", encoding="utf-8")
    editor_exe.write_text("", encoding="utf-8")

    def fake_run(command, **kwargs):
        return subprocess.CompletedProcess(command, 6, stdout="", stderr="compiler error")

    monkeypatch.setattr(main_mod.subprocess, "run", fake_run)
    monkeypatch.setattr(main_mod.subprocess, "Popen", lambda *_args, **_kwargs: pytest.fail("should not launch"))

    args = argparse.Namespace(
        project=str(project),
        editor_exe=str(editor_exe),
        build_bat=str(build_bat),
        config="Development",
        wait=True,
        skip_relaunch=False,
        build_timeout=30,
        relaunch_timeout=5,
    )

    result = main_mod._run_offline_build_and_relaunch(args)

    assert result["success"] is False
    assert result["status"] == "build_failed"
    assert result["exit_code"] == 2
    assert "compiler error" in result["build_output"]


def test_cmd_wait_for_ready_returns_when_bridge_health_succeeds(capsys, monkeypatch):
    parser = build_parser()
    args = parser.parse_args(["wait-for-ready", "--timeout", "5"])
    monkeypatch.setattr(
        "soft_ue_cli.__main__.health_check",
        lambda **_kwargs: {"running": True, "name": "soft-ue-bridge"},
    )
    monkeypatch.setattr("soft_ue_cli.__main__.get_server_url", lambda: "http://127.0.0.1:8080")
    monkeypatch.setattr("time.monotonic", lambda: 0.0)

    cmd_wait_for_ready(args)

    result = json.loads(capsys.readouterr().out)
    assert result["success"] is True
    assert result["status"] == "ready"
    assert result["server_url"] == "http://127.0.0.1:8080"
    assert result["health"]["running"] is True


def test_cmd_wait_for_ready_timeout_reports_last_error(capsys, monkeypatch):
    parser = build_parser()
    args = parser.parse_args(["wait-for-ready", "--timeout", "2", "--poll-interval", "1"])
    clock = {"now": 0.0}

    def fake_sleep(seconds):
        clock["now"] += seconds

    monkeypatch.setattr("soft_ue_cli.__main__.health_check", lambda **_kwargs: {"error": "connection refused"})
    monkeypatch.setattr("soft_ue_cli.__main__.get_server_url", lambda: "http://127.0.0.1:8080")
    monkeypatch.setattr("soft_ue_cli.__main__._detect_blocking_editor_modal", lambda: None)
    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])

    with pytest.raises(SystemExit) as exc:
        cmd_wait_for_ready(args)

    assert exc.value.code == 1
    captured = capsys.readouterr()
    result = json.loads(captured.out)
    assert result["success"] is False
    assert result["status"] == "timeout"
    assert result["last_error"] == "connection refused"
    assert "bridge did not become ready within 2s" in captured.err


def test_cmd_wait_for_ready_timeout_reports_restore_packages_modal(capsys, monkeypatch):
    parser = build_parser()
    args = parser.parse_args(["wait-for-ready", "--timeout", "2", "--poll-interval", "1"])
    clock = {"now": 0.0}

    def fake_sleep(seconds):
        clock["now"] += seconds

    monkeypatch.setattr("soft_ue_cli.__main__.health_check", lambda **_kwargs: {"error": "timed out"})
    monkeypatch.setattr("soft_ue_cli.__main__.get_server_url", lambda: "http://127.0.0.1:8080")
    monkeypatch.setattr(
        "soft_ue_cli.__main__._detect_blocking_editor_modal",
        lambda: {
            "kind": "editor_startup_modal",
            "title": "Restore Packages",
            "process_id": 1234,
            "recovery_hint": "Close the editor, clear the package restore marker, then relaunch.",
        },
    )
    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])

    with pytest.raises(SystemExit) as exc:
        cmd_wait_for_ready(args)

    assert exc.value.code == 1
    captured = capsys.readouterr()
    result = json.loads(captured.out)
    assert result["status"] == "editor_blocked_by_modal"
    assert result["diagnostics"]["modal"]["title"] == "Restore Packages"
    assert result["diagnostics"]["lifecycle_state"] == "editor_blocked_by_modal"
    assert "Restore Packages" in captured.err


def test_cmd_wait_for_ready_launches_editor_before_polling(capsys, monkeypatch, tmp_path):
    uproject_path = tmp_path / "MyGame.uproject"
    uproject_path.write_text("{}", encoding="utf-8")
    parser = build_parser()
    args = parser.parse_args(["wait-for-ready", "--launch-editor", str(uproject_path)])
    launched: list[str] = []

    monkeypatch.setattr("soft_ue_cli.__main__._launch_editor_for_wait", lambda path: launched.append(path))
    monkeypatch.setattr(
        "soft_ue_cli.__main__.health_check",
        lambda **_kwargs: {"running": True, "name": "soft-ue-bridge"},
    )
    monkeypatch.setattr("soft_ue_cli.__main__.get_server_url", lambda: "http://127.0.0.1:8080")
    monkeypatch.setattr("time.monotonic", lambda: 0.0)

    cmd_wait_for_ready(args)

    assert launched == [str(uproject_path)]
    assert json.loads(capsys.readouterr().out)["status"] == "ready"


def test_wait_for_build_and_relaunch_reports_intermediate_status(tmp_path, capsys, monkeypatch):
    status_path = tmp_path / "BuildAndRelaunch.status.json"
    log_path = tmp_path / "BuildAndRelaunch.log"
    log_path.write_text("build log\n", encoding="utf-8")
    pending_statuses = [
        {
            "complete": False,
            "success": False,
            "stage": "waiting_for_editor_shutdown",
            "message": "Waiting for editor process 123 to exit.",
        },
        {
            "complete": False,
            "success": False,
            "stage": "building",
            "message": "Running Build.bat for MyGameEditor.",
        },
        {
            "complete": True,
            "success": True,
            "stage": "completed",
            "exit_code": 0,
            "message": "Build completed successfully.",
        },
    ]
    clock = {"now": 0.0}

    def fake_sleep(_seconds):
        clock["now"] += 1.0
        if pending_statuses:
            status_path.write_text(json.dumps(pending_statuses.pop(0)), encoding="utf-8")

    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])

    _wait_for_build_and_relaunch(
        {
            "project": "MyGame",
            "build_status_path": str(status_path),
            "build_log_path": str(log_path),
        },
        skip_relaunch=True,
        poll_interval=0.1,
        build_timeout=10.0,
    )

    captured = capsys.readouterr()
    assert "waiting_for_editor_shutdown" in captured.err
    assert "building" in captured.err
    assert json.loads(captured.out)["status"] == "build_succeeded"


def test_wait_for_build_and_relaunch_timeout_reports_last_stage(tmp_path, capsys, monkeypatch):
    status_path = tmp_path / "BuildAndRelaunch.status.json"
    log_path = tmp_path / "BuildAndRelaunch.log"
    status_path.write_text(
        json.dumps(
            {
                "complete": False,
                "success": False,
                "stage": "building",
                "message": "Running Build.bat for MyGameEditor.",
            }
        ),
        encoding="utf-8",
    )
    log_path.write_text("UnrealBuildTool is still running\n", encoding="utf-8")
    clock = {"now": 0.0}

    def fake_sleep(seconds):
        clock["now"] += seconds

    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])

    with pytest.raises(SystemExit) as exc:
        _wait_for_build_and_relaunch(
            {
                "project": "MyGame",
                "build_status_path": str(status_path),
                "build_log_path": str(log_path),
            },
            skip_relaunch=True,
            poll_interval=1.0,
            build_timeout=2.0,
        )

    assert exc.value.code == 1
    captured = capsys.readouterr()
    result = json.loads(captured.out)
    assert result["status"] == "build_timeout"
    assert result["last_stage"] == "building"
    assert "UnrealBuildTool is still running" in result["build_output_tail"]
    assert "last stage: building" in captured.err
    assert str(status_path) in captured.err
    assert str(log_path) in captured.err


def test_wait_for_build_and_relaunch_reads_success_status_before_timeout(tmp_path, capsys, monkeypatch):
    status_path = tmp_path / "BuildAndRelaunch.status.json"
    log_path = tmp_path / "BuildAndRelaunch.log"
    log_path.write_text("build ok\n", encoding="utf-8")
    clock = {"now": 0.0}

    def fake_sleep(seconds):
        clock["now"] += seconds
        status_path.write_text(
            json.dumps(
                {
                    "complete": True,
                    "success": True,
                    "stage": "completed",
                    "exit_code": 0,
                    "message": "Build completed successfully.",
                }
            ),
            encoding="utf-8",
        )

    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])

    _wait_for_build_and_relaunch(
        {
            "project": "MyGame",
            "build_status_path": str(status_path),
            "build_log_path": str(log_path),
        },
        skip_relaunch=True,
        poll_interval=1.0,
        build_timeout=1.0,
    )

    result = json.loads(capsys.readouterr().out)
    assert result["success"] is True
    assert result["status"] == "build_succeeded"


def test_wait_for_build_and_relaunch_relaunch_timeout_reports_modal(tmp_path, capsys, monkeypatch):
    status_path = tmp_path / "BuildAndRelaunch.status.json"
    status_path.write_text(
        json.dumps(
            {
                "complete": True,
                "success": True,
                "stage": "completed",
                "exit_code": 0,
                "message": "Build completed and editor relaunch was requested.",
            }
        ),
        encoding="utf-8",
    )
    clock = {"now": 0.0}

    def fake_sleep(seconds):
        clock["now"] += seconds

    monkeypatch.setattr("time.sleep", fake_sleep)
    monkeypatch.setattr("time.monotonic", lambda: clock["now"])
    monkeypatch.setattr(main_mod, "health_check", lambda **_kwargs: {"error": "timed out"})
    monkeypatch.setattr(
        main_mod,
        "_detect_blocking_editor_modal",
        lambda: {
            "kind": "editor_startup_modal",
            "title": "Restore Packages",
            "process_id": 42,
            "recovery_hint": "Close the editor, clear the package restore marker, then relaunch.",
        },
    )

    _wait_for_build_and_relaunch(
        {
            "project": "MyGame",
            "build_status_path": str(status_path),
        },
        skip_relaunch=False,
        startup_recovery=None,
        poll_interval=1.0,
        build_timeout=1.0,
        relaunch_timeout=2.0,
    )

    result = json.loads(capsys.readouterr().out)
    assert result["status"] == "build_succeeded_relaunch_blocked"
    assert result["diagnostics"]["modal"]["title"] == "Restore Packages"


def test_cmd_trigger_live_coding_forwards_scope_args():
    parser = build_parser()
    args = parser.parse_args([
        "trigger-live-coding",
        "--module",
        "SoftUEBridgeEditor",
        "--plugin",
        "SoftUEBridge",
        "--allow-header-changes",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_trigger_live_coding(args)

    mock_run.assert_called_once_with(
        "trigger-live-coding",
        {
            "wait_for_completion": True,
            "allow_header_changes": True,
            "module": "SoftUEBridgeEditor",
            "plugin": "SoftUEBridge",
        },
    )


def test_cmd_reload_bridge_module_forwards_module():
    parser = build_parser()
    args = parser.parse_args(["reload-bridge-module", "--module", "SoftUEBridgeEditor"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_reload_bridge_module(args)

    mock_run.assert_called_once_with(
        "reload-bridge-module",
        {"module": "SoftUEBridgeEditor"},
    )


def test_parser_exec_console_command():
    parser = build_parser()
    args = parser.parse_args(["exec-console-command", "--world", "editor", "stat", "fps"])
    assert args.world == "editor"
    assert args.command_parts == ["stat", "fps"]


def test_cmd_exec_console_command_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["exec-console-command", "--world", "editor", "--player-index", "1", "stat", "fps"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_exec_console_command(args)

    mock_run.assert_called_once_with(
        "exec-console-command",
        {"command": "stat fps", "world": "editor", "player_index": 1},
    )


def test_cmd_exec_console_command_auto_starts_pie():
    parser = build_parser()
    args = parser.parse_args(["exec-console-command", "--auto-start-pie", "stat", "fps"])

    with patch("soft_ue_cli.__main__._ensure_pie_running") as mock_ensure, patch(
        "soft_ue_cli.__main__._run_tool", return_value={"success": True}
    ):
        cmd_exec_console_command(args)

    mock_ensure.assert_called_once()


def test_parser_validate_class_path():
    parser = build_parser()
    args = parser.parse_args(["validate-class-path", "/Game/BP_Hero.BP_Hero_C", "--parent-depth", "5"])
    assert args.class_path == "/Game/BP_Hero.BP_Hero_C"
    assert args.parent_depth == 5


def test_cmd_validate_class_path_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["validate-class-path", "/Game/BP_Hero"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_validate_class_path(args)

    mock_run.assert_called_once_with("validate-class-path", {"class_path": "/Game/BP_Hero"})


def test_parser_inspect_pawn_possession():
    parser = build_parser()
    args = parser.parse_args(["inspect-pawn-possession", "--class-filter", "Character", "--actor-name", "Hero"])
    assert args.class_filter == "Character"
    assert args.actor_name == "Hero"
    assert args.world == "pie"


def test_cmd_inspect_pawn_possession_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["inspect-pawn-possession", "--world", "editor", "--class-filter", "Character"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_inspect_pawn_possession(args)

    mock_run.assert_called_once_with("inspect-pawn-possession", {"world": "editor", "class_filter": "Character"})


def test_parser_release_asset_lock():
    parser = build_parser()
    args = parser.parse_args(["asset", "release-lock", "/Game/Blueprints/BP_Player"])
    assert args.asset_path == "/Game/Blueprints/BP_Player"


def test_cmd_release_asset_lock_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["asset", "release-lock", "/Game/Blueprints/BP_Player"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_release_asset_lock(args)

    mock_run.assert_called_once_with("release-asset-lock", {"asset_path": "/Game/Blueprints/BP_Player"})


def test_parser_query_asset_pattern_alias():
    parser = build_parser()
    args = parser.parse_args(["asset", "query", "--pattern", "CO_PC_Test", "--class", "CustomizableObject"])
    assert args.query == "CO_PC_Test"
    assert args.asset_class == "CustomizableObject"


def test_cmd_query_asset_pattern_forwards_query():
    parser = build_parser()
    args = parser.parse_args(["asset", "query", "--pattern", "CO_PC_Test", "--class", "CustomizableObject"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_query_asset(args)

    mock_run.assert_called_once_with(
        "query-asset",
        {"query": "CO_PC_Test", "class": "CustomizableObject"},
    )


def test_parser_inspect_customizable_object_graph():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "graph", "/Game/Characters/CO_Hero.CO_Hero", "--include-node-properties"]
    )
    assert args.asset_path == "/Game/Characters/CO_Hero.CO_Hero"
    assert args.include_node_properties is True


def test_cmd_inspect_customizable_object_graph_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "graph", "/Game/Characters/CO_Hero.CO_Hero", "--include-node-properties"]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_inspect_customizable_object_graph(args)

    mock_run.assert_called_once_with(
        "inspect-customizable-object-graph",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero", "include_node_properties": True},
    )


def test_parser_inspect_mutable_parameters():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "parameters", "/Game/Characters/CO_Hero.CO_Hero"])
    assert args.asset_path == "/Game/Characters/CO_Hero.CO_Hero"


def test_cmd_inspect_mutable_parameters_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "parameters", "/Game/Characters/CO_Hero.CO_Hero"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_inspect_mutable_parameters(args)

    mock_run.assert_called_once_with(
        "inspect-mutable-parameters",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero"},
    )


def test_parser_inspect_mutable_diagnostics():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "diagnostics", "/Game/Characters/CO_Hero.CO_Hero"])
    assert args.asset_path == "/Game/Characters/CO_Hero.CO_Hero"


def test_cmd_inspect_mutable_diagnostics_forwards_args():
    parser = build_parser()
    args = parser.parse_args(["mutable", "inspect", "diagnostics", "/Game/Characters/CO_Hero.CO_Hero"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_inspect_mutable_diagnostics(args)

    mock_run.assert_called_once_with(
        "inspect-mutable-diagnostics",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero"},
    )


def test_cmd_add_co_node_forwards_generic_node_args():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "add-node",
            "/Game/Characters/CO_Hero.CO_Hero",
            "CustomizableObjectNodeFloatParameter",
            "--graph-name",
            "Source",
            "--position",
            "100,200",
            "--properties",
            '{"ParameterName":"Height"}',
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_co_node(args)

    mock_run.assert_called_once_with(
        "add-customizable-object-node",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node_class": "CustomizableObjectNodeFloatParameter",
            "graph_name": "Source",
            "position": [100, 200],
            "properties": {"ParameterName": "Height"},
        },
    )


def test_cmd_add_co_parameter_defaults_node_class_and_parameter_name():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "add-parameter",
            "/Game/Characters/CO_Hero.CO_Hero",
            "BodyHeight",
            "--parameter-type",
            "float",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_co_parameter(args)

    mock_run.assert_called_once_with(
        "add-customizable-object-node",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node_class": "CustomizableObjectNodeFloatParameter",
            "properties": {"ParameterName": "BodyHeight"},
        },
    )


def test_cmd_add_co_mesh_option_forwards_mesh_property():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "add-mesh-option",
            "/Game/Characters/CO_Hero.CO_Hero",
            "/Game/Meshes/SKM_Boots.SKM_Boots",
            "--position",
            "320,120",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_co_mesh_option(args)

    mock_run.assert_called_once_with(
        "add-customizable-object-node",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node_class": "CustomizableObjectNodeSkeletalMesh",
            "position": [320, 120],
            "properties": {"SkeletalMesh": "/Game/Meshes/SKM_Boots.SKM_Boots"},
        },
    )


def test_cmd_set_co_base_mesh_forwards_node_property():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "set-base-mesh",
            "/Game/Characters/CO_Hero.CO_Hero",
            "node-guid-1",
            "/Game/Meshes/SKM_Base.SKM_Base",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_co_base_mesh(args)

    mock_run.assert_called_once_with(
        "set-customizable-object-node-property",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node": "node-guid-1",
            "properties": {"SkeletalMesh": "/Game/Meshes/SKM_Base.SKM_Base"},
        },
    )


def test_cmd_set_co_layout_blocks_forwards_layout_payload():
    parser = build_parser()
    args = parser.parse_args([
        "mutable",
        "graph",
        "set-layout-blocks",
        "/Game/Characters/CO_Hero.CO_Hero",
        "remove-blocks-node",
        "--grid-size",
        "4,4",
        "--max-grid-size",
        "8,8",
        "--packing-strategy",
        "Fixed",
        "--parent-layout-index",
        "1",
        "--parent-material-node",
        "material-node",
        "--blocks",
        '[{"min":[0,0],"size":[1,1]},{"min":[2,2],"max":[4,4],"priority":3}]',
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_co_layout_blocks(args)

    mock_run.assert_called_once_with(
        "set-customizable-object-layout-blocks",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node": "remove-blocks-node",
            "grid_size": [4, 4],
            "max_grid_size": [8, 8],
            "packing_strategy": "Fixed",
            "parent_layout_index": 1,
            "parent_material_node": "material-node",
            "blocks": [
                {"min": [0, 0], "size": [1, 1]},
                {"min": [2, 2], "max": [4, 4], "priority": 3},
            ],
        },
    )


def test_cmd_set_co_layout_blocks_forwards_source_uv_layout_target():
    parser = build_parser()
    args = parser.parse_args([
        "mutable",
        "graph",
        "set-layout-blocks",
        "/Game/Characters/CO_Hero.CO_Hero",
        "skeletal-mesh-node",
        "--grid-size",
        "4,4",
        "--blocks",
        '[{"min":[0,0],"size":[1,1]}]',
        "--lod-index",
        "1",
        "--section-index",
        "2",
        "--uv-channel",
        "3",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_co_layout_blocks(args)

    mock_run.assert_called_once_with(
        "set-customizable-object-layout-blocks",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node": "skeletal-mesh-node",
            "grid_size": [4, 4],
            "blocks": [{"min": [0, 0], "size": [1, 1]}],
            "lod_index": 1,
            "section_index": 2,
            "uv_channel": 3,
        },
    )


def test_cmd_add_co_group_child_forwards_pin_connection():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "add-group-child",
            "/Game/Characters/CO_Hero.CO_Hero",
            "group-node",
            "child-node",
            "--group-pin",
            "Objects",
            "--child-pin",
            "Object",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_co_group_child(args)

    mock_run.assert_called_once_with(
        "connect-customizable-object-pins",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "source_node": "child-node",
            "source_pin": "Object",
            "target_node": "group-node",
            "target_pin": "Objects",
        },
    )


def test_cmd_set_co_node_property_forwards_json_properties():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "set-node-property",
            "/Game/Characters/CO_Hero.CO_Hero",
            "node-guid-1",
            "--properties",
            '{"ParameterName":"Hat"}',
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_co_node_property(args)

    mock_run.assert_called_once_with(
        "set-customizable-object-node-property",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node": "node-guid-1",
            "properties": {"ParameterName": "Hat"},
        },
    )


def test_cmd_connect_co_pins_forwards_connection():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "connect-pins",
            "/Game/Characters/CO_Hero.CO_Hero",
            "source-node",
            "Value",
            "target-node",
            "Input",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_connect_co_pins(args)

    mock_run.assert_called_once_with(
        "connect-customizable-object-pins",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "source_node": "source-node",
            "source_pin": "Value",
            "target_node": "target-node",
            "target_pin": "Input",
            "auto_regenerate": True,
        },
    )


def test_cmd_connect_co_pins_can_disable_auto_regenerate():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "connect-pins",
            "/Game/Characters/CO_Hero.CO_Hero",
            "source-node",
            "Value",
            "target-node",
            "Input",
            "--no-auto-regenerate",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_connect_co_pins(args)

    mock_run.assert_called_once_with(
        "connect-customizable-object-pins",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "source_node": "source-node",
            "source_pin": "Value",
            "target_node": "target-node",
            "target_pin": "Input",
            "auto_regenerate": False,
        },
    )


def test_cmd_regenerate_co_node_pins_forwards_node_reference():
    from soft_ue_cli import __main__ as main_mod

    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "regenerate-node-pins", "/Game/Characters/CO_Hero.CO_Hero", "node-guid-1"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        main_mod.cmd_regenerate_co_node_pins(args)

    mock_run.assert_called_once_with(
        "regenerate-customizable-object-node-pins",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero", "node": "node-guid-1"},
    )


def test_cmd_compile_co_forwards_asset_path():
    parser = build_parser()
    args = parser.parse_args(["mutable", "compile", "/Game/Characters/CO_Hero.CO_Hero"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_compile_co(args)

    mock_run.assert_called_once_with(
        "compile-customizable-object",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero"},
    )


def test_cmd_compile_co_gather_references_forwards_flag():
    parser = build_parser()
    args = parser.parse_args(["mutable", "compile", "/Game/Characters/CO_Hero.CO_Hero", "--gather-references"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_compile_co(args)

    mock_run.assert_called_once_with(
        "compile-customizable-object",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero", "gather_references": True},
    )


def test_cmd_create_co_from_spec_forwards_json_spec():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "create-from-spec",
            "/Game/Characters/CO_Hero.CO_Hero",
            "--spec",
            '{"nodes":[{"id":"mesh","class":"CustomizableObjectNodeSkeletalMesh"}],"edges":[]}',
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_create_co_from_spec(args)

    mock_run.assert_called_once_with(
        "create-customizable-object-from-spec",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "spec": {
                "nodes": [{"id": "mesh", "class": "CustomizableObjectNodeSkeletalMesh"}],
                "edges": [],
            },
        },
    )


def test_cmd_set_node_position_forwards_positions_for_customizable_object_paths():
    parser = build_parser()
    args = parser.parse_args(["blueprint", "node", "position",
            "/Game/Characters/CO_Hero.CO_Hero",
            "--positions",
            '[{"guid":"11111111-2222-3333-4444-555555555555","x":120,"y":240}]',
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_node_position(args)

    mock_run.assert_called_once_with(
        "set-node-position",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "positions": [{"guid": "11111111-2222-3333-4444-555555555555", "x": 120, "y": 240}],
        },
    )


def test_cmd_set_node_position_accepts_mcp_native_positions_array():
    args = argparse.Namespace(
        asset_path="/Game/BP_Player",
        positions=[{"guid": "abc", "x": 500, "y": 100}],
        graph_name="EventGraph",
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_set_node_position(args)

    mock_run.assert_called_once_with(
        "set-node-position",
        {
            "asset_path": "/Game/BP_Player",
            "positions": [{"guid": "abc", "x": 500, "y": 100}],
            "graph_name": "EventGraph",
        },
    )


def test_cmd_remove_co_node_forwards_node_reference():
    from soft_ue_cli import __main__ as main_mod

    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "remove-node", "/Game/Characters/CO_Hero.CO_Hero", "node-guid-1"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        main_mod.cmd_remove_co_node(args)

    mock_run.assert_called_once_with(
        "remove-customizable-object-node",
        {"asset_path": "/Game/Characters/CO_Hero.CO_Hero", "node": "node-guid-1"},
    )


def test_cmd_wire_co_slot_from_table_forwards_macro_args():
    parser = build_parser()
    args = parser.parse_args(["mutable", "graph", "wire-slot-from-table",
            "/Game/Characters/CO_Hero.CO_Hero",
            "Boots",
            "/Game/Data/DT_Equipment.DT_Equipment",
            "Slot",
            "/Game/Materials/M_Boots.M_Boots",
            "ComponentMesh_0",
            "--filter-value",
            "Light",
            "--filter-value",
            "Heavy",
            "--filter-operation",
            "AND",
            "--lod-index",
            "1",
            "--material-index",
            "2",
            "--node-position",
            "100,200",
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_wire_co_slot_from_table(args)

    mock_run.assert_called_once_with(
        "wire-customizable-object-slot-from-table",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "parameter_name": "Boots",
            "data_table_path": "/Game/Data/DT_Equipment.DT_Equipment",
            "filter_column": "Slot",
            "filter_values": ["Light", "Heavy"],
            "filter_operation": "AND",
            "material_asset": "/Game/Materials/M_Boots.M_Boots",
            "component_mesh_node": "ComponentMesh_0",
            "lod_index": 1,
            "material_index": 2,
            "node_position": [100, 200],
        },
    )


def test_cmd_add_datatable_row_forwards_row_data_as_object():
    parser = build_parser()
    args = parser.parse_args(
        [
            "add-datatable-row",
            "/Game/Data/DT_Items.DT_Items",
            "Boots",
            "--row-data",
            '{"ItemId": 2, "SlotTag": {"TagName": "Equipment.Feet"}, "MutableEnumOption": "Boots", "DisplayName": "Boots"}',
        ]
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_datatable_row(args)

    mock_run.assert_called_once_with(
        "add-datatable-row",
        {
            "asset_path": "/Game/Data/DT_Items.DT_Items",
            "row_name": "Boots",
            "row_data": {
                "ItemId": 2,
                "SlotTag": {"TagName": "Equipment.Feet"},
                "MutableEnumOption": "Boots",
                "DisplayName": "Boots",
            },
        },
    )


# -- capture-screenshot parser -------------------------------------------------


def test_parser_capture_screenshot_window():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "window"])
    assert args.mode == "window"
    assert args.func == cmd_capture_screenshot


def test_parser_capture_screenshot_tab():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "tab", "--window-name", "Blueprint"])
    assert args.mode == "tab"
    assert args.window_name == "Blueprint"


def test_parser_capture_screenshot_region():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "region", "--region", "0,0,800,600"])
    assert args.mode == "region"
    assert args.region == "0,0,800,600"


def test_parser_capture_screenshot_viewport():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "viewport"])
    assert args.mode == "viewport"


def test_parser_capture_screenshot_pie_window():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "pie-window"])
    assert args.mode == "pie-window"


def test_parser_capture_screenshot_format_and_output():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "window", "--format", "png", "--output", "file"])
    assert args.format == "png"
    assert args.output == "file"


def test_cmd_capture_screenshot_copies_to_requested_output_file(tmp_path, capsys):
    source = tmp_path / "bridge-shot.png"
    output = tmp_path / "requested-shot.png"
    source.write_bytes(b"png-bytes")
    parser = build_parser()
    args = parser.parse_args([
        "capture",
        "screenshot",
        "--source",
        "pie-window",
        "--output-file",
        str(output),
    ])

    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": str(source), "mode": "file"}) as mock_call:
        cmd_capture_screenshot(args)

    mock_call.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "pie-window",
            "output": "file",
        },
    )
    payload = json.loads(capsys.readouterr().out)
    assert output.read_bytes() == b"png-bytes"
    assert payload["file_path"] == str(output)
    assert payload["bridge_file_path"] == str(source)


def test_parser_capture_screenshot_invalid_mode():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["capture", "screenshot", "--source", "invalid"])


def test_cmd_capture_screenshot_window_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "window"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with("capture-screenshot", {"mode": "window"})


def test_cmd_capture_screenshot_tab_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "tab", "--window-name", "OutputLog"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "tab", "window_name": "OutputLog"}
    )


def test_cmd_capture_screenshot_region_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "region", "--region", "10,20,800,600"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "region", "region": [10, 20, 800, 600]}
    )


def test_cmd_capture_screenshot_viewport_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "viewport", "--format", "png"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "viewport", "format": "png"}
    )


def test_cmd_capture_screenshot_window_can_opt_out_of_safe_mode():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "window", "--unsafe-slate-window-capture"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "window", "safe_mode": False}
    )


def test_cmd_capture_pie_screenshot_calls_safe_composited_mode():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "pie-window",
        "--format",
        "jpeg",
        "--output",
        "base64",
        "--scale",
        "70",
        "--cleanup-previous",
    ])
    assert args.func == cmd_capture_screenshot
    with patch("soft_ue_cli.__main__.call_tool", return_value={"image_base64": "..."}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "pie-window",
            "format": "jpeg",
            "output": "base64",
            "scale": 70.0,
            "cleanup_previous": True,
        },
    )


def test_removed_capture_pie_screenshot_can_opt_into_unsafe_slate_capture():
    parser = build_parser(include_removed=True)
    args = parser.parse_args(["capture-pie-screenshot", "--unsafe-slate-window-capture"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_run:
        cmd_capture_pie_screenshot(args)

    mock_run.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "pie-window",
            "safe_mode": False,
        },
    )


def test_removed_capture_pie_screenshot_copies_to_requested_output_file(tmp_path, capsys):
    source = tmp_path / "bridge-pie-shot.png"
    output = tmp_path / "requested-pie-shot.png"
    source.write_bytes(b"png-bytes")
    parser = build_parser(include_removed=True)
    args = parser.parse_args(["capture-pie-screenshot", "--output-file", str(output)])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"file_path": str(source), "mode": "file"}) as mock_run:
        cmd_capture_pie_screenshot(args)

    mock_run.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "pie-window",
            "output": "file",
        },
    )
    payload = json.loads(capsys.readouterr().out)
    assert output.read_bytes() == b"png-bytes"
    assert payload["file_path"] == str(output)
    assert payload["bridge_file_path"] == str(source)


def test_cmd_capture_screenshot_all_options():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "window",
        "--format",
        "jpeg",
        "--output",
        "base64",
        "--scale",
        "50",
        "--color-mode",
        "grayscale",
    ])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"image_base64": "..."}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "window",
            "format": "jpeg",
            "output": "base64",
            "scale": 50.0,
            "color_mode": "grayscale",
        },
    )


def test_cmd_capture_screenshot_invalid_region_exits():
    parser = build_parser()
    args = parser.parse_args(["capture", "screenshot", "--source", "region", "--region", "a,b,c,d"])
    with pytest.raises(SystemExit) as exc:
        cmd_capture_screenshot(args)
    assert exc.value.code == 1


def test_cmd_compare_umg_screenshot_outputs_structured_result(tmp_path, capsys):
    from PIL import Image

    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    annotated = tmp_path / "annotated.png"
    Image.new("RGB", (8, 8), (20, 40, 80)).save(reference)
    Image.new("RGB", (8, 8), (24, 42, 78)).save(captured)

    parser = build_parser()
    args = parser.parse_args(["umg", "layout", "compare", "--mode", "pixel",
        str(reference),
        str(captured),
        "--annotated-output",
        str(annotated),
    ])
    assert args.func == main_mod.cmd_umg_layout

    main_mod.cmd_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is True
    assert 0.95 <= data["similarity_score"] <= 1.0
    assert data["layout_regions"]
    assert "element_presence_delta" in data
    assert data["annotated_diff_path"] == str(annotated)
    assert annotated.exists()


def test_cmd_compare_umg_screenshot_accepts_mcp_array_crop(tmp_path, capsys):
    from PIL import Image

    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    Image.new("RGB", (4, 4), (10, 20, 30)).save(reference)
    Image.new("RGB", (8, 8), (10, 20, 30)).save(captured)
    args = argparse.Namespace(
        reference_image=str(reference),
        captured_image=str(captured),
        crop=[0, 0, 4, 4],
        annotated_output=None,
        threshold=0.9,
    )

    cmd_compare_umg_screenshot(args)

    data = json.loads(capsys.readouterr().out)
    assert data["captured"]["crop"] == [0, 0, 4, 4]
    assert data["similarity_score"] == 1.0


def test_cmd_compare_umg_layout_outputs_structured_deltas(tmp_path, capsys):
    expected = tmp_path / "expected.json"
    actual = tmp_path / "actual.json"
    expected.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    actual.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0.1, 0, 1, 1]}]}), encoding="utf-8")

    parser = build_parser()
    args = parser.parse_args(["umg", "layout", "compare", "--mode", "geometry",
        str(expected),
        str(actual),
        "--bounds-tolerance",
        "0.01",
    ])

    assert hasattr(main_mod, "cmd_compare_umg_layout")
    main_mod.cmd_compare_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is False
    assert data["deltas"][0]["kind"] == "bounds"


def test_cmd_extract_umg_layout_forwards_designer_request():
    parser = build_parser()
    args = parser.parse_args(["umg", "layout", "extract", "--source",
        "designer",
        "--asset-path",
        "/Game/UI/WBP_Menu",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"widgets": []}) as mock_run:
        assert hasattr(main_mod, "cmd_extract_umg_layout")
        main_mod.cmd_extract_umg_layout(args)

    mock_run.assert_called_once_with(
        "inspect-widget-blueprint",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "include_defaults": True,
            "depth_limit": 12,
        },
    )


def test_cmd_umg_layout_extract_designer_wraps_existing_extraction():
    parser = build_parser()
    args = parser.parse_args(["umg", "layout",
        "extract",
        "--source",
        "designer",
        "--asset-path",
        "/Game/UI/WBP_Menu",
    ])

    assert args.func == main_mod.cmd_umg_layout
    with patch("soft_ue_cli.__main__._run_tool", return_value={"widgets": []}) as mock_run:
        main_mod.cmd_umg_layout(args)

    mock_run.assert_called_once_with(
        "inspect-widget-blueprint",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "include_defaults": True,
            "depth_limit": 12,
        },
    )


def test_cmd_umg_layout_compare_geometry_supports_subset(tmp_path, capsys):
    expected = tmp_path / "expected.json"
    actual = tmp_path / "actual.json"
    expected.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    actual.write_text(
        json.dumps(
            {
                "widgets": [
                    {"name": "A", "normalized_bounds": [0, 0, 1, 1]},
                    {"name": "Decor", "normalized_bounds": [0.5, 0.5, 0.1, 0.1]},
                ]
            }
        ),
        encoding="utf-8",
    )

    args = build_parser().parse_args(["umg", "layout",
        "compare",
        "--mode",
        "geometry",
        "--subset",
        str(expected),
        str(actual),
    ])
    main_mod.cmd_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is True
    assert data["summary"]["ignored_extra_widgets"] == 1


def test_cmd_umg_layout_fit_writes_corrected_spec(tmp_path, capsys):
    concept = tmp_path / "concept.json"
    actual = tmp_path / "actual.json"
    spec = tmp_path / "spec.json"
    output = tmp_path / "corrected.json"
    concept.write_text(
        json.dumps({"canvas_size": [100, 100], "widgets": [{"name": "A", "bounds": [20, 10, 30, 10]}]}),
        encoding="utf-8",
    )
    actual.write_text(
        json.dumps({"canvas_size": [100, 100], "widgets": [{"name": "A", "bounds": [10, 10, 30, 10]}]}),
        encoding="utf-8",
    )
    spec.write_text(
        json.dumps({"root": {"class": "CanvasPanel", "name": "Root", "children": [{"class": "TextBlock", "name": "A", "slot": {"position": [10, 10], "size": [30, 10]}}]}}),
        encoding="utf-8",
    )

    args = build_parser().parse_args(["umg", "layout",
        "fit",
        "--concept",
        str(concept),
        "--actual",
        str(actual),
        "--spec",
        str(spec),
        "--output",
        str(output),
    ])
    main_mod.cmd_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is True
    assert data["corrections"][0]["widget"] == "A"
    assert json.loads(output.read_text(encoding="utf-8"))["root"]["children"][0]["slot"]["position"] == [20, 10]


def test_cmd_umg_layout_compare_both_combines_geometry_and_pixel(tmp_path, capsys):
    from PIL import Image

    expected = tmp_path / "expected.json"
    actual = tmp_path / "actual.json"
    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    expected.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    actual.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    Image.new("RGB", (4, 4), (10, 20, 30)).save(reference)
    Image.new("RGB", (4, 4), (10, 20, 30)).save(captured)

    args = build_parser().parse_args(["umg", "layout",
        "compare",
        "--mode",
        "both",
        str(expected),
        str(actual),
        "--reference-image",
        str(reference),
        "--captured-image",
        str(captured),
    ])
    main_mod.cmd_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is True
    assert data["geometry"]["success"] is True
    assert data["pixel"]["success"] is True


# -- capture-viewport parser & cmd ---------------------------------------------


def test_parser_capture_viewport_defaults():
    parser = build_parser()
    args = parser.parse_args(["capture", "viewport"])
    assert args.func == cmd_capture_viewport
    assert args.format is None
    assert args.output is None


def test_parser_capture_viewport_with_options():
    parser = build_parser()
    args = parser.parse_args(["capture", "viewport",
        "--format",
        "jpeg",
        "--output",
        "base64",
        "--scale",
        "50",
        "--width",
        "640",
        "--height",
        "360",
        "--color-mode",
        "monochrome",
        "--cleanup-previous",
    ])
    assert args.format == "jpeg"
    assert args.output == "base64"
    assert args.scale == 50.0
    assert args.width == 640
    assert args.height == 360
    assert args.color_mode == "monochrome"
    assert args.cleanup_previous is True


def test_cmd_capture_viewport_default():
    parser = build_parser()
    args = parser.parse_args(["capture", "viewport"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/vp.png"}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with("capture-viewport", {})


def test_cmd_capture_viewport_with_format():
    parser = build_parser()
    args = parser.parse_args(["capture", "viewport", "--format", "png"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/vp.png"}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with("capture-viewport", {"format": "png"})


def test_cmd_capture_viewport_all_options():
    parser = build_parser()
    args = parser.parse_args(["capture", "viewport",
        "--format",
        "jpeg",
        "--output",
        "base64",
        "--scale",
        "50",
        "--width",
        "640",
        "--height",
        "360",
        "--color-mode",
        "grayscale",
        "--cleanup-previous",
    ])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"image_base64": "..."}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with(
        "capture-viewport",
        {
            "format": "jpeg",
            "output": "base64",
            "scale": 50.0,
            "width": 640,
            "height": 360,
            "color_mode": "grayscale",
            "cleanup_previous": True,
        },
    )


def test_capture_viewport_family_routes_to_existing_tool():
    parser = build_parser()
    args = parser.parse_args([
        "capture",
        "viewport",
        "--source",
        "editor",
        "--format",
        "jpeg",
        "--scale",
        "50",
        "--color-mode",
        "grayscale",
    ])

    assert args.func == cmd_capture_viewport
    with patch("soft_ue_cli.__main__._run_tool", return_value={"file_path": "/tmp/vp.jpg"}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "capture-viewport",
        {
            "source": "editor",
            "format": "jpeg",
            "scale": 50.0,
            "color_mode": "grayscale",
        },
    )


def test_capture_screenshot_family_routes_to_existing_tool():
    parser = build_parser()
    args = parser.parse_args([
        "capture",
        "screenshot",
        "--source",
        "pie-window",
        "--output",
        "base64",
        "--width",
        "640",
    ])

    assert args.func == cmd_capture_screenshot
    with patch("soft_ue_cli.__main__._run_tool", return_value={"image_base64": "..."}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "pie-window",
            "output": "base64",
            "width": 640,
        },
    )


def test_capture_screenshot_family_region_uses_region_arg():
    parser = build_parser()
    args = parser.parse_args([
        "capture",
        "screenshot",
        "--source",
        "region",
        "--region",
        "10,20,800,600",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"file_path": "/tmp/region.png"}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "capture-screenshot",
        {
            "mode": "region",
            "region": [10, 20, 800, 600],
        },
    )


@pytest.mark.parametrize(
    ("argv", "expected_attrs"),
    [
        (["mutable", "inspect", "graph", "/Game/Characters/CO_Hero.CO_Hero"], {"command": "mutable", "mutable_action": "inspect", "mutable_inspect_action": "graph"}),
        (["mutable", "graph", "add-node", "/Game/Characters/CO_Hero.CO_Hero", "CustomizableObjectNodeFloatParameter"], {"command": "mutable", "mutable_action": "graph", "mutable_graph_action": "add-node"}),
        (["mutable", "compile", "/Game/Characters/CO_Hero.CO_Hero"], {"command": "mutable", "mutable_action": "compile"}),
        (["statetree", "inspect", "/Game/AI/ST_Enemy"], {"command": "statetree", "statetree_action": "inspect"}),
        (["anim", "rewind", "status"], {"command": "anim", "anim_action": "rewind", "anim_rewind_action": "status"}),
        (["automation", "tests", "run", "--filter", "Project."], {"command": "automation", "automation_action": "tests", "automation_tests_action": "run"}),
        (["asset", "query", "--asset-path", "/Game/Data/DT_Items"], {"command": "asset", "asset_action": "query"}),
        (["asset", "inspect-file", "C:/Project/Content/BP_Player.uasset"], {"command": "asset", "asset_action": "inspect-file"}),
        (["blueprint", "inspect", "/Game/Blueprints/BP_Player"], {"command": "blueprint", "blueprint_action": "inspect"}),
        (["blueprint", "graph", "inspect", "/Game/Blueprints/BP_Player"], {"command": "blueprint", "blueprint_action": "graph", "blueprint_graph_action": "inspect"}),
        (["blueprint", "node", "add", "/Game/Blueprints/BP_Player", "K2Node_CallFunction"], {"command": "blueprint", "blueprint_action": "node", "blueprint_node_action": "add"}),
    ],
)
def test_canonical_command_families_parse_as_canonical_commands(argv, expected_attrs):
    args = build_parser().parse_args(argv)

    for attr, expected in expected_attrs.items():
        assert getattr(args, attr) == expected


@pytest.mark.parametrize("argv", [
    ["query-blueprint", "/Game/Blueprints/BP_Player"],
    ["query-blueprint-graph", "/Game/Blueprints/BP_Player"],
    ["inspect-customizable-object-graph", "/Game/Characters/CO_Hero.CO_Hero"],
    ["capture-viewport"],
    ["run-automation-tests", "--filter", "Project."],
    ["umg-layout", "extract", "--source", "concept-image", "--input", "concept.png"],
])
def test_removed_flat_commands_are_no_longer_supported(argv):
    with pytest.raises(SystemExit):
        build_parser().parse_args(argv)


def test_canonical_command_family_normalization_preserves_root_options():
    args = build_parser().parse_args([
        "--server",
        "http://127.0.0.1:8080",
        "--timeout=45",
        "blueprint",
        "graph",
        "inspect",
        "/Game/Blueprints/BP_Player",
    ])

    assert args.server == "http://127.0.0.1:8080"
    assert args.timeout == 45
    assert args.command == "blueprint"
    assert args.blueprint_action == "graph"
    assert args.blueprint_graph_action == "inspect"


def test_mutable_graph_add_node_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "mutable",
        "graph",
        "add-node",
        "/Game/Characters/CO_Hero.CO_Hero",
        "CustomizableObjectNodeFloatParameter",
        "--properties",
        '{"ParameterName":"Height"}',
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "add-customizable-object-node",
        {
            "asset_path": "/Game/Characters/CO_Hero.CO_Hero",
            "node_class": "CustomizableObjectNodeFloatParameter",
            "properties": {"ParameterName": "Height"},
        },
    )


def test_statetree_state_add_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "statetree",
        "state",
        "add",
        "/Game/AI/ST_Enemy",
        "Patrol",
        "--parent-state",
        "Combat",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "add-statetree-state",
        {
            "asset_path": "/Game/AI/ST_Enemy",
            "state_name": "Patrol",
            "parent_state": "Combat",
        },
    )


def test_anim_rewind_snapshot_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "anim",
        "rewind",
        "snapshot",
        "--actor-tag",
        "Player",
        "--time",
        "1.25",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "rewind-snapshot",
        {
            "actor_tag": "Player",
            "time": 1.25,
        },
    )


def test_anim_retarget_repoint_references_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "retarget",
        "repoint-references",
        "/Game/Anim/AM_Attack",
        "/Game/Anim/BS_Locomotion",
        "--map",
        "/Game/Anim/AS_Attack=/Game/Anim/RTG/AS_Attack",
        "--map",
        "/Game/Anim/AS_Run:/Game/Anim/RTG/AS_Run",
        "--target-skeleton",
        "/Game/Characters/SKEL_Target",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-repoint-references",
        {
            "asset_paths": ["/Game/Anim/AM_Attack", "/Game/Anim/BS_Locomotion"],
            "replacement_map": {
                "/Game/Anim/AS_Attack": "/Game/Anim/RTG/AS_Attack",
                "/Game/Anim/AS_Run": "/Game/Anim/RTG/AS_Run",
            },
            "target_skeleton": "/Game/Characters/SKEL_Target",
            "checkout": True,
            "save": True,
        },
    )


def test_anim_retarget_blueprint_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "retarget",
        "blueprint",
        "/Game/Anim/ABP_Hero",
        "/Game/Anim/ABP_Hero_Target",
        "--target-skeleton",
        "/Game/Characters/SKEL_Target",
        "--bone-map",
        "upperarm_l=upperarm_l_target",
        "--bone-map",
        "spine_01:spine_a",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-retarget-blueprint",
        {
            "source_blueprint": "/Game/Anim/ABP_Hero",
            "target_blueprint": "/Game/Anim/ABP_Hero_Target",
            "target_skeleton": "/Game/Characters/SKEL_Target",
            "bone_map": {
                "upperarm_l": "upperarm_l_target",
                "spine_01": "spine_a",
            },
            "checkout": True,
            "save": True,
        },
    )


def test_anim_retarget_blueprint_routes_optional_anim_map_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "retarget",
        "blueprint",
        "/Game/Anim/ABP_Hero",
        "/Game/Anim/ABP_Hero_Target",
        "--target-skeleton",
        "/Game/Characters/SKEL_Target",
        "--bone-map",
        "upperarm_l=upperarm_l_target",
        "--anim-map",
        "/Game/Anim/AS_Idle=/Game/Anim/RTG/AS_Idle",
        "--anim-map",
        "/Game/Anim/BS_Run:/Game/Anim/RTG/BS_Run",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-retarget-blueprint",
        {
            "source_blueprint": "/Game/Anim/ABP_Hero",
            "target_blueprint": "/Game/Anim/ABP_Hero_Target",
            "target_skeleton": "/Game/Characters/SKEL_Target",
            "bone_map": {
                "upperarm_l": "upperarm_l_target",
            },
            "animation_asset_map": {
                "/Game/Anim/AS_Idle": "/Game/Anim/RTG/AS_Idle",
                "/Game/Anim/BS_Run": "/Game/Anim/RTG/BS_Run",
            },
            "save": True,
        },
    )


def test_anim_montage_set_slot_animation_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "montage",
        "set-slot-animation",
        "/Game/Anim/AM_Attack",
        "/Game/Anim/AS_Attack_RTG",
        "--slot-name",
        "UpperBody",
        "--section",
        "Attack",
        "--start-time",
        "0.25",
        "--play-rate",
        "1.2",
        "--looping-count",
        "2",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-montage-set-slot-animation",
        {
            "asset_path": "/Game/Anim/AM_Attack",
            "anim_path": "/Game/Anim/AS_Attack_RTG",
            "slot_name": "UpperBody",
            "section": "Attack",
            "start_time": 0.25,
            "play_rate": 1.2,
            "looping_count": 2,
            "checkout": True,
            "save": True,
        },
    )


def test_anim_montage_set_slot_animation_uses_default_slot_and_minimal_payload():
    args = build_parser().parse_args([
        "anim",
        "montage",
        "set-slot-animation",
        "/Game/Anim/AM_Attack",
        "/Game/Anim/AS_Attack_RTG",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-montage-set-slot-animation",
        {
            "asset_path": "/Game/Anim/AM_Attack",
            "anim_path": "/Game/Anim/AS_Attack_RTG",
        },
    )


def test_anim_montage_inspect_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "montage",
        "inspect",
        "/Game/Anim/AM_Attack",
        "--include",
        "notifies,sections,slots",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-montage-inspect",
        {
            "asset_path": "/Game/Anim/AM_Attack",
            "include": "notifies,sections,slots",
        },
    )


def test_anim_retarget_sequence_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "retarget",
        "sequence",
        "/Game/Anim/AS_Attack",
        "/Game/Anim/RTG/AS_Attack_RTG",
        "--source-mesh",
        "/Game/Characters/SKM_Source",
        "--target-mesh",
        "/Game/Characters/SKM_Target",
        "--ik-retargeter",
        "/Game/Characters/RTG_SourceToTarget",
        "--overwrite",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "anim-retarget-sequence",
        {
            "source_sequence": "/Game/Anim/AS_Attack",
            "target_sequence": "/Game/Anim/RTG/AS_Attack_RTG",
            "source_mesh": "/Game/Characters/SKM_Source",
            "target_mesh": "/Game/Characters/SKM_Target",
            "ik_retargeter": "/Game/Characters/RTG_SourceToTarget",
            "overwrite": True,
            "checkout": True,
            "save": True,
        },
    )


def test_anim_pose_search_inspect_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "pose-search",
        "inspect",
        "/Game/Motion/PS_Hero",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "pose-search-schema-inspect",
        {
            "schema_path": "/Game/Motion/PS_Hero",
        },
    )


def test_anim_pose_search_remap_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "pose-search",
        "remap",
        "/Game/Motion/PS_Hero",
        "--target-skeleton",
        "/Game/Characters/SKEL_Target",
        "--bone-map",
        "pelvis=pelvis_target",
        "--bone-map",
        "spine_01:spine_a",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "pose-search-schema-remap",
        {
            "schema_path": "/Game/Motion/PS_Hero",
            "target_skeleton": "/Game/Characters/SKEL_Target",
            "bone_map": {
                "pelvis": "pelvis_target",
                "spine_01": "spine_a",
            },
            "checkout": True,
            "save": True,
        },
    )


def test_anim_pose_search_database_repoint_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "anim",
        "pose-search",
        "database-repoint",
        "/Game/Motion/PSD_Hero",
        "--schema",
        "/Game/Motion/PS_Hero_Target",
        "--anim-map",
        "/Game/Anim/AS_Walk=/Game/Anim/RTG/AS_Walk",
        "--anim-map",
        "/Game/Anim/AS_Run:/Game/Anim/RTG/AS_Run",
        "--reindex",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "pose-search-database-repoint",
        {
            "database_path": "/Game/Motion/PSD_Hero",
            "schema_path": "/Game/Motion/PS_Hero_Target",
            "animation_asset_map": {
                "/Game/Anim/AS_Walk": "/Game/Anim/RTG/AS_Walk",
                "/Game/Anim/AS_Run": "/Game/Anim/RTG/AS_Run",
            },
            "reindex": True,
            "checkout": True,
            "save": True,
        },
    )


def test_asset_repoint_references_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "asset",
        "repoint-references",
        "/Game/Data/DA_SkillSet",
        "/Game/Data/DA_ComboSet",
        "--map",
        "/Game/Anim/AM_Attack=/Game/Anim/RTG/AM_Attack",
        "--map",
        "/Game/Anim/AM_Dodge:/Game/Anim/RTG/AM_Dodge",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "asset-repoint-references",
        {
            "asset_paths": ["/Game/Data/DA_SkillSet", "/Game/Data/DA_ComboSet"],
            "replacement_map": {
                "/Game/Anim/AM_Attack": "/Game/Anim/RTG/AM_Attack",
                "/Game/Anim/AM_Dodge": "/Game/Anim/RTG/AM_Dodge",
            },
            "checkout": True,
            "save": True,
        },
    )


def test_asset_skeletal_socket_create_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "asset",
        "skeletal-socket",
        "create",
        "/Game/Characters/SKM_Hero",
        "weapon_r",
        "hand_r",
        "--location",
        "1,2,3",
        "--rotation",
        "10,20,30",
        "--scale",
        "1,1,1",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "skeletal-mesh-socket-create",
        {
            "asset_path": "/Game/Characters/SKM_Hero",
            "socket_name": "weapon_r",
            "bone_name": "hand_r",
            "location": [1.0, 2.0, 3.0],
            "rotation": [10.0, 20.0, 30.0],
            "scale": [1.0, 1.0, 1.0],
            "checkout": True,
            "save": True,
        },
    )


def test_asset_skeletal_socket_remove_routes_to_bridge_tool():
    args = build_parser().parse_args([
        "asset",
        "skeletal-socket",
        "remove",
        "/Game/Characters/SKM_Hero",
        "weapon_r",
        "--checkout",
        "--save",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "skeletal-mesh-socket-remove",
        {
            "asset_path": "/Game/Characters/SKM_Hero",
            "socket_name": "weapon_r",
            "checkout": True,
            "save": True,
        },
    )


def test_automation_tests_run_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "automation",
        "tests",
        "run",
        "--filter",
        "Project.",
        "--timeout",
        "90",
        "--max-tests",
        "2",
        "--list-only",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "run-automation-tests",
        {
            "filter": "Project.",
            "flags": "all",
            "timeout": 90.0,
            "list_only": True,
            "max_tests": 2,
        },
        timeout=210.0,
    )


def test_asset_preview_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "asset",
        "preview",
        "/Game/Textures/T_Player",
        "--resolution",
        "512",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"file_path": "/tmp/preview.png"}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "get-asset-preview",
        {
            "asset_path": "/Game/Textures/T_Player",
            "resolution": 512,
        },
    )


def test_blueprint_graph_inspect_family_routes_to_existing_tool():
    args = build_parser().parse_args([
        "blueprint",
        "graph",
        "inspect",
        "/Game/Blueprints/BP_Player",
        "--graph-name",
        "EventGraph",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "query-blueprint-graph",
        {
            "asset_path": "/Game/Blueprints/BP_Player",
            "graph_name": "EventGraph",
        },
    )


def test_parser_trigger_input_target_accepts_negative_vector_with_space():
    parser = build_parser()
    args = parser.parse_args(["trigger-input", "move-to", "--target", "-2000,-4190,88"])
    assert args.target == "-2000,-4190,88"


def test_cmd_trigger_input_forwards_negative_target_vector():
    parser = build_parser()
    args = parser.parse_args(["trigger-input", "move-to", "--target", "-2000,-4190,88"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_trigger_input(args)

    mock_run.assert_called_once_with(
        "trigger-input",
        {"action": "move-to", "target": [-2000.0, -4190.0, 88.0]},
    )


# -- inspect-runtime-widgets ---------------------------------------------------


def test_parser_inspect_runtime_widgets_defaults():
    parser = build_parser(include_removed=True)
    args = parser.parse_args(["inspect-runtime-widgets"])
    assert args.func.__name__ == "cmd_inspect_runtime_widgets"
    assert args.filter is None
    assert args.class_filter is None
    assert args.depth_limit is None
    assert args.include_slate is False
    assert args.pie_index is None
    assert args.no_geometry is False
    assert args.no_properties is False
    assert args.root_widget is None


def test_parser_inspect_runtime_widgets_all_args():
    parser = build_parser(include_removed=True)
    args = parser.parse_args([
        "inspect-runtime-widgets",
        "--filter", "HealthBar",
        "--class-filter", "TextBlock",
        "--depth-limit", "3",
        "--include-slate",
        "--pie-index", "1",
        "--no-geometry",
        "--no-properties",
        "--root-widget", "WBP_HUD_C_0",
    ])
    assert args.filter == "HealthBar"
    assert args.class_filter == "TextBlock"
    assert args.depth_limit == 3
    assert args.include_slate is True
    assert args.pie_index == 1
    assert args.no_geometry is True
    assert args.no_properties is True
    assert args.root_widget == "WBP_HUD_C_0"


def test_umg_runtime_inspect_routes_to_runtime_widget_tool():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "runtime",
        "inspect",
        "--root-widget",
        "WBP_Menu_C_0",
        "--depth-limit",
        "2",
        "--include-slate",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"root_widgets": []}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "inspect-runtime-widgets",
        {
            "depth_limit": 2,
            "include_slate": True,
            "root_widget": "WBP_Menu_C_0",
        },
    )


# -- apply-widget-tree ---------------------------------------------------------


def test_parser_apply_widget_tree_json_spec():
    parser = build_parser()
    spec = '{"root":{"class":"CanvasPanel","name":"RootCanvas"}}'
    args = parser.parse_args(["umg", "designer", "apply",
        "/Game/UI/WBP_Menu",
        "--spec",
        spec,
        "--compile",
        "--save",
        "--checkout",
    ])
    assert args.func.__name__ == "cmd_apply_widget_tree"
    assert args.asset_path == "/Game/UI/WBP_Menu"
    assert args.spec == spec
    assert args.spec_file is None
    assert args.append is False
    assert args.compile is True
    assert args.save is True
    assert args.checkout is True


def test_cmd_apply_widget_tree_forwards_spec_file(tmp_path):
    spec_path = tmp_path / "widget_tree.json"
    spec_path.write_text(
        json.dumps({
            "root": {
                "class": "CanvasPanel",
                "name": "RootCanvas",
                "children": [
                    {
                        "class": "TextBlock",
                        "name": "TitleText",
                        "text": "Main Menu",
                        "slot": {"position": [32, 48], "size": [480, 72]},
                    }
                ],
            }
        }),
        encoding="utf-8",
    )
    parser = build_parser()
    args = parser.parse_args(["umg", "designer", "apply",
        "/Game/UI/WBP_Menu",
        "--spec-file",
        str(spec_path),
        "--append",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_apply_widget_tree(args)

    mock_run.assert_called_once_with(
        "apply-widget-tree",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "spec": {
                "root": {
                    "class": "CanvasPanel",
                    "name": "RootCanvas",
                    "children": [
                        {
                            "class": "TextBlock",
                            "name": "TitleText",
                            "text": "Main Menu",
                            "slot": {"position": [32, 48], "size": [480, 72]},
                        }
                    ],
                }
            },
            "replace": False,
        },
    )


def test_cmd_apply_widget_tree_requires_spec_or_file(capsys):
    parser = build_parser()
    args = parser.parse_args(["umg", "designer", "apply", "/Game/UI/WBP_Menu"])

    with pytest.raises(SystemExit) as exc:
        cmd_apply_widget_tree(args)

    assert exc.value.code == 1
    assert "either --spec or --spec-file is required" in capsys.readouterr().err


def test_cmd_wire_widget_navigation_forwards_bindings_file(tmp_path):
    bindings_path = tmp_path / "navigation.json"
    bindings_path.write_text(
        json.dumps([
            {
                "button": "StartButton",
                "mode": "switcher",
                "switcher": "ScreenSwitcher",
                "target_index": 1,
            }
        ]),
        encoding="utf-8",
    )
    parser = build_parser()
    args = parser.parse_args(["umg", "navigation", "wire",
        "/Game/UI/WBP_Menu",
        "--bindings-file",
        str(bindings_path),
        "--compile",
        "--save",
        "--checkout",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_wire_widget_navigation(args)

    mock_run.assert_called_once_with(
        "wire-widget-navigation",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "bindings": [
                {
                    "button": "StartButton",
                    "mode": "switcher",
                    "switcher": "ScreenSwitcher",
                    "target_index": 1,
                }
            ],
            "compile": True,
            "save": True,
            "checkout": True,
        },
    )


def test_cmd_wire_widget_navigation_forwards_allow_pie():
    parser = build_parser()
    args = parser.parse_args(["umg", "navigation", "wire",
        "/Game/UI/WBP_Menu",
        "--bindings",
        '[{"button":"StartButton"}]',
        "--allow-pie",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_wire_widget_navigation(args)

    mock_run.assert_called_once_with(
        "wire-widget-navigation",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "bindings": [{"button": "StartButton"}],
            "allow_pie": True,
        },
    )


def test_cmd_wire_widget_navigation_requires_bindings(capsys):
    parser = build_parser()
    args = parser.parse_args(["umg", "navigation", "wire", "/Game/UI/WBP_Menu"])

    with pytest.raises(SystemExit) as exc:
        cmd_wire_widget_navigation(args)

    assert exc.value.code == 1
    assert "either --bindings or --bindings-file is required" in capsys.readouterr().err


def test_cmd_verify_umg_workflow_forwards_contract_args():
    clicks = '[{"button":"StartButton","expect_active_index":1,"switcher":"ScreenSwitcher"}]'
    args = argparse.Namespace(
        widget_class="/Game/UI/WBP_Menu.WBP_Menu_C",
        root_widget=None,
        expected_widgets='["RootCanvas","StartButton","ScreenSwitcher"]',
        expected_widgets_file=None,
        expected_text='["Main Menu"]',
        expected_text_file=None,
        click_sequence=clicks,
        click_sequence_file=None,
        capture_after=True,
        pie_index=1,
        remove_preview=True,
        preview_lifecycle=None,
        viewport_z_order=None,
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_verify_umg_workflow(args)

    mock_run.assert_called_once_with(
        "verify-umg-workflow",
        {
            "widget_class": "/Game/UI/WBP_Menu.WBP_Menu_C",
            "expected_widgets": ["RootCanvas", "StartButton", "ScreenSwitcher"],
            "expected_text": ["Main Menu"],
            "click_sequence": [
                {
                    "button": "StartButton",
                    "expect_active_index": 1,
                    "switcher": "ScreenSwitcher",
                }
            ],
            "capture_after": True,
            "pie_index": 1,
            "remove_preview": True,
        },
    )


def test_umg_designer_apply_routes_to_apply_widget_tree():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "designer",
        "apply",
        "/Game/UI/WBP_Menu",
        "--spec",
        '{"root":{"class":"CanvasPanel","name":"RootCanvas"}}',
        "--compile",
    ])

    assert args.func == cmd_apply_widget_tree
    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "apply-widget-tree",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "spec": {"root": {"class": "CanvasPanel", "name": "RootCanvas"}},
            "compile": True,
        },
    )


def test_umg_navigation_wire_routes_to_wire_widget_navigation():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "navigation",
        "wire",
        "/Game/UI/WBP_Menu",
        "--bindings",
        '[{"button":"StartButton"}]',
        "--allow-busy",
    ])

    assert args.func == cmd_wire_widget_navigation
    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "wire-widget-navigation",
        {
            "asset_path": "/Game/UI/WBP_Menu",
            "bindings": [{"button": "StartButton"}],
            "allow_busy": True,
        },
    )


def test_umg_layout_compare_routes_to_existing_layout_handler(tmp_path, capsys):
    expected = tmp_path / "expected.json"
    actual = tmp_path / "actual.json"
    expected.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    actual.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")

    args = build_parser().parse_args([
        "umg",
        "layout",
        "compare",
        "--mode",
        "geometry",
        str(expected),
        str(actual),
    ])

    assert args.func == main_mod.cmd_umg_layout
    main_mod.cmd_umg_layout(args)

    data = json.loads(capsys.readouterr().out)
    assert data["success"] is True


def test_umg_layout_extract_runtime_resolves_preview_handle(capsys):
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "layout",
        "extract",
        "--source",
        "runtime",
        "--preview-handle",
        "softue-preview:abc",
        "--full-geometry",
    ])

    def fake_run(tool_name, arguments):
        if tool_name == "umg-preview-list":
            return {
                "success": True,
                "previews": [
                    {
                        "preview_handle": "softue-preview:abc",
                        "widget_name": "WBP_Menu_C_0",
                    }
                ],
            }
        if tool_name == "inspect-runtime-widgets":
            return {
                "source_type": "runtime",
                "widget_count": 1,
                "root_widgets": [
                    {
                        "name": "WBP_Menu_C_0",
                        "class": "WBP_Menu_C",
                        "geometry": {
                            "absolute_position": [0, 0],
                            "local_size": [1920, 1080],
                        },
                    }
                ],
            }
        raise AssertionError(tool_name)

    with patch("soft_ue_cli.__main__._run_tool", side_effect=fake_run) as mock_run:
        args.func(args)

    assert mock_run.call_args_list[0].args == ("umg-preview-list", {"pie_index": 0})
    assert mock_run.call_args_list[1].args == (
        "inspect-runtime-widgets",
        {
            "pie_index": 0,
            "root_widget": "WBP_Menu_C_0",
            "include_geometry": True,
            "include_slate": True,
        },
    )
    payload = json.loads(capsys.readouterr().out)
    assert payload["preview_handle"] == "softue-preview:abc"
    assert payload["runtime_root_widget"] == "WBP_Menu_C_0"
    assert payload["widgets"][0]["name"] == "WBP_Menu_C_0"


def test_umg_workflow_iterate_layout_writes_manifest(tmp_path, capsys):
    concept = tmp_path / "concept.json"
    spec = tmp_path / "spec.json"
    output_dir = tmp_path / "iter"
    bridge_shot = tmp_path / "bridge-shot.png"
    concept.write_text(
        json.dumps({
            "canvas_size": [1920, 1080],
            "widgets": [{"name": "StartButton", "normalized_bounds": [0.1, 0.2, 0.2, 0.1]}],
        }),
        encoding="utf-8",
    )
    spec.write_text(
        json.dumps({
            "root": {
                "class": "CanvasPanel",
                "name": "RootCanvas",
                "children": [
                    {
                        "class": "Button",
                        "name": "StartButton",
                        "slot": {"position": [100, 200], "size": [300, 100]},
                    }
                ],
            }
        }),
        encoding="utf-8",
    )
    bridge_shot.write_bytes(b"png")

    def fake_run(tool_name, arguments, **kwargs):
        if tool_name == "pie-session" and arguments["action"] == "status":
            return {"state": "stopped"}
        if tool_name == "pie-session" and arguments["action"] == "start":
            return {"success": True, "state": "running"}
        if tool_name == "apply-widget-tree":
            return {"success": True}
        if tool_name == "umg-preview-replace":
            return {"success": True, "preview_handle": "softue-preview:abc", "widget_name": "WBP_Menu_C_0"}
        if tool_name == "capture-screenshot":
            return {"file_path": str(bridge_shot), "width": 1920, "height": 1080, "mode": "file"}
        if tool_name == "inspect-runtime-widgets":
            return {
                "widget_count": 2,
                "root_widgets": [
                    {
                        "name": "WBP_Menu_C_0",
                        "class": "WBP_Menu_C",
                        "geometry": {"absolute_position": [0, 0], "local_size": [1920, 1080]},
                        "children": [
                            {
                                "name": "StartButton",
                                "class": "Button",
                                "geometry": {"absolute_position": [210, 230], "local_size": [310, 110]},
                            }
                        ],
                    }
                ],
            }
        raise AssertionError(tool_name)

    args = build_parser().parse_args([
        "umg",
        "workflow",
        "iterate-layout",
        "--asset-path",
        "/Game/UI/WBP_Menu",
        "--widget-class",
        "/Game/UI/WBP_Menu.WBP_Menu_C",
        "--concept-layout",
        str(concept),
        "--spec",
        str(spec),
        "--output-dir",
        str(output_dir),
        "--apply",
        "--compile",
        "--save",
        "--capture",
    ])

    with patch("soft_ue_cli.__main__._run_tool", side_effect=fake_run):
        args.func(args)

    payload = json.loads(capsys.readouterr().out)
    manifest = output_dir / "iteration_manifest.json"
    assert payload["manifest_file"] == str(manifest)
    assert manifest.exists()
    written = json.loads(manifest.read_text(encoding="utf-8"))
    assert written["iterations"][0]["preview_handle"] == "softue-preview:abc"
    assert Path(written["iterations"][0]["runtime_layout"]).exists()
    assert Path(written["iterations"][0]["comparison_report"]).exists()
    assert Path(written["iterations"][0]["corrected_spec"]).exists()
    assert Path(written["iterations"][0]["screenshot"]).read_bytes() == b"png"


def test_umg_preview_replace_routes_to_preview_primitive():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "preview",
        "replace",
        "--widget-class",
        "/Game/UI/WBP_Menu.WBP_Menu_C",
        "--pie-index",
        "1",
        "--viewport-z-order",
        "7",
        "--capture-after",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "umg-preview-replace",
        {
            "widget_class": "/Game/UI/WBP_Menu.WBP_Menu_C",
            "pie_index": 1,
            "viewport_z_order": 7,
            "capture_after": True,
        },
    )


def test_umg_preview_replace_forwards_viewport_layout_controls():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "preview",
        "replace",
        "--widget-class",
        "/Game/UI/WBP_Menu.WBP_Menu_C",
        "--fullscreen",
        "--viewport-anchors",
        "0,0,1,1",
        "--viewport-position",
        "0,0",
        "--viewport-size",
        "1920,1080",
        "--viewport-alignment",
        "0,0",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "umg-preview-replace",
        {
            "widget_class": "/Game/UI/WBP_Menu.WBP_Menu_C",
            "fullscreen": True,
            "viewport_anchors": [0.0, 0.0, 1.0, 1.0],
            "viewport_position": [0.0, 0.0],
            "viewport_size": [1920.0, 1080.0],
            "viewport_alignment": [0.0, 0.0],
        },
    )


def test_umg_preview_remove_routes_to_preview_primitive():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "preview",
        "remove",
        "--preview-handle",
        "softue-preview:world:widget:guid",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "umg-preview-remove",
        {
            "preview_handle": "softue-preview:world:widget:guid",
        },
    )


def test_umg_verify_navigation_routes_to_verify_umg_workflow():
    parser = build_parser()
    args = parser.parse_args([
        "umg",
        "verify",
        "navigation",
        "--click-sequence",
        '[{"button":"StartButton"}]',
        "--root-widget",
        "WBP_Menu_C_0",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "verify-umg-workflow",
        {
            "root_widget": "WBP_Menu_C_0",
            "click_sequence": [{"button": "StartButton"}],
        },
    )


# -- set-node-property (issue #28) --------------------------------------------


def test_parser_set_node_property_positional_args():
    parser = build_parser()
    args = parser.parse_args([
        "blueprint",
        "node",
        "property",
        "/Game/ABP_Hero",
        "AABB1122-CCDD-EEFF-0011-223344556677",
        '{"SpringStiffness": 450}',
    ])
    assert args.asset_path == "/Game/ABP_Hero"
    assert args.node_guid == "AABB1122-CCDD-EEFF-0011-223344556677"
    assert args.properties == '{"SpringStiffness": 450}'


def test_parser_set_node_property_alpha():
    parser = build_parser()
    args = parser.parse_args([
        "blueprint",
        "node",
        "property",
        "/Game/ABP_Hero",
        "GUID-0001",
        '{"Alpha": 0.08}',
    ])
    assert args.asset_path == "/Game/ABP_Hero"
    assert args.node_guid == "GUID-0001"
    assert args.properties == '{"Alpha": 0.08}'


# -- query-mpc (issue #32) ----------------------------------------------------


def test_parser_query_mpc_defaults():
    parser = build_parser()
    args = parser.parse_args(["query-mpc", "/Game/Materials/MPC_GlobalParams"])
    assert args.asset_path == "/Game/Materials/MPC_GlobalParams"
    assert args.action is None
    assert args.parameter_name is None
    assert args.value is None
    assert args.world is None


def test_parser_query_mpc_read_action():
    parser = build_parser()
    args = parser.parse_args(["query-mpc", "/Game/Materials/MPC_Wind", "--action", "read"])
    assert args.action == "read"


def test_parser_query_mpc_write_action():
    parser = build_parser()
    args = parser.parse_args([
        "query-mpc",
        "/Game/Materials/MPC_Wind",
        "--action", "write",
        "--parameter-name", "WindIntensity",
        "--value", "0.5",
    ])
    assert args.action == "write"
    assert args.parameter_name == "WindIntensity"
    assert args.value == "0.5"


def test_parser_query_mpc_write_vector():
    parser = build_parser()
    args = parser.parse_args([
        "query-mpc",
        "/Game/Materials/MPC_Wind",
        "--action", "write",
        "--parameter-name", "WindColor",
        "--value", "[1.0,0.5,0.0,1.0]",
    ])
    assert args.parameter_name == "WindColor"
    assert args.value == "[1.0,0.5,0.0,1.0]"


def test_parser_query_mpc_world():
    parser = build_parser()
    args = parser.parse_args(["query-mpc", "/Game/Materials/MPC_Wind", "--world", "pie"])
    assert args.world == "pie"


def test_parser_query_mpc_invalid_action_exits():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["query-mpc", "/Game/Materials/MPC_Wind", "--action", "delete"])


def test_parser_query_mpc_invalid_world_exits():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["query-mpc", "/Game/Materials/MPC_Wind", "--world", "server"])


def test_cmd_query_mpc_invalid_scalar_value_exits():
    parser = build_parser()
    args = parser.parse_args([
        "query-mpc",
        "/Game/Materials/MPC_Wind",
        "--action", "write",
        "--parameter-name", "WindIntensity",
        "--value", "abc",
    ])
    with pytest.raises(SystemExit) as exc:
        cmd_query_mpc(args)
    assert exc.value.code == 1


# -- save-asset --checkout (issue #30) ----------------------------------------


def test_parser_save_asset_defaults():
    parser = build_parser()
    args = parser.parse_args(["asset", "save", "/Game/Blueprints/BP_Player"])
    assert args.asset_path == "/Game/Blueprints/BP_Player"
    assert args.checkout is False


def test_parser_save_asset_checkout_flag():
    parser = build_parser()
    args = parser.parse_args(["asset", "save", "/Game/Blueprints/BP_Player", "--checkout"])
    assert args.asset_path == "/Game/Blueprints/BP_Player"
    assert args.checkout is True


# -- query-material --parent-chain (issue #31) --------------------------------


def test_parser_query_material_parent_chain_default():
    parser = build_parser()
    args = parser.parse_args(["query-material", "/Game/Materials/M_Rock"])
    assert args.parent_chain is False


def test_parser_query_material_parent_chain_flag():
    parser = build_parser()
    args = parser.parse_args(["query-material", "/Game/Materials/MI_Rock", "--parent-chain"])
    assert args.asset_path == "/Game/Materials/MI_Rock"
    assert args.parent_chain is True


# -- query-material MaterialFunction support (issue #39) ----------------------


def test_parser_query_material_function_path():
    parser = build_parser()
    args = parser.parse_args(["query-material", "/Game/Functions/MF_DistanceFade", "--include", "graph"])
    assert args.asset_path == "/Game/Functions/MF_DistanceFade"
    assert args.include == "graph"


# -- compile-material (issue #43) ---------------------------------------------


def test_parser_compile_material():
    parser = build_parser()
    args = parser.parse_args(["compile-material", "/Game/Materials/M_Rock"])
    assert args.asset_path == "/Game/Materials/M_Rock"


# -- get-logs Unicode encoding (issue #40) ------------------------------------


def test_print_json_unicode_survives_replace_encoding(capsys):
    """Ensure _print_json doesn't crash on chars outside the current locale."""
    from soft_ue_cli.__main__ import _print_json
    _print_json({"msg": "hello \u2014 world"})
    captured = capsys.readouterr()
    assert "hello" in captured.out


def test_print_json_unicode_falls_back_for_strict_cp949_stdout(monkeypatch):
    """Ensure _print_json remains usable before main() can reconfigure stdout."""
    import io
    import sys

    from soft_ue_cli.__main__ import _print_json

    buffer = io.BytesIO()
    stdout = io.TextIOWrapper(buffer, encoding="cp949", errors="strict", newline="")
    monkeypatch.setattr(sys, "stdout", stdout)

    _print_json({"msg": "hello \u2014 world", "text": "한글"})
    stdout.flush()

    output = buffer.getvalue().decode("cp949")
    assert "\\u2014" in output
    assert "\\ud55c\\uae00" in output


# -- query-level --include-foliage / --include-grass (issue #34) --------------


def test_parser_query_level_include_foliage_default():
    parser = build_parser()
    args = parser.parse_args(["query-level"])
    assert args.include_foliage is False


def test_parser_query_level_include_grass_default():
    parser = build_parser()
    args = parser.parse_args(["query-level"])
    assert args.include_grass is False


def test_parser_query_level_include_foliage_flag():
    parser = build_parser()
    args = parser.parse_args(["query-level", "--include-foliage"])
    assert args.include_foliage is True
    assert args.include_grass is False


def test_parser_query_level_include_grass_flag():
    parser = build_parser()
    args = parser.parse_args(["query-level", "--include-grass"])
    assert args.include_grass is True
    assert args.include_foliage is False


def test_parser_query_level_both_foliage_and_grass():
    parser = build_parser()
    args = parser.parse_args(["query-level", "--include-foliage", "--include-grass"])
    assert args.include_foliage is True
    assert args.include_grass is True


# -- MSYS path mangling fix (issue #44) ---------------------------------------


def test_fix_msys_path_mangling():
    from soft_ue_cli.__main__ import _fix_msys_asset_path
    # Mangled by Git Bash
    assert _fix_msys_asset_path("C:/Program Files/Git/Game/Materials/M_Rock") == "/Game/Materials/M_Rock"
    assert _fix_msys_asset_path("C:/Program Files/Git/Engine/Content/Foo") == "/Engine/Content/Foo"
    # Already correct ??pass through
    assert _fix_msys_asset_path("/Game/Materials/M_Rock") == "/Game/Materials/M_Rock"
    # No mount point ??pass through
    assert _fix_msys_asset_path("some/local/path") == "some/local/path"
    # Empty/None
    assert _fix_msys_asset_path("") == ""


def test_cmd_add_graph_node_invalid_position_exits():
    parser = build_parser()
    args = parser.parse_args(["blueprint", "node", "add",
        "/Game/BP_Player",
        "K2Node_CallFunction",
        "--position", "x,y",
    ])
    with pytest.raises(SystemExit) as exc:
        cmd_add_graph_node(args)
    assert exc.value.code == 1


def test_cmd_add_graph_node_accepts_mcp_native_position_array():
    args = argparse.Namespace(
        asset_path="/Game/BP_Player",
        graph_name="EventGraph",
        node_class="K2Node_IfThenElse",
        position=[400, 0],
        no_auto_position=False,
        connect_to_node=None,
        connect_to_pin=None,
        properties=None,
    )

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_graph_node(args)

    mock_run.assert_called_once_with(
        "add-graph-node",
        {
            "asset_path": "/Game/BP_Player",
            "node_class": "K2Node_IfThenElse",
            "graph_name": "EventGraph",
            "position": [400, 0],
        },
    )


# -- AnimBlueprint state machine authoring -----------------------------------


def test_cmd_add_anim_state_machine_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["anim", "state-machine", "add",
        "/Game/Animation/ABP_Hero",
        "Locomotion",
        "--graph-name", "AnimGraph",
        "--default-state", "Idle",
        "--position", "120,240",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_anim_state_machine(args)

    mock_run.assert_called_once_with(
        "add-anim-state-machine",
        {
            "asset_path": "/Game/Animation/ABP_Hero",
            "state_machine_name": "Locomotion",
            "graph_name": "AnimGraph",
            "default_state": "Idle",
            "position": [120, 240],
        },
    )


def test_cmd_add_anim_state_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["anim", "state", "add",
        "/Game/Animation/ABP_Hero",
        "Locomotion",
        "Run",
        "--entry",
        "--position", "480,120",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_anim_state(args)

    mock_run.assert_called_once_with(
        "add-anim-state",
        {
            "asset_path": "/Game/Animation/ABP_Hero",
            "state_machine_name": "Locomotion",
            "state_name": "Run",
            "entry": True,
            "position": [480, 120],
        },
    )


def test_cmd_add_anim_transition_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["anim", "transition", "add",
        "/Game/Animation/ABP_Hero",
        "Locomotion",
        "Idle",
        "Run",
        "--crossfade-duration", "0.15",
        "--priority", "2",
        "--bidirectional",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_add_anim_transition(args)

    mock_run.assert_called_once_with(
        "add-anim-transition",
        {
            "asset_path": "/Game/Animation/ABP_Hero",
            "state_machine_name": "Locomotion",
            "source_state": "Idle",
            "target_state": "Run",
            "crossfade_duration": 0.15,
            "priority": 2,
            "bidirectional": True,
        },
    )


# -- query-enum / query-struct ------------------------------------------------


def test_parser_query_enum():
    parser = build_parser()
    args = parser.parse_args(["query-enum", "/Game/Data/E_MenuState"])
    assert args.asset_path == "/Game/Data/E_MenuState"
    assert args.func == cmd_query_enum


def test_cmd_query_enum_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["query-enum", "/Game/Data/E_MenuState"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"enumerators": []}) as mock_call:
        cmd_query_enum(args)
    mock_call.assert_called_once_with("query-enum", {"asset_path": "/Game/Data/E_MenuState"})


def test_parser_query_struct():
    parser = build_parser()
    args = parser.parse_args(["query-struct", "/Game/Data/S_Result"])
    assert args.asset_path == "/Game/Data/S_Result"
    assert args.func == cmd_query_struct


def test_cmd_query_struct_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["query-struct", "/Game/Data/S_Result"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"members": []}) as mock_call:
        cmd_query_struct(args)
    mock_call.assert_called_once_with("query-struct", {"asset_path": "/Game/Data/S_Result"})


# -- batch-call ----------------------------------------------------------------


def test_batch_call_parses_required_json():
    payload = '[{"tool":"pie-tick","args":{"frames":10}}]'
    args = build_parser().parse_args(["batch-call", "--calls", payload])
    assert args.command == "batch-call"
    assert args.calls == payload
    assert args.calls_file is None
    assert args.continue_on_error is False


def test_batch_call_parses_file_and_continue_flag():
    args = build_parser().parse_args(["batch-call", "--calls-file", "scenario.json", "--continue-on-error"])
    assert args.calls is None
    assert args.calls_file == "scenario.json"
    assert args.continue_on_error is True


def test_batch_call_forwards_to_run_tool():
    ns = argparse.Namespace(
        calls='[{"tool":"pie-tick","args":{"frames":5}}]',
        calls_file=None,
        continue_on_error=False,
    )
    with patch("soft_ue_cli.__main__._run_tool", return_value={"status": "ok"}) as mock_run:
        cmd_batch_call(ns)
    mock_run.assert_called_once_with("batch-call", {"calls": [{"tool": "pie-tick", "args": {"frames": 5}}]})


def test_batch_call_rejects_non_array_json():
    ns = argparse.Namespace(calls='{"tool":"pie-tick"}', calls_file=None, continue_on_error=False)
    with pytest.raises(SystemExit) as exc:
        cmd_batch_call(ns)
    assert exc.value.code == 1


# -- pie-session ---------------------------------------------------------------


def test_pie_session_continue_on_blueprint_compile_errors_forwards_action():
    args = build_parser().parse_args([
        "pie-session",
        "start",
        "--continue-on-blueprint-compile-errors",
    ])
    assert args.func == cmd_pie_session

    with patch("soft_ue_cli.__main__.call_tool", return_value={"state": "starting"}) as mock_call:
        cmd_pie_session(args)

    mock_call.assert_called_once_with(
        "pie-session",
        {"action": "start", "blueprint_error_action": "continue"},
    )


def test_pie_session_blueprint_error_report_preflight_forwards_options():
    args = build_parser().parse_args([
        "pie-session",
        "start",
        "--blueprint-error-action",
        "report",
        "--preflight-blueprints",
    ])

    with patch("soft_ue_cli.__main__.call_tool", return_value={"state": "blocked_by_blueprint_compile_errors"}) as mock_call:
        cmd_pie_session(args)

    mock_call.assert_called_once_with(
        "pie-session",
        {
            "action": "start",
            "blueprint_error_action": "report",
            "preflight_blueprints": True,
        },
    )


# -- pie-tick ------------------------------------------------------------------


def test_pie_tick_parses_required_frames():
    args = build_parser().parse_args(["pie-tick", "--frames", "30"])
    assert args.command == "pie-tick"
    assert args.frames == 30
    assert args.delta is None
    assert args.no_auto_start is False
    assert args.map is None


def test_pie_tick_parses_all_flags():
    args = build_parser().parse_args([
        "pie-tick",
        "--frames", "60",
        "--delta", "0.0166666",
        "--no-auto-start",
        "--map", "/Game/Maps/Test",
        "--timeout", "42.5",
    ])
    assert args.frames == 60
    assert args.delta == pytest.approx(0.0166666)
    assert args.no_auto_start is True
    assert args.map == "/Game/Maps/Test"
    assert args.timeout == 42.5


def test_pie_tick_forwards_to_run_tool():
    ns = argparse.Namespace(frames=30, delta=None, no_auto_start=False, map=None, timeout=None)
    with patch("soft_ue_cli.__main__._run_tool", return_value={"ticks": 30}) as mock_run:
        cmd_pie_tick(ns)
    mock_run.assert_called_once_with("pie-tick", {"frames": 30}, timeout=90.0)


def test_pie_tick_forwards_timeout_to_tool():
    ns = argparse.Namespace(frames=30, delta=None, no_auto_start=False, map=None, timeout=7.5)
    with patch("soft_ue_cli.__main__._run_tool", return_value={"ticks": 30}) as mock_run:
        cmd_pie_tick(ns)
    mock_run.assert_called_once_with("pie-tick", {"frames": 30, "timeout": 7.5}, timeout=67.5)


def test_pie_tick_exits_nonzero_for_structured_native_timeout(capsys):
    ns = argparse.Namespace(frames=30, delta=None, no_auto_start=False, map=None, timeout=1.0)
    with patch(
        "soft_ue_cli.__main__._run_tool",
        return_value={
            "success": False,
            "status": "timeout",
            "tool": "pie-tick",
            "active_phase": "tick_frames",
        },
    ):
        with pytest.raises(SystemExit) as exc:
            cmd_pie_tick(ns)

    assert exc.value.code == 1
    result = json.loads(capsys.readouterr().out)
    assert result["active_phase"] == "tick_frames"


# -- inspect-anim-instance -----------------------------------------------------


def test_inspect_anim_instance_parses_required():
    args = build_parser().parse_args(["anim", "instance", "inspect", "--actor-tag", "TestCharacter"])
    assert args.command == "anim"
    assert args.anim_action == "instance"
    assert args.anim_instance_action == "inspect"
    assert args.actor_tag == "TestCharacter"
    assert args.mesh_component is None
    assert args.include is None
    assert args.blend_weights is None


def test_inspect_anim_instance_parses_all_flags():
    args = build_parser().parse_args(["anim", "instance", "inspect",
        "--actor-tag", "TestCharacter",
        "--mesh-component", "CharacterMesh0",
        "--include", "state_machines,montages",
        "--blend-weights", "LayerAim,LayerLocomotion",
    ])
    assert args.mesh_component == "CharacterMesh0"
    assert args.include == "state_machines,montages"
    assert args.blend_weights == "LayerAim,LayerLocomotion"


def test_inspect_anim_instance_forwards_to_run_tool():
    ns = argparse.Namespace(
        actor_tag="TestCharacter",
        asset_path=None,
        mesh_component="CharacterMesh0",
        include="state_machines,montages",
        blend_weights="LayerAim",
    )
    with patch("soft_ue_cli.__main__._run_tool", return_value={"anim_instance_class": "/Script/Test"}) as mock_run:
        cmd_inspect_anim_instance(ns)
    mock_run.assert_called_once_with(
        "inspect-anim-instance",
        {
            "actor_tag": "TestCharacter",
            "mesh_component": "CharacterMesh0",
            "include": ["state_machines", "montages"],
            "blend_weights": ["LayerAim"],
        },
    )


def test_inspect_anim_instance_accepts_asset_path_without_actor_tag():
    args = build_parser().parse_args(["anim", "instance", "inspect",
        "--asset-path",
        "/Game/Animation/ABP_Player",
        "--include",
        "topology,sync_groups",
    ])

    assert args.actor_tag is None
    assert args.asset_path == "/Game/Animation/ABP_Player"

    with patch("soft_ue_cli.__main__._run_tool", return_value={"mode": "asset"}) as mock_run, patch(
        "soft_ue_cli.__main__._print_json"
    ):
        args.func(args)

    mock_run.assert_called_once_with(
        "inspect-anim-instance",
        {
            "asset_path": "/Game/Animation/ABP_Player",
            "include": ["topology", "sync_groups"],
        },
    )


def test_inspect_anim_instance_requires_actor_tag_or_asset_path():
    args = argparse.Namespace(
        actor_tag=None,
        asset_path=None,
        mesh_component=None,
        include=None,
        blend_weights=None,
    )

    with pytest.raises(SystemExit) as exc:
        cmd_inspect_anim_instance(args)

    assert exc.value.code == 1


def test_sync_marker_commands_parse_and_forward():
    parser = build_parser()

    inspect_args = parser.parse_args(["anim", "sync-marker", "inspect", "/Game/Animation/Run"])
    compare_args = parser.parse_args(["anim", "sync-marker", "compare",
        "/Game/Animation/Walk",
        "/Game/Animation/Run",
        "--marker",
        "Foot_L",
    ])
    add_args = parser.parse_args(["anim", "sync-marker", "add", "/Game/Animation/Run", "Foot_L", "0.25", "--save"])
    remove_args = parser.parse_args(["anim", "sync-marker", "remove",
        "/Game/Animation/Run",
        "Foot_L",
        "--time",
        "0.25",
        "--tolerance",
        "0.01",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={}) as mock_run, patch(
        "soft_ue_cli.__main__._print_json"
    ):
        inspect_args.func(inspect_args)
        compare_args.func(compare_args)
        add_args.func(add_args)
        remove_args.func(remove_args)

    assert mock_run.call_args_list[0].args == ("inspect-sync-markers", {"asset_path": "/Game/Animation/Run"})
    assert mock_run.call_args_list[1].args == (
        "compare-sync-markers",
        {
            "asset_paths": ["/Game/Animation/Walk", "/Game/Animation/Run"],
            "marker": "Foot_L",
        },
    )
    assert mock_run.call_args_list[2].args == (
        "add-sync-marker",
        {
            "asset_path": "/Game/Animation/Run",
            "marker": "Foot_L",
            "time": 0.25,
            "save": True,
        },
    )
    assert mock_run.call_args_list[3].args == (
        "remove-sync-marker",
        {
            "asset_path": "/Game/Animation/Run",
            "marker": "Foot_L",
            "time": 0.25,
            "tolerance": 0.01,
        },
    )


# -- call-function extensions --------------------------------------------------


def test_call_function_cdo_mode():
    args = build_parser().parse_args([
        "call-function",
        "--class-path", "/Game/Test/BP_TestActor",
        "--function-name", "ComputeValue",
        "--use-cdo",
        "--args", '{"flag":true}',
    ])
    assert args.class_path == "/Game/Test/BP_TestActor"
    assert args.use_cdo is True
    assert args.spawn_transient is False
    assert args.actor_name is None


def test_call_function_transient_mode_with_seed():
    args = build_parser().parse_args([
        "call-function",
        "--class-path", "/Game/Foo",
        "--function-name", "Bar",
        "--spawn-transient",
        "--seed", "42",
    ])
    assert args.class_path == "/Game/Foo"
    assert args.spawn_transient is True
    assert args.seed == 42


def test_call_function_batch_json_forwards(tmp_path):
    batch = [{"arg1": 1}, {"arg1": 2}]
    batch_file = tmp_path / "sweep.json"
    batch_file.write_text(json.dumps(batch), encoding="utf-8")

    ns = argparse.Namespace(
        actor_name=None,
        class_path="/Game/Foo",
        function_name="Bar",
        args=None,
        spawn_transient=False,
        use_cdo=True,
        seed=None,
        world=None,
        batch_json=str(batch_file),
        output=None,
    )
    with patch("soft_ue_cli.__main__._run_tool", return_value={"success": True}) as mock_run:
        cmd_call_function(ns)
    mock_run.assert_called_once_with(
        "call-function",
        {"function_name": "Bar", "class_path": "/Game/Foo", "use_cdo": True, "batch": batch},
    )
