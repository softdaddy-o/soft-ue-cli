"""Tests for INI layer merge engine."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.ini_parser import UeIniFile
from soft_ue_cli.config.merge import merge_ini_layers, trace_key


def test_merge_simple_override():
    merged = merge_ini_layers([UeIniFile.from_string("[S]\nK=base\n"), UeIniFile.from_string("[S]\nK=project\n")])
    assert merged.get("S", "K") == "project"


def test_merge_array_append_across_layers():
    merged = merge_ini_layers([UeIniFile.from_string("[S]\n+Arr=one\n"), UeIniFile.from_string("[S]\n+Arr=two\n")])
    assert merged.get_array("S", "Arr") == ["one", "two"]


def test_merge_array_remove_across_layers():
    merged = merge_ini_layers([UeIniFile.from_string("[S]\n+Arr=one\n+Arr=two\n"), UeIniFile.from_string("[S]\n-Arr=one\n")])
    assert merged.get_array("S", "Arr") == ["two"]


def test_merge_delete_across_layers():
    merged = merge_ini_layers([UeIniFile.from_string("[S]\nK=base\n"), UeIniFile.from_string("[S]\n!K\n")])
    assert merged.get("S", "K") is None


def test_merge_clear_and_add_across_layers():
    merged = merge_ini_layers([UeIniFile.from_string("[S]\n+Arr=one\n+Arr=two\n"), UeIniFile.from_string("[S]\n.Arr=only\n")])
    assert merged.get_array("S", "Arr") == ["only"]


def test_merge_preserves_sections_from_all_layers():
    merged = merge_ini_layers([UeIniFile.from_string("[A]\nK=1\n"), UeIniFile.from_string("[B]\nK=2\n")])
    assert set(merged.sections()) >= {"A", "B"}


def test_merge_empty_layers():
    assert merge_ini_layers([]).sections() == []


def test_trace_key_shows_all_layers():
    base = UeIniFile.from_string("[S]\nK=base\n")
    base.path = Path("Base.ini")
    project = UeIniFile.from_string("[S]\nK=project\n")
    project.path = Path("Default.ini")
    trace = trace_key([base, project], "S", "K")
    assert len(trace) == 2
    assert trace[0]["value"] == "base"
    assert trace[1]["value"] == "project"


def test_trace_key_missing_in_layer():
    base = UeIniFile.from_string("[S]\nK=base\n")
    base.path = Path("Base.ini")
    project = UeIniFile.from_string("[S]\nOther=x\n")
    project.path = Path("Default.ini")
    trace = trace_key([base, project], "S", "K")
    assert len(trace) == 1
