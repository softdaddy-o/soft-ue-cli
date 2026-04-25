"""Tests for cli/soft_ue_cli/__main__.py — argument parsing and cmd_setup output."""
from __future__ import annotations

import json
import sys
from unittest.mock import patch

import pytest

from soft_ue_cli.__main__ import (
    _SCRIPTS_DIR,
    _claude_md_section,
    _parse_vector,
    _validate_script_name,
    build_parser,
    cmd_delete_script,
    cmd_list_scripts,
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
