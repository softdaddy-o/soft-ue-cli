"""Tests for the inspect_uasset public API."""

from __future__ import annotations

from types import SimpleNamespace
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[1]))

import soft_ue_cli.uasset as uasset_mod
from soft_ue_cli.uasset import inspect_uasset
from soft_ue_cli.uasset.blueprint import extract_blueprint
from soft_ue_cli.uasset.package import UAssetPackage
from soft_ue_cli.uasset.types import ExportEntry, ImportEntry, NameEntry
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


def test_non_blueprint_file_uses_generic_summary(monkeypatch, tmp_path):
    asset = tmp_path / "not_blueprint.uasset"
    asset.write_bytes(b"placeholder")

    fake_package = SimpleNamespace(
        summary=SimpleNamespace(total_header_size=0, file_version_ue5=1007, file_version_ue4=0),
        names=[],
        imports=[],
        exports=[],
        resolve_import_class=lambda index: "<unresolved>",
    )
    monkeypatch.setattr(uasset_mod, "UAssetPackage", lambda stream: fake_package)
    monkeypatch.setattr(uasset_mod, "extract_blueprint", lambda package, reader, offset_adjust=0: (_ for _ in ()).throw(UAssetError("No Blueprint export found in this .uasset file")))

    result = uasset_mod.inspect_uasset(str(asset))
    assert result["asset_class"] == "Unknown"
    assert result["blueprint_type"] == "N/A"
    assert result["export_count"] == 0


def test_non_blueprint_properties_section_defaults_to_unavailable(monkeypatch, tmp_path):
    asset = tmp_path / "not_external_actor.uasset"
    asset.write_bytes(b"placeholder")

    fake_package = SimpleNamespace(
        summary=SimpleNamespace(total_header_size=0, file_version_ue5=1007, file_version_ue4=0),
        names=[],
        imports=[],
        exports=[],
        resolve_import_class=lambda index: "<unresolved>",
    )
    monkeypatch.setattr(uasset_mod, "UAssetPackage", lambda stream: fake_package)
    monkeypatch.setattr(
        uasset_mod,
        "extract_blueprint",
        lambda package, reader, offset_adjust=0: (_ for _ in ()).throw(UAssetError("No Blueprint export found in this .uasset file")),
    )

    result = uasset_mod.inspect_uasset(str(asset), sections=["summary", "properties"])
    assert result["property_count"] == 0
    assert result["properties"] == {"count": 0, "items": [], "fidelity": "unavailable"}


def test_skeleton_properties_section_reads_tagged_export_properties(monkeypatch, tmp_path):
    names = [
        "None",
        "BoneTree",
        "ArrayProperty",
        "StructProperty",
        "VirtualBones",
        "Sockets",
        "CompatibleSkeletons",
        "ObjectProperty",
    ]
    payload = b"".join(
        [
            _array_property(1, 2, 3, b"bone-payload"),
            _array_property(4, 2, 3, b"virtual-bone-payload"),
            _array_property(5, 2, 7, b"socket-payload"),
            _none_tag(),
        ]
    )
    asset = tmp_path / "SK_Test_Skeleton.uasset"
    asset.write_bytes(payload)

    fake_package = object.__new__(UAssetPackage)
    fake_package.summary = SimpleNamespace(total_header_size=0, file_version_ue5=1007, file_version_ue4=0)
    fake_package.names = [NameEntry(name) for name in names]
    fake_package.imports = [
        ImportEntry(class_package="CoreUObject", class_name="Package", outer_index=0, object_name="Engine"),
        ImportEntry(class_package="CoreUObject", class_name="Class", outer_index=-1, object_name="Skeleton"),
    ]
    fake_package.exports = [
        ExportEntry(
            class_index=-2,
            super_index=0,
            template_index=0,
            outer_index=0,
            object_name="SK_Test_Skeleton",
            serial_offset=0,
            serial_size=len(payload),
        )
    ]

    monkeypatch.setattr(uasset_mod, "UAssetPackage", lambda stream: fake_package)
    monkeypatch.setattr(
        uasset_mod,
        "extract_blueprint",
        lambda package, reader, offset_adjust=0: (_ for _ in ()).throw(UAssetError("No Blueprint export found")),
    )

    result = uasset_mod.inspect_uasset(str(asset), sections=["summary", "properties"])

    assert result["asset_class"] == "Skeleton"
    assert result["property_count"] == 3
    assert result["properties"]["fidelity"] == "partial"
    assert [item["name"] for item in result["properties"]["items"]] == ["BoneTree", "VirtualBones", "Sockets"]
    assert result["properties"]["items"][0]["inner_type"] == "StructProperty"


def test_diff_uasset_skeleton_properties_reports_property_changes(monkeypatch):
    left = {
        "file": "left.uasset",
        "name": "SK_Test_Skeleton",
        "ue_version": "5.4",
        "asset_class": "Skeleton",
        "parent_class": "N/A",
        "parent_class_path": "",
        "blueprint_type": "N/A",
        "properties": {
            "count": 1,
            "items": [{"name": "BoneTree", "type": "ArrayProperty", "value": {"raw_hex": "01"}, "fidelity": "raw"}],
            "fidelity": "partial",
        },
    }
    right = {
        **left,
        "file": "right.uasset",
        "properties": {
            "count": 2,
            "items": [
                {"name": "BoneTree", "type": "ArrayProperty", "value": {"raw_hex": "02"}, "fidelity": "raw"},
                {"name": "VirtualBones", "type": "ArrayProperty", "value": {"raw_hex": "03"}, "fidelity": "raw"},
            ],
            "fidelity": "partial",
        },
    }

    monkeypatch.setattr(uasset_mod, "inspect_uasset", lambda path, sections: left if "left" in str(path) else right)

    result = uasset_mod.diff_uasset("left.uasset", "right.uasset", sections=["properties"])

    assert result["changes"]["properties"]["change_count"] == 2
    assert result["changes"]["properties"]["added"][0]["name"] == "VirtualBones"
    assert result["changes"]["properties"]["modified"][0]["name"] == "BoneTree"


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


def _none_tag() -> bytes:
    return struct.pack("<q", 0)


def _tag_header(name_index: int, type_index: int, size: int, extra: bytes = b"") -> bytes:
    return struct.pack("<qqii", name_index, type_index, size, 0) + extra + b"\x00"


def _array_property(name_index: int, type_index: int, inner_index: int, raw_payload: bytes) -> bytes:
    payload = struct.pack("<i", 1) + raw_payload
    extra = struct.pack("<q", inner_index)
    return _tag_header(name_index, type_index, len(payload), extra=extra) + payload
