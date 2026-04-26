"""Tests for BuildConfiguration.xml parser."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.xml_parser import BuildConfigXml

SAMPLE_XML = """\
<?xml version="1.0" encoding="utf-8"?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
    <BuildConfiguration>
        <bCompilePhysX>true</bCompilePhysX>
        <bCompileAPEX>true</bCompileAPEX>
        <MaxParallelActions>12</MaxParallelActions>
    </BuildConfiguration>
    <WindowsPlatform>
        <Compiler>VisualStudio2022</Compiler>
    </WindowsPlatform>
</Configuration>
"""


def test_parse_simple_value():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert xml.get("BuildConfiguration", "bCompilePhysX") == "true"


def test_parse_nested_section():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert xml.get("WindowsPlatform", "Compiler") == "VisualStudio2022"


def test_get_missing_returns_none():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert xml.get("BuildConfiguration", "NoSuchKey") is None


def test_get_missing_section_returns_none():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert xml.get("NoSuchSection", "Key") is None


def test_sections_list():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert set(xml.sections()) >= {"BuildConfiguration", "WindowsPlatform"}


def test_keys_in_section():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    assert set(xml.keys("BuildConfiguration")) >= {"bCompilePhysX", "bCompileAPEX", "MaxParallelActions"}


def test_items():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    items = xml.items("BuildConfiguration")
    assert items["bCompilePhysX"] == "true"
    assert items["MaxParallelActions"] == "12"


def test_set_value():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    xml.set("BuildConfiguration", "bCompilePhysX", "false")
    assert xml.get("BuildConfiguration", "bCompilePhysX") == "false"


def test_set_new_key():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    xml.set("BuildConfiguration", "NewKey", "NewValue")
    assert xml.get("BuildConfiguration", "NewKey") == "NewValue"


def test_set_new_section():
    xml = BuildConfigXml.from_string(SAMPLE_XML)
    xml.set("LinuxPlatform", "Compiler", "Clang")
    assert xml.get("LinuxPlatform", "Compiler") == "Clang"


def test_write_roundtrip(tmp_path):
    xml = BuildConfigXml.from_file(_write_sample_xml(tmp_path))
    assert xml.get("BuildConfiguration", "bCompilePhysX") == "true"


def test_from_file(tmp_path):
    path = _write_sample_xml(tmp_path)
    assert BuildConfigXml.from_file(path).get("BuildConfiguration", "bCompilePhysX") == "true"


def _write_sample_xml(tmp_path: Path) -> Path:
    path = tmp_path / "BuildConfiguration.xml"
    BuildConfigXml.from_string(SAMPLE_XML).write(path)
    return path
