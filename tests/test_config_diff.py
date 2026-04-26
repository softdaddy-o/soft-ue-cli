"""Tests for config diff engine."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.diff import diff_ini_files, diff_xml_files
from soft_ue_cli.config.ini_parser import UeIniFile
from soft_ue_cli.config.xml_parser import BuildConfigXml


def test_diff_added_key():
    result = diff_ini_files(UeIniFile.from_string("[S]\nK=V\n"), UeIniFile.from_string("[S]\nK=V\nNew=X\n"))
    assert any(change["key"] == "New" and change["type"] == "added" for change in result["sections"]["S"]["changes"])


def test_diff_removed_key():
    result = diff_ini_files(UeIniFile.from_string("[S]\nK=V\nOld=X\n"), UeIniFile.from_string("[S]\nK=V\n"))
    assert any(change["key"] == "Old" and change["type"] == "removed" for change in result["sections"]["S"]["changes"])


def test_diff_changed_value():
    result = diff_ini_files(UeIniFile.from_string("[S]\nK=old\n"), UeIniFile.from_string("[S]\nK=new\n"))
    assert any(
        change["key"] == "K" and change["type"] == "changed" and change["old"] == "old" and change["new"] == "new"
        for change in result["sections"]["S"]["changes"]
    )


def test_diff_no_changes():
    assert diff_ini_files(UeIniFile.from_string("[S]\nK=V\n"), UeIniFile.from_string("[S]\nK=V\n"))["total_changes"] == 0


def test_diff_added_section():
    result = diff_ini_files(UeIniFile.from_string("[S]\nK=V\n"), UeIniFile.from_string("[S]\nK=V\n[New]\nX=Y\n"))
    assert "New" in result["sections"]


def test_diff_xml_changed():
    a = BuildConfigXml.from_string(
        '<?xml version="1.0"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><BuildConfiguration><bCompilePhysX>true</bCompilePhysX></BuildConfiguration></Configuration>',
    )
    b = BuildConfigXml.from_string(
        '<?xml version="1.0"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><BuildConfiguration><bCompilePhysX>false</bCompilePhysX></BuildConfiguration></Configuration>',
    )
    result = diff_xml_files(a, b)
    assert any(
        change["key"] == "bCompilePhysX" and change["old"] == "true" and change["new"] == "false"
        for change in result["sections"]["BuildConfiguration"]["changes"]
    )
