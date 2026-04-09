"""Tests for cli/soft_ue_cli/__main__.py ??argument parsing and cmd_setup output."""

from __future__ import annotations

import json
from unittest.mock import patch

import pytest


from soft_ue_cli.__main__ import (
    _SCRIPTS_DIR,
    _claude_md_section,
    _parse_int_list,
    _parse_vector,
    _validate_script_name,
    build_parser,
    cmd_add_graph_node,
    cmd_capture_screenshot,
    cmd_capture_viewport,
    cmd_delete_script,
    cmd_list_scripts,
    cmd_query_mpc,
    cmd_run_python_script,
    cmd_save_script,
    cmd_setup,
)


# -- _parse_vector -------------------------------------------------------------


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


def test_parser_get_logs_defaults():
    parser = build_parser()
    args = parser.parse_args(["get-logs"])
    assert args.lines == 100
    assert args.raw is False


def test_parser_set_console_var():
    parser = build_parser()
    args = parser.parse_args(["set-console-var", "r.VSync", "0"])
    assert args.name == "r.VSync"
    assert args.value == "0"


def test_parser_get_console_var():
    parser = build_parser()
    args = parser.parse_args(["get-console-var", "t.MaxFPS"])
    assert args.name == "t.MaxFPS"


def test_parser_inspect_uasset():
    parser = build_parser()
    args = parser.parse_args(["inspect-uasset", "BP_Player.uasset", "--sections", "summary", "--format", "json"])
    assert args.file_path == "BP_Player.uasset"
    assert args.sections == "summary"
    assert args.format == "json"


def test_parser_diff_uasset():
    parser = build_parser()
    args = parser.parse_args(["diff-uasset", "BP_Old.uasset", "BP_New.uasset", "--sections", "variables"])
    assert args.left_file == "BP_Old.uasset"
    assert args.right_file == "BP_New.uasset"
    assert args.sections == "variables"


def test_parser_call_function_no_args():
    parser = build_parser()
    args = parser.parse_args(["call-function", "BP_Hero", "Jump"])
    assert args.actor_name == "BP_Hero"
    assert args.function_name == "Jump"
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
    mock_call.assert_called_once_with("run-python-script", {"script": "print('run')"})


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


# -- capture-screenshot parser -------------------------------------------------


def test_parser_capture_screenshot_window():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "window"])
    assert args.mode == "window"
    assert args.func == cmd_capture_screenshot


def test_parser_capture_screenshot_tab():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "tab", "--window-name", "Blueprint"])
    assert args.mode == "tab"
    assert args.window_name == "Blueprint"


def test_parser_capture_screenshot_region():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "region", "--region", "0,0,800,600"])
    assert args.mode == "region"
    assert args.region == "0,0,800,600"


def test_parser_capture_screenshot_viewport():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "viewport"])
    assert args.mode == "viewport"


def test_parser_capture_screenshot_format_and_output():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "window", "--format", "png", "--output", "file"])
    assert args.format == "png"
    assert args.output == "file"


def test_parser_capture_screenshot_invalid_mode():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["capture-screenshot", "invalid"])


def test_cmd_capture_screenshot_window_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "window"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with("capture-screenshot", {"mode": "window"})


def test_cmd_capture_screenshot_tab_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "tab", "--window-name", "OutputLog"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "tab", "window_name": "OutputLog"}
    )


def test_cmd_capture_screenshot_region_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "region", "--region", "10,20,800,600"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "region", "region": [10, 20, 800, 600]}
    )


def test_cmd_capture_screenshot_viewport_calls_tool():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "viewport", "--format", "png"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/shot.png"}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "viewport", "format": "png"}
    )


def test_cmd_capture_screenshot_all_options():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "window", "--format", "jpeg", "--output", "base64"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"image_base64": "..."}) as mock_call:
        cmd_capture_screenshot(args)
    mock_call.assert_called_once_with(
        "capture-screenshot", {"mode": "window", "format": "jpeg", "output": "base64"}
    )


def test_cmd_capture_screenshot_invalid_region_exits():
    parser = build_parser()
    args = parser.parse_args(["capture-screenshot", "region", "--region", "a,b,c,d"])
    with pytest.raises(SystemExit) as exc:
        cmd_capture_screenshot(args)
    assert exc.value.code == 1


# -- capture-viewport parser & cmd ---------------------------------------------


def test_parser_capture_viewport_defaults():
    parser = build_parser()
    args = parser.parse_args(["capture-viewport"])
    assert args.func == cmd_capture_viewport
    assert args.format is None
    assert args.output is None


def test_parser_capture_viewport_with_options():
    parser = build_parser()
    args = parser.parse_args(["capture-viewport", "--format", "jpeg", "--output", "base64"])
    assert args.format == "jpeg"
    assert args.output == "base64"


def test_cmd_capture_viewport_default():
    parser = build_parser()
    args = parser.parse_args(["capture-viewport"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/vp.png"}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with("capture-viewport", {})


def test_cmd_capture_viewport_with_format():
    parser = build_parser()
    args = parser.parse_args(["capture-viewport", "--format", "png"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"file_path": "/tmp/vp.png"}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with("capture-viewport", {"format": "png"})


def test_cmd_capture_viewport_all_options():
    parser = build_parser()
    args = parser.parse_args(["capture-viewport", "--format", "jpeg", "--output", "base64"])
    with patch("soft_ue_cli.__main__.call_tool", return_value={"image_base64": "..."}) as mock_call:
        cmd_capture_viewport(args)
    mock_call.assert_called_once_with("capture-viewport", {"format": "jpeg", "output": "base64"})


# -- inspect-runtime-widgets ---------------------------------------------------


def test_parser_inspect_runtime_widgets_defaults():
    parser = build_parser()
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
    parser = build_parser()
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


# -- set-node-property (issue #28) --------------------------------------------


def test_parser_set_node_property_positional_args():
    parser = build_parser()
    args = parser.parse_args([
        "set-node-property",
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
        "set-node-property",
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
    args = parser.parse_args(["save-asset", "/Game/Blueprints/BP_Player"])
    assert args.asset_path == "/Game/Blueprints/BP_Player"
    assert args.checkout is False


def test_parser_save_asset_checkout_flag():
    parser = build_parser()
    args = parser.parse_args(["save-asset", "/Game/Blueprints/BP_Player", "--checkout"])
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
    args = parser.parse_args([
        "add-graph-node",
        "/Game/BP_Player",
        "K2Node_CallFunction",
        "--position", "x,y",
    ])
    with pytest.raises(SystemExit) as exc:
        cmd_add_graph_node(args)
    assert exc.value.code == 1
