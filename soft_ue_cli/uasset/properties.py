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
    name = _resolve_name_ref(name_resolver, int(name_ref))
    if name == "None":
        return None

    type_ref = reader.read_int64()
    prop_type = _resolve_name_ref(name_resolver, int(type_ref))
    size = reader.read_int32()
    array_index = reader.read_int32()
    tag = PropertyTag(name=name, type=prop_type, size=size, array_index=array_index)

    if prop_type == "StructProperty":
        struct_ref = reader.read_int64()
        tag.struct_name = _resolve_name_ref(name_resolver, int(struct_ref))
        reader.read_fguid()
    elif prop_type in {"EnumProperty", "ByteProperty"}:
        enum_ref = reader.read_int64()
        tag.enum_name = _resolve_name_ref(name_resolver, int(enum_ref))
    elif prop_type in {"ArrayProperty", "SetProperty"}:
        inner_ref = reader.read_int64()
        tag.inner_type = _resolve_name_ref(name_resolver, int(inner_ref))
    elif prop_type == "MapProperty":
        inner_ref = reader.read_int64()
        value_ref = reader.read_int64()
        tag.inner_type = _resolve_name_ref(name_resolver, int(inner_ref))
        tag.value_type = _resolve_name_ref(name_resolver, int(value_ref))
    elif prop_type == "BoolProperty":
        tag.value = reader.read_bool()

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


def _resolve_name_ref(name_resolver: Any, value: int) -> str:
    if hasattr(name_resolver, "resolve_name_ref"):
        return name_resolver.resolve_name_ref(value)

    if hasattr(name_resolver, "get_name"):
        name_count = len(getattr(name_resolver, "names", []))
        low = value & 0xFFFFFFFF
        high = (value >> 32) & 0xFFFFFFFF
        if 0 <= value < name_count:
            return name_resolver.get_name(value)
        if 0 <= low < name_count:
            return name_resolver.get_name(int(low))
        if 0 <= high < name_count:
            return name_resolver.get_name(int(high))
        return name_resolver.get_name(value)

    if isinstance(name_resolver, (list, tuple)):
        low = value & 0xFFFFFFFF
        high = (value >> 32) & 0xFFFFFFFF
        if 0 <= value < len(name_resolver):
            return str(name_resolver[value])
        if 0 <= low < len(name_resolver):
            return str(name_resolver[int(low)])
        if 0 <= high < len(name_resolver):
            return str(name_resolver[int(high)])

    return f"<invalid_name_{value}>"
