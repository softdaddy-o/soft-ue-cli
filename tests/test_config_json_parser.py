"""Tests for .uproject/.uplugin JSON parser."""

from __future__ import annotations

from pathlib import Path


from soft_ue_cli.config.json_parser import ProjectJson

SAMPLE_UPROJECT = """\
{
    "FileVersion": 3,
    "EngineAssociation": "5.5",
    "Category": "",
    "Description": "My Game",
    "Modules": [
        {
            "Name": "MyGame",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }
    ],
    "Plugins": [
        {
            "Name": "SoftUEBridge",
            "Enabled": true
        }
    ]
}
"""


def test_get_scalar():
    assert ProjectJson.from_string(SAMPLE_UPROJECT).get("EngineAssociation") == "5.5"


def test_get_nested_dot_path():
    assert ProjectJson.from_string(SAMPLE_UPROJECT).get("Modules.0.Name") == "MyGame"


def test_get_missing_returns_none():
    assert ProjectJson.from_string(SAMPLE_UPROJECT).get("NoSuchKey") is None


def test_keys():
    assert set(ProjectJson.from_string(SAMPLE_UPROJECT).keys()) >= {"FileVersion", "EngineAssociation", "Modules", "Plugins"}


def test_set_scalar():
    project = ProjectJson.from_string(SAMPLE_UPROJECT)
    project.set("Description", "Updated")
    assert project.get("Description") == "Updated"


def test_set_new_key():
    project = ProjectJson.from_string(SAMPLE_UPROJECT)
    project.set("NewKey", "NewValue")
    assert project.get("NewKey") == "NewValue"


def test_write_roundtrip(tmp_path):
    project = ProjectJson.from_string(SAMPLE_UPROJECT)
    path = tmp_path / "Test.uproject"
    project.write(path)
    assert ProjectJson.from_file(path).get("EngineAssociation") == "5.5"


def test_from_file(tmp_path):
    path = tmp_path / "Test.uproject"
    path.write_text(SAMPLE_UPROJECT, encoding="utf-8")
    assert ProjectJson.from_file(path).get("Description") == "My Game"
