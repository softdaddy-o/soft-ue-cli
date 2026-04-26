"""Tests for UE-style INI parser."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.ini_parser import UeIniFile


def test_parse_simple_key_value():
    ini = UeIniFile.from_string("[MySection]\nMyKey=MyValue\n")
    assert ini.get("MySection", "MyKey") == "MyValue"


def test_parse_multiple_sections():
    ini = UeIniFile.from_string("[SectionA]\nA=1\n[SectionB]\nB=2\n")
    assert ini.get("SectionA", "A") == "1"
    assert ini.get("SectionB", "B") == "2"


def test_parse_slash_section_name():
    ini = UeIniFile.from_string("[/Script/Engine.RendererSettings]\nr.Bloom=True\n")
    assert ini.get("/Script/Engine.RendererSettings", "r.Bloom") == "True"


def test_parse_comment_lines():
    ini = UeIniFile.from_string("; comment\n[S]\n; another comment\nK=V\n")
    assert ini.get("S", "K") == "V"


def test_parse_empty_value():
    ini = UeIniFile.from_string("[S]\nK=\n")
    assert ini.get("S", "K") == ""


def test_parse_quoted_value():
    ini = UeIniFile.from_string('[S]\nK="hello world"\n')
    assert ini.get("S", "K") == "hello world"


def test_parse_array_append():
    ini = UeIniFile.from_string("[S]\n+Arr=One\n+Arr=Two\n+Arr=Three\n")
    assert ini.get_array("S", "Arr") == ["One", "Two", "Three"]


def test_parse_array_remove():
    ini = UeIniFile.from_string("[S]\n+Arr=One\n+Arr=Two\n-Arr=One\n")
    assert ini.get_array("S", "Arr") == ["Two"]


def test_parse_array_clear_and_add():
    ini = UeIniFile.from_string("[S]\n+Arr=One\n+Arr=Two\n.Arr=Only\n")
    assert ini.get_array("S", "Arr") == ["Only"]


def test_parse_delete_key():
    ini = UeIniFile.from_string("[S]\nK=V\n!K\n")
    assert ini.get("S", "K") is None


def test_get_missing_key_returns_none():
    ini = UeIniFile.from_string("[S]\nK=V\n")
    assert ini.get("S", "Missing") is None


def test_get_missing_section_returns_none():
    ini = UeIniFile.from_string("[S]\nK=V\n")
    assert ini.get("NoSuch", "K") is None


def test_sections_list():
    ini = UeIniFile.from_string("[A]\nK=1\n[B]\nK=2\n")
    assert set(ini.sections()) == {"A", "B"}


def test_keys_in_section():
    ini = UeIniFile.from_string("[S]\nA=1\nB=2\nC=3\n")
    assert set(ini.keys("S")) >= {"A", "B", "C"}


def test_from_file(tmp_path):
    path = tmp_path / "Test.ini"
    path.write_text("[S]\nK=V\n", encoding="utf-8")
    assert UeIniFile.from_file(path).get("S", "K") == "V"
