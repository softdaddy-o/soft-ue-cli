"""Tests for External Actor offline inspection."""

from __future__ import annotations

from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).parents[1]))

from soft_ue_cli.uasset.external_actor import extract_external_actor_summary
from soft_ue_cli.uasset.package import UAssetPackage
from soft_ue_cli.uasset.reader import UAssetReader
from soft_ue_cli.uasset.types import ExportEntry, ImportEntry, NameEntry


def test_extract_external_actor_summary_reads_actor_metadata():
    package = object.__new__(UAssetPackage)
    package.names = [
        NameEntry("None"),
        NameEntry("ActorLabel"),
        NameEntry("StrProperty"),
        NameEntry("ActorGuid"),
        NameEntry("StructProperty"),
        NameEntry("Guid"),
        NameEntry("FolderPath"),
        NameEntry("NameProperty"),
        NameEntry("Tags"),
        NameEntry("ArrayProperty"),
        NameEntry("Gameplay"),
        NameEntry("Foliage"),
        NameEntry("RootComponent"),
        NameEntry("ObjectProperty"),
        NameEntry("RuntimeGrid"),
        NameEntry("MainGrid"),
        NameEntry("DataLayerAssets"),
        NameEntry("bIsSpatiallyLoaded"),
        NameEntry("BoolProperty"),
        NameEntry("Environment/Trees"),
        NameEntry("StaticMeshComponent0"),
        NameEntry("CustomDensity"),
        NameEntry("FloatProperty"),
        NameEntry("ActorScale3D"),
        NameEntry("Vector"),
    ]
    package.imports = [
        ImportEntry(class_package="CoreUObject", class_name="Package", outer_index=0, object_name="Engine"),
        ImportEntry(class_package="CoreUObject", class_name="Class", outer_index=-1, object_name="StaticMeshActor"),
        ImportEntry(class_package="CoreUObject", class_name="Class", outer_index=-1, object_name="StaticMeshComponent"),
        ImportEntry(class_package="CoreUObject", class_name="Package", outer_index=0, object_name="/Game/Maps/OpenWorld"),
        ImportEntry(class_package="Engine", class_name="Level", outer_index=-4, object_name="PersistentLevel"),
        ImportEntry(class_package="CoreUObject", class_name="Package", outer_index=0, object_name="/Game/DataLayers/Forest"),
        ImportEntry(class_package="CoreUObject", class_name="Object", outer_index=-6, object_name="Forest"),
    ]

    payload = b"".join(
        [
            _str_property(1, 2, "Pine_01"),
            _guid_property(3, 4, 5, bytes.fromhex("00112233445566778899aabbccddeeff")),
            _name_property(6, 7, 19),
            _name_array_property(8, 9, 7, [10, 11]),
            _object_property(12, 13, 2),
            _name_property(14, 7, 15),
            _object_array_property(16, 9, 13, [-7]),
            _bool_property(17, 18, True),
            _float_property(21, 22, 42.5),
            _vector_property(23, 4, 24, (1.0, 2.0, 3.0)),
            _none_tag(),
        ]
    )

    package.exports = [
        ExportEntry(
            class_index=-2,
            super_index=0,
            template_index=0,
            outer_index=-5,
            object_name="SM_Tree_12",
            serial_offset=0,
            serial_size=len(payload),
        ),
        ExportEntry(
            class_index=-3,
            super_index=0,
            template_index=0,
            outer_index=1,
            object_name="StaticMeshComponent0",
            serial_offset=len(payload),
            serial_size=0,
        ),
    ]

    reader = UAssetReader(_BytesIO(payload))

    result = extract_external_actor_summary(
        package,
        reader,
        "D:/Project/Content/__ExternalActors__/Maps/OpenWorld/5/TQ/ABCDEFG.uasset",
    )

    assert result["is_external_actor"] is True
    assert result["name"] == "SM_Tree_12"
    assert result["asset_class"] == "StaticMeshActor"
    assert result["actor_label"] == "Pine_01"
    assert result["actor_guid"] == "00112233-4455-6677-8899-aabbccddeeff"
    assert result["actor_class_path"] == "/Script/Engine.StaticMeshActor"
    assert result["actor_outer_path"] == "/Game/Maps/OpenWorld.PersistentLevel"
    assert result["actor_folder_path"] == "Environment/Trees"
    assert result["actor_runtime_grid"] == "MainGrid"
    assert result["actor_tags"] == ["Gameplay", "Foliage"]
    assert result["actor_data_layers"] == ["/Game/DataLayers/Forest.Forest"]
    assert result["actor_spatially_loaded"] is True
    assert result["property_count"] == 10
    assert result["properties"]["count"] == 10
    assert result["properties"]["fidelity"] == "exact"
    density = next(item for item in result["properties"]["items"] if item["name"] == "CustomDensity")
    scale = next(item for item in result["properties"]["items"] if item["name"] == "ActorScale3D")
    assert density["value"] == 42.5
    assert scale["value"] == {"x": 1.0, "y": 2.0, "z": 3.0}
    assert result["external_actor"]["source_hint"].startswith("__ExternalActors__/Maps/OpenWorld/")


