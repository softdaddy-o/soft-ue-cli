"""Tests for offline .uasset tagged property parsing."""

from __future__ import annotations

import io
import struct

from soft_ue_cli.uasset.properties import read_property_tag, read_tagged_properties
from soft_ue_cli.uasset.reader import UAssetReader


def _fname_ref(index: int, number: int = 0) -> int:
    return (number << 32) | index


class _FakeNameResolver:
    def __init__(self, names: list[str]) -> None:
        self.names = names

    def get_name(self, index: int) -> str:
        if 0 <= index < len(self.names):
            return self.names[index]
        return f"<invalid_name_{index}>"

    def resolve_name_ref(self, value: int) -> str:
        index = value & 0xFFFFFFFF
        if 0 <= index < len(self.names):
            return self.names[index]
        return f"<invalid_name_{value}>"


def test_read_property_tag_uses_resolved_name_ref_for_combined_fname():
    resolver = _FakeNameResolver([
        "None",
        "ArrayProperty",
        "DataLayerAssets",
        "SoftObjectProperty",
    ])
    payload = b"".join([
        struct.pack("<q", _fname_ref(2, 17)),
        struct.pack("<q", _fname_ref(1, 9)),
        struct.pack("<i", 4),
        struct.pack("<i", 0),
        struct.pack("<q", _fname_ref(3, 4)),
        b"\x00",
    ])

    tag = read_property_tag(UAssetReader(io.BytesIO(payload)), resolver)

    assert tag is not None
    assert tag.name == "DataLayerAssets"
    assert tag.type == "ArrayProperty"
    assert tag.inner_type == "SoftObjectProperty"


def test_read_tagged_properties_stops_on_none_with_combined_fname():
    resolver = _FakeNameResolver([
        "None",
        "NameProperty",
        "ActorLabel",
    ])
    payload = b"".join([
        struct.pack("<q", _fname_ref(2, 3)),
        struct.pack("<q", _fname_ref(1, 5)),
        struct.pack("<i", 8),
        struct.pack("<i", 0),
        b"\x00",
        struct.pack("<q", _fname_ref(2, 1)),
        struct.pack("<q", _fname_ref(0, 11)),
    ])

    props = read_tagged_properties(UAssetReader(io.BytesIO(payload)), resolver)

    assert [prop.name for prop in props] == ["ActorLabel"]
    assert props[0].type == "NameProperty"
