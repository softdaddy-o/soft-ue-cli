"""Integration tests for the config pipeline (offline, no bridge)."""

from __future__ import annotations

import json
from pathlib import Path

import pytest


from soft_ue_cli.config import (
    BuildConfigXml,
    ConfigDiscovery,
    ProjectJson,
    UeIniFile,
    diff_ini_files,
    merge_ini_layers,
    parse_ini_key,
    trace_key,
)


@pytest.fixture
def ue_project(tmp_path):
    engine = tmp_path / "Engine"
    project = tmp_path / "MyProject"

    (engine / "Config").mkdir(parents=True)
    (engine / "Config" / "Base.ini").write_text("", encoding="utf-8")
    (engine / "Config" / "BaseEngine.ini").write_text(
        "[/Script/Engine.RendererSettings]\n"
        "r.DefaultFeature.AutoExposure=True\n"
        "r.DefaultFeature.Bloom=True\n"
        "+r.SupportedShaderFormats=PCD3D_SM5\n"
        "+r.SupportedShaderFormats=PCD3D_SM6\n",
        encoding="utf-8",
    )

    (project / "Config").mkdir(parents=True)
    (project / "Config" / "DefaultEngine.ini").write_text(
        "[/Script/Engine.RendererSettings]\n"
        "r.DefaultFeature.AutoExposure=False\n"
        "-r.SupportedShaderFormats=PCD3D_SM5\n",
        encoding="utf-8",
    )

    (project / "MyProject.uproject").write_text(
        json.dumps({"FileVersion": 3, "EngineAssociation": "5.5"}),
        encoding="utf-8",
    )

    (project / "Saved" / "Config" / "UnrealBuildTool").mkdir(parents=True)
    (project / "Saved" / "Config" / "UnrealBuildTool" / "BuildConfiguration.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">\n'
        "  <BuildConfiguration>\n"
        "    <MaxParallelActions>16</MaxParallelActions>\n"
        "  </BuildConfiguration>\n"
        "</Configuration>\n",
        encoding="utf-8",
    )

    return engine, project


def test_full_merge_pipeline(ue_project):
    engine, project = ue_project
    discovery = ConfigDiscovery(engine_path=engine, project_path=project)
    layers = discovery.ini_layers(config_type="Engine")
    merged = merge_ini_layers([UeIniFile.from_file(layer.path) for layer in layers if layer.exists])
    assert merged.get("/Script/Engine.RendererSettings", "r.DefaultFeature.AutoExposure") == "False"
    assert merged.get("/Script/Engine.RendererSettings", "r.DefaultFeature.Bloom") == "True"
    assert "PCD3D_SM6" in merged.get_array("/Script/Engine.RendererSettings", "r.SupportedShaderFormats")
    assert "PCD3D_SM5" not in merged.get_array("/Script/Engine.RendererSettings", "r.SupportedShaderFormats")


def test_trace_across_layers(ue_project):
    engine, project = ue_project
    discovery = ConfigDiscovery(engine_path=engine, project_path=project)
    loaded: list[UeIniFile] = []
    for layer in discovery.ini_layers(config_type="Engine"):
        if not layer.exists:
            continue
        ini = UeIniFile.from_file(layer.path)
        ini.path = layer.path
        loaded.append(ini)
    trace = trace_key(loaded, "/Script/Engine.RendererSettings", "r.DefaultFeature.AutoExposure")
    assert len(trace) == 2
    assert trace[0]["value"] == "True"
    assert trace[1]["value"] == "False"


def test_diff_engine_vs_project(ue_project):
    engine, project = ue_project
    result = diff_ini_files(
        UeIniFile.from_file(engine / "Config" / "BaseEngine.ini"),
        UeIniFile.from_file(project / "Config" / "DefaultEngine.ini"),
    )
    assert result["total_changes"] > 0


def test_project_json_read(ue_project):
    _, project = ue_project
    assert ProjectJson.from_file(project / "MyProject.uproject").get("EngineAssociation") == "5.5"


def test_xml_read(ue_project):
    _, project = ue_project
    assert (
        BuildConfigXml.from_file(project / "Saved" / "Config" / "UnrealBuildTool" / "BuildConfiguration.xml")
        .get("BuildConfiguration", "MaxParallelActions")
        == "16"
    )


def test_parse_ini_key_roundtrip():
    section, key = parse_ini_key("[/Script/Engine.RendererSettings]r.DefaultFeature.AutoExposure")
    assert section == "/Script/Engine.RendererSettings"
    assert key == "r.DefaultFeature.AutoExposure"