class _BytesIO:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._offset = 0

    def seek(self, offset: int, whence: int = 0) -> int:
        if whence == 0:
            self._offset = offset
        elif whence == 1:
            self._offset += offset
        elif whence == 2:
            self._offset = len(self._data) + offset
        return self._offset

    def tell(self) -> int:
        return self._offset

    def read(self, count: int = -1) -> bytes:
        if count < 0:
            count = len(self._data) - self._offset
        start = self._offset
        end = min(len(self._data), start + count)
        self._offset = end
        return self._data[start:end]


def _none_tag() -> bytes:
    return struct.pack("<q", 0)


def _tag_header(name_index: int, type_index: int, size: int, extra: bytes = b"") -> bytes:
    return struct.pack("<qqii", name_index, type_index, size, 0) + extra + b"\x00"


def _str_property(name_index: int, type_index: int, value: str) -> bytes:
    encoded = value.encode("utf-8") + b"\x00"
    payload = struct.pack("<i", len(encoded)) + encoded
    return _tag_header(name_index, type_index, len(payload)) + payload


def _guid_property(name_index: int, type_index: int, struct_index: int, guid: bytes) -> bytes:
    extra = struct.pack("<q", struct_index) + (b"\x00" * 16)
    return _tag_header(name_index, type_index, len(guid), extra=extra) + guid


def _name_property(name_index: int, type_index: int, value_index: int) -> bytes:
    return _tag_header(name_index, type_index, 8) + struct.pack("<q", value_index)


def _name_array_property(name_index: int, type_index: int, inner_index: int, values: list[int]) -> bytes:
    payload = struct.pack("<i", len(values)) + b"".join(struct.pack("<q", value) for value in values)
    extra = struct.pack("<q", inner_index)
    return _tag_header(name_index, type_index, len(payload), extra=extra) + payload


def _object_property(name_index: int, type_index: int, object_index: int) -> bytes:
    return _tag_header(name_index, type_index, 4) + struct.pack("<i", object_index)


def _object_array_property(name_index: int, type_index: int, inner_index: int, values: list[int]) -> bytes:
    payload = struct.pack("<i", len(values)) + b"".join(struct.pack("<i", value) for value in values)
    extra = struct.pack("<q", inner_index)
    return _tag_header(name_index, type_index, len(payload), extra=extra) + payload


def _bool_property(name_index: int, type_index: int, value: bool) -> bytes:
    return struct.pack("<qqii", name_index, type_index, 0, 0) + (b"\x01" if value else b"\x00") + b"\x00"


def _float_property(name_index: int, type_index: int, value: float) -> bytes:
    payload = struct.pack("<f", value)
    return _tag_header(name_index, type_index, len(payload)) + payload


def _vector_property(name_index: int, type_index: int, struct_index: int, values: tuple[float, float, float]) -> bytes:
    payload = struct.pack("<fff", *values)
    extra = struct.pack("<q", struct_index) + (b"\x00" * 16)
    return _tag_header(name_index, type_index, len(payload), extra=extra) + payload
