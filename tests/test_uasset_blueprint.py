"""Tests for the inspect_uasset public API."""

from __future__ import annotations

from types import SimpleNamespace
from pathlib import Path

import pytest


import soft_ue_cli.uasset as uasset_mod
from soft_ue_cli.uasset import inspect_uasset
from soft_ue_cli.uasset.blueprint import extract_blueprint
from soft_ue_cli.uasset.package import UAssetPackage
from soft_ue_cli.uasset.types import ExportEntry, ImportEntry
from soft_ue_cli.uasset.types import UAssetError


def test_missing_file_raises():
    with pytest.raises(FileNotFoundError):
        inspect_uasset("/nonexistent/path.uasset")


def test_invalid_file_raises(tmp_path):
    bad = tmp_path / "bad.uasset"
    bad.write_bytes(b"not a uasset file at all")
    with pytest.raises(UAssetError, match="magic"):
        inspect_uasset(str(bad))


def test_empty_file_raises(tmp_path):
    empty = tmp_path / "empty.uasset"
    empty.write_bytes(b"")
    with pytest.raises(UAssetError):
        inspect_uasset(str(empty))


def test_non_blueprint_file_raises(monkeypatch, tmp_path):
    asset = tmp_path / "not_blueprint.uasset"
    asset.write_bytes(b"placeholder")

    fake_package = SimpleNamespace(summary=SimpleNamespace(total_header_size=0, file_version_ue5=1007, file_version_ue4=0))
    monkeypatch.setattr(uasset_mod, "UAssetPackage", lambda stream: fake_package)
    monkeypatch.setattr(uasset_mod, "extract_blueprint", lambda package, reader, offset_adjust=0: (_ for _ in ()).throw(UAssetError("No Blueprint export found in this .uasset file")))

    with pytest.raises(UAssetError, match="No Blueprint export found"):
        uasset_mod.inspect_uasset(str(asset))


def test_parent_class_path_uses_outer_package():
    package = object.__new__(UAssetPackage)
    package.imports = [
        ImportEntry(class_package="CoreUObject", class_name="Package", outer_index=0, object_name="Engine"),
        ImportEntry(class_package="CoreUObject", class_name="Class", outer_index=-1, object_name="Actor"),
    ]
    package.exports = []

    assert package.resolve_import_object_path(-2) == "/Script/Engine.Actor"


def test_extract_blueprint_uses_parsed_blueprint_type(monkeypatch):
    package = SimpleNamespace(
        exports=[ExportEntry(class_index=-1, super_index=0, outer_index=0, object_name="BP_Test", serial_offset=128, serial_size=32)],
        resolve_import_class=lambda index: "Blueprint",
        resolve_object_name=lambda index: "Actor",
        resolve_import_object_path=lambda index: "/Script/Engine.Actor",
    )
    reader = SimpleNamespace()

    monkeypatch.setattr(
        "soft_ue_cli.uasset.blueprint._read_blueprint_summary_properties",
        lambda package, reader, offset: {"parent_class_index": -2, "blueprint_type": 3},
    )

    result = extract_blueprint(package, reader)
    assert result["parent_class_path"] == "/Script/Engine.Actor"
    assert result["blueprint_type"] == "Interface"


def test_summary_fixture_placeholder_skipped():
    pytest.skip("Requires a real Blueprint fixture in cli/tests/fixtures/")
