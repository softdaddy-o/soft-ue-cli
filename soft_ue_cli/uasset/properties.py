"""Tagged property helpers for conservative UObject parsing."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .reader import UAssetReader


@dataclass(slots=True)
class PropertyTag:
    name: str
    type: str
    size: int
    value: Any = None
    struct_name: str = ""
    enum_name: str = ""
    inner_type: str = ""
    value_type: str = ""
    array_index: int = 0
    has_property_guid: bool = False


def read_property_tag(reader: UAssetReader, name_resolver: Any) -> PropertyTag | None:
    """Read a single property tag header."""
    name_ref = reader.read_int64()
    name = name_resolver.get_name(int(name_ref))
    if name == "None":
        return None

    type_ref = reader.read_int64()
    prop_type = name_resolver.get_name(int(type_ref))
    size = reader.read_int32()
    array_index = reader.read_int32()
    tag = PropertyTag(name=name, type=prop_type, size=size, array_index=array_index)

    if prop_type == "StructProperty":
        struct_ref = reader.read_int64()
        tag.struct_name = name_resolver.get_name(int(struct_ref))
        reader.read_fguid()
    elif prop_type in {"EnumProperty", "ByteProperty"}:
        enum_ref = reader.read_int64()
        tag.enum_name = name_resolver.get_name(int(enum_ref))
    elif prop_type in {"ArrayProperty", "SetProperty"}:
        inner_ref = reader.read_int64()
        tag.inner_type = name_resolver.get_name(int(inner_ref))
    elif prop_type == "MapProperty":
        inner_ref = reader.read_int64()
        value_ref = reader.read_int64()
        tag.inner_type = name_resolver.get_name(int(inner_ref))
        tag.value_type = name_resolver.get_name(int(value_ref))

    tag.has_property_guid = reader.read_bool()
    if tag.has_property_guid:
        reader.read_fguid()

    return tag


def skip_property_value(reader: UAssetReader, tag: PropertyTag) -> None:
    if tag.type == "BoolProperty":
        return
    reader.skip(tag.size)


def read_tagged_properties(reader: UAssetReader, name_resolver: Any) -> list[PropertyTag]:
    props: list[PropertyTag] = []
    while True:
        tag = read_property_tag(reader, name_resolver)
        if tag is None:
            break
        props.append(tag)
        skip_property_value(reader, tag)
    return props
