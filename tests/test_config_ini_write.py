"""Tests for UE INI write and round-trip."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.ini_parser import UeIniFile


def test_set_scalar_value():
    ini = UeIniFile()
    ini.set("S", "K", "V")
    assert ini.get("S", "K") == "V"


def test_set_overwrites_previous():
    ini = UeIniFile()
    ini.set("S", "K", "old")
    ini.set("S", "K", "new")
    assert ini.get("S", "K") == "new"


def test_append_array():
    ini = UeIniFile()
    ini.append("S", "Arr", "A")
    ini.append("S", "Arr", "B")
    assert ini.get_array("S", "Arr") == ["A", "B"]


def test_delete_key():
    ini = UeIniFile()
    ini.set("S", "K", "V")
    ini.delete("S", "K")
    assert ini.get("S", "K") is None


def test_to_string_roundtrip():
    original = "[/Script/Engine.RendererSettings]\nr.Bloom=True\nr.Exposure=1.5\n"
    reparsed = UeIniFile.from_string(UeIniFile.from_string(original).to_string())
    assert reparsed.get("/Script/Engine.RendererSettings", "r.Bloom") == "True"
    assert reparsed.get("/Script/Engine.RendererSettings", "r.Exposure") == "1.5"


def test_to_string_array_operators():
    ini = UeIniFile()
    ini.append("S", "Arr", "One")
    ini.append("S", "Arr", "Two")
    text = ini.to_string()
    assert "+Arr=One" in text
    assert "+Arr=Two" in text


def test_write_to_file(tmp_path):
    ini = UeIniFile()
    ini.set("S", "K", "V")
    path = tmp_path / "out.ini"
    ini.write(path)
    assert UeIniFile.from_file(path).get("S", "K") == "V"


def test_write_creates_parent_dirs(tmp_path):
    ini = UeIniFile()
    ini.set("S", "K", "V")
    path = tmp_path / "sub" / "dir" / "out.ini"
    ini.write(path)
    assert path.exists()
