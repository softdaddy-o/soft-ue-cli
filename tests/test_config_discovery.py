"""Tests for config file hierarchy discovery."""

from __future__ import annotations

from pathlib import Path

import pytest


from soft_ue_cli.config.discovery import ConfigDiscovery


@pytest.fixture
def mock_project(tmp_path):
    engine = tmp_path / "Engine"
    project = tmp_path / "MyProject"

    (engine / "Config").mkdir(parents=True)
    (engine / "Config" / "Base.ini").write_text("[Core]\n", encoding="utf-8")
    (engine / "Config" / "BaseEngine.ini").write_text("[/Script/Engine.RendererSettings]\nr.Bloom=True\n", encoding="utf-8")
    (engine / "Config" / "BaseGame.ini").write_text("[/Script/Engine.GameSession]\nMaxPlayers=16\n", encoding="utf-8")

    (engine / "Config" / "Windows").mkdir()
    (engine / "Config" / "Windows" / "BaseWindowsEngine.ini").write_text("[WindowsSettings]\nfoo=bar\n", encoding="utf-8")

    (project / "Config").mkdir(parents=True)
    (project / "Config" / "DefaultEngine.ini").write_text("[/Script/Engine.RendererSettings]\nr.Bloom=False\n", encoding="utf-8")
    (project / "Config" / "DefaultGame.ini").write_text("[/Script/Engine.GameSession]\nMaxPlayers=32\n", encoding="utf-8")

    (project / "Config" / "Windows").mkdir()
    (project / "Config" / "Windows" / "WindowsEngine.ini").write_text("[W]\nx=1\n", encoding="utf-8")

    (project / "Saved" / "Config" / "Windows").mkdir(parents=True)
    (project / "Saved" / "Config" / "Windows" / "Engine.ini").write_text("[Saved]\ns=1\n", encoding="utf-8")

    (project / "MyProject.uproject").write_text('{"EngineAssociation":"5.5"}', encoding="utf-8")
    return engine, project


def test_discover_ini_layers(mock_project):
    engine, project = mock_project
    layers = ConfigDiscovery(engine_path=engine, project_path=project).ini_layers(config_type="Engine")
    names = [layer.name for layer in layers]
    assert "AbsoluteBase" in names
    assert "Base" in names
    assert "ProjectDefault" in names


def test_discover_existing_files(mock_project):
    engine, project = mock_project
    layers = ConfigDiscovery(engine_path=engine, project_path=project).ini_layers(config_type="Engine")
    assert len([layer for layer in layers if layer.exists]) >= 2


def test_discover_platforms(mock_project):
    engine, project = mock_project
    assert "Windows" in ConfigDiscovery(engine_path=engine, project_path=project).detect_platforms()


def test_discover_xml_layers(mock_project):
    engine, project = mock_project
    layers = ConfigDiscovery(engine_path=engine, project_path=project).xml_layers()
    assert len(layers) == 3
    assert [layer.name for layer in layers] == ["engine", "user-global", "project-local"]


def test_discover_project_files(mock_project):
    engine, project = mock_project
    files = ConfigDiscovery(engine_path=engine, project_path=project).project_json_files()
    assert any("MyProject.uproject" in str(file.path) for file in files)


def test_saved_layer(mock_project):
    engine, project = mock_project
    layers = ConfigDiscovery(engine_path=engine, project_path=project).ini_layers(config_type="Engine", platform="Windows")
    assert "Saved" in [layer.name for layer in layers]
