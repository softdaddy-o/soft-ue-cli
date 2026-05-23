"""Extract conservative metadata and tagged property payloads from External Actor package exports."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .package import UAssetPackage
from .properties import read_property_tag
from .reader import UAssetReader

_ACTOR_HINT_FIELDS = frozenset({
    "ActorLabel",
    "ActorGuid",
    "FolderPath",
    "RuntimeGrid",
    "DataLayerAssets",
    "Tags",
    "RootComponent",
    "bIsSpatiallyLoaded",
})

_GENERIC_TAGGED_PROPERTY_ASSET_CLASSES = frozenset({
    "Skeleton",
    "SkeletalMesh",
})


def extract_external_actor_summary(
    package: UAssetPackage,
    reader: UAssetReader,
    asset_path: str | Path,
    *,
    offset_adjust: int = 0,
) -> dict:
    """Return summary fields and tagged property payloads for External Actor-like packages."""
    actor = _find_actor_export(package, reader, offset_adjust=offset_adjust)
    if actor is None:
        return {}

    export, parsed = actor
    properties = parsed["by_name"]
    property_items = parsed["items"]
    property_fidelity = parsed["fidelity"]

    actor_class_path = package.resolve_class_path(export.class_index)
    actor_class = package.resolve_import_class(export.class_index)
    if actor_class.startswith("<") and actor_class_path and not actor_class_path.startswith("<"):
        actor_class = actor_class_path.rsplit(".", 1)[-1]

    outer_path = package.resolve_object_path(export.outer_index) if export.outer_index else ""
    actor_label = _string_or_empty(properties.get("ActorLabel"))

    summary = {
        "is_external_actor": True,
        "name": export.object_name,
        "asset_class": actor_class,
        "actor_label": actor_label,
        "actor_guid": _string_or_empty(properties.get("ActorGuid")),
        "actor_class_path": actor_class_path,
        "actor_outer_path": outer_path,
        "actor_folder_path": _string_or_empty(properties.get("FolderPath")),
        "actor_runtime_grid": _string_or_empty(properties.get("RuntimeGrid")),
        "actor_tags": _list_of_strings(properties.get("Tags")),
        "actor_data_layers": _list_of_strings(properties.get("DataLayerAssets")),
        "actor_spatially_loaded": _bool_or_none(properties.get("bIsSpatiallyLoaded")),
        "property_count": len(property_items),
        "properties": {
            "count": len(property_items),
            "items": property_items,
            "fidelity": property_fidelity,
        },
        "external_actor": {
            "export_name": export.object_name,
            "label": actor_label,
            "class": actor_class,
            "class_path": actor_class_path,
            "outer_path": outer_path,
            "guid": _string_or_empty(properties.get("ActorGuid")),
            "folder_path": _string_or_empty(properties.get("FolderPath")),
            "runtime_grid": _string_or_empty(properties.get("RuntimeGrid")),
            "tags": _list_of_strings(properties.get("Tags")),
            "data_layers": _list_of_strings(properties.get("DataLayerAssets")),
            "spatially_loaded": _bool_or_none(properties.get("bIsSpatiallyLoaded")),
            "source_hint": _external_actor_source_hint(asset_path),
            "property_count": len(property_items),
            "property_fidelity": property_fidelity,
            "fidelity": "partial",
        },
    }

    return summary


def extract_generic_asset_properties(
    package: UAssetPackage,
    reader: UAssetReader,
    *,
    offset_adjust: int = 0,
) -> dict | None:
    """Return tagged property payloads for supported non-Blueprint asset exports."""
    best: tuple[int, Any, dict[str, Any], str] | None = None

    for export in package.exports:
        payload_offset = export.serial_offset - offset_adjust
        if payload_offset < 0 or export.serial_size <= 0:
            continue

        class_name = package.resolve_import_class(export.class_index)
        if class_name.startswith("<"):
            class_path = package.resolve_class_path(export.class_index)
            class_name = class_path.rsplit(".", 1)[-1] if class_path and not class_path.startswith("<") else "Unknown"

        if class_name not in _GENERIC_TAGGED_PROPERTY_ASSET_CLASSES:
            continue

        parsed = _read_export_properties(
            package,
            reader,
            payload_offset,
            serial_size=export.serial_size,
        )
        property_items = parsed["items"]
        if not property_items:
            continue

        score = len(property_items)
        if any(item.get("name") in {"BoneTree", "VirtualBones", "Sockets", "Materials"} for item in property_items):
            score += 10

        candidate = (score, export, parsed, class_name)
        if best is None or candidate[0] > best[0]:
            best = candidate

    if best is None:
        return None

    _, export, parsed, class_name = best
    property_items = parsed["items"]
    return {
        "property_count": len(property_items),
        "properties": {
            "count": len(property_items),
            "items": property_items,
            "fidelity": parsed["fidelity"],
            "source_export": export.object_name,
            "source_class": class_name,
        },
    }


def _find_actor_export(
    package: UAssetPackage,
    reader: UAssetReader,
    *,
    offset_adjust: int,
):
    best: tuple[int, int, Any, dict[str, Any]] | None = None

    for export in package.exports:
        payload_offset = export.serial_offset - offset_adjust
        if payload_offset < 0:
            continue

        parsed = _read_export_properties(
            package,
            reader,
            payload_offset,
            serial_size=export.serial_size,
        )
        properties = parsed["by_name"]
        if not properties and not parsed["items"]:
            continue

        class_name = package.resolve_import_class(export.class_index)
        score = _score_actor_candidate(export.object_name, class_name, properties)
        if score <= 0:
            continue

        top_level_bonus = 1 if export.outer_index == 0 else 0
        candidate = (score, top_level_bonus, export, parsed)
        if best is None or candidate[:2] > best[:2]:
            best = candidate

    if best is None:
        return None
    return best[2], best[3]


def _score_actor_candidate(object_name: str, class_name: str, properties: dict[str, Any]) -> int:
    score = 0
    if "ActorLabel" in properties:
        score += 5
    if "ActorGuid" in properties:
        score += 4
    if "FolderPath" in properties:
        score += 3
    if "RuntimeGrid" in properties or "bIsSpatiallyLoaded" in properties:
        score += 2
    if "Tags" in properties or "DataLayerAssets" in properties:
        score += 1
    if "RootComponent" in properties:
        score += 1
    if "Actor" in class_name or object_name.startswith("BP_"):
        score += 1
    return score


def _read_export_properties(
    package: UAssetPackage,
    reader: UAssetReader,
    offset: int,
    *,
    serial_size: int | None = None,
) -> dict[str, Any]:
    items: list[dict[str, Any]] = []
    properties: dict[str, Any] = {}
    had_raw_values = False
    end_offset = offset + serial_size if serial_size is not None and serial_size > 0 else None

    try:
        reader.seek(offset)
        while end_offset is None or reader.tell() < end_offset:
            tag = read_property_tag(reader, package)
            if tag is None:
                break

            value_offset = reader.tell()
            try:
                value, value_fidelity = _read_property_value(reader, package, tag)
            except Exception:
                reader.seek(value_offset)
                value = _read_raw_value(reader, tag.size)
                value_fidelity = "raw"

            consumed = reader.tell() - value_offset
            if consumed < tag.size:
                reader.skip(tag.size - consumed)
            elif consumed > tag.size:
                reader.seek(value_offset + tag.size)

            item = _make_property_item(tag, value, value_fidelity)
            items.append(item)
            if value_fidelity != "parsed":
                had_raw_values = True
            if tag.name not in properties:
                properties[tag.name] = value
    except Exception:
        had_raw_values = True

    fidelity = "partial" if items and had_raw_values else ("exact" if items else "unavailable")
    return {
        "items": items,
        "by_name": properties,
        "fidelity": fidelity,
    }


def _make_property_item(tag, value: Any, fidelity: str) -> dict[str, Any]:
    item = {
        "name": _property_display_name(tag.name, tag.array_index),
        "type": tag.type,
        "array_index": tag.array_index,
        "value": value,
        "fidelity": fidelity,
    }
    if tag.array_index:
        item["property_name"] = tag.name
    if tag.struct_name and tag.struct_name != "None":
        item["struct"] = tag.struct_name
    if tag.enum_name and tag.enum_name != "None":
        item["enum"] = tag.enum_name
    if tag.inner_type and tag.inner_type != "None":
        item["inner_type"] = tag.inner_type
    if tag.value_type and tag.value_type != "None":
        item["value_type"] = tag.value_type
    return item


def _read_property_value(reader: UAssetReader, package: UAssetPackage, tag) -> tuple[Any, str]:
    prop_type = tag.type

    if prop_type == "BoolProperty":
        return bool(tag.value), "parsed"
    if prop_type in {"Int8Property", "Int16Property", "IntProperty", "Int64Property"}:
        return _read_sized_int(reader, tag.size, signed=True), "parsed"
    if prop_type in {"UInt16Property", "UInt32Property", "UInt64Property"}:
        return _read_sized_int(reader, tag.size, signed=False), "parsed"
    if prop_type == "FloatProperty":
        return reader.read_float(), "parsed"
    if prop_type == "DoubleProperty":
        return reader.read_double(), "parsed"
    if prop_type == "NameProperty":
        return package.resolve_name_ref(reader.read_int64()), "parsed"
    if prop_type == "StrProperty":
        return reader.read_fstring(), "parsed"
    if prop_type in {"ObjectProperty", "WeakObjectProperty", "LazyObjectProperty", "InterfaceProperty", "ClassProperty"}:
        return _read_object_ref(reader, package), "parsed"
    if prop_type in {"SoftObjectProperty", "SoftClassProperty"}:
        return _read_soft_object_path(reader, package, tag.size), "parsed"
    if prop_type in {"EnumProperty", "ByteProperty"}:
        return _read_enum_like_value(reader, package, tag), "parsed"
    if prop_type == "StructProperty":
        return _read_struct_value(reader, package, tag.struct_name, tag.size), "parsed"
    if prop_type in {"ArrayProperty", "SetProperty"}:
        return _read_array_value(reader, package, tag.inner_type, tag.size), "parsed"
    if prop_type == "MapProperty":
        return _read_map_value(reader, package, tag.inner_type, tag.value_type, tag.size), "parsed"

    return _read_raw_value(reader, tag.size), "raw"


def _read_array_value(reader: UAssetReader, package: UAssetPackage, inner_type: str, size: int) -> list[Any]:
    start = reader.tell()
    end = start + size
    count = reader.read_int32()
    values: list[Any] = []
    for _ in range(max(count, 0)):
        if reader.tell() >= end:
            break
        remaining = end - reader.tell()
        values.append(_read_element_value(reader, package, inner_type, remaining))
    reader.seek(end)
    return values


def _read_map_value(
    reader: UAssetReader,
    package: UAssetPackage,
    key_type: str,
    value_type: str,
    size: int,
) -> list[dict[str, Any]]:
    start = reader.tell()
    end = start + size
    count = reader.read_int32()
    items: list[dict[str, Any]] = []
    for _ in range(max(count, 0)):
        if reader.tell() >= end:
            break
        remaining = end - reader.tell()
        key = _read_element_value(reader, package, key_type, remaining)
        remaining = end - reader.tell()
        value = _read_element_value(reader, package, value_type, remaining)
        items.append({"key": key, "value": value})
    reader.seek(end)
    return items


def _read_element_value(reader: UAssetReader, package: UAssetPackage, prop_type: str, remaining: int) -> Any:
    if prop_type == "BoolProperty":
        return reader.read_bool()
    if prop_type == "Int8Property":
        return _read_sized_int(reader, 1, signed=True)
    if prop_type == "Int16Property":
        return _read_sized_int(reader, 2, signed=True)
    if prop_type == "IntProperty":
        return _read_sized_int(reader, 4, signed=True)
    if prop_type == "Int64Property":
        return _read_sized_int(reader, 8, signed=True)
    if prop_type == "UInt16Property":
        return _read_sized_int(reader, 2, signed=False)
    if prop_type == "UInt32Property":
        return _read_sized_int(reader, 4, signed=False)
    if prop_type == "UInt64Property":
        return _read_sized_int(reader, 8, signed=False)
    if prop_type == "FloatProperty":
        return reader.read_float()
    if prop_type == "DoubleProperty":
        return reader.read_double()
    if prop_type == "NameProperty":
        return package.resolve_name_ref(reader.read_int64())
    if prop_type == "StrProperty":
        return reader.read_fstring()
    if prop_type in {"ObjectProperty", "WeakObjectProperty", "LazyObjectProperty", "InterfaceProperty", "ClassProperty"}:
        return _read_object_ref(reader, package)
    if prop_type in {"SoftObjectProperty", "SoftClassProperty"}:
        return _read_soft_object_path(reader, package, remaining)
    if prop_type in {"EnumProperty", "ByteProperty"} and remaining >= 8:
        return package.resolve_name_ref(reader.read_int64())
    if prop_type == "StructProperty":
        raise ValueError("StructProperty array/set entries require element struct metadata")
    raise ValueError(f"Unsupported nested property type: {prop_type}")


def _read_struct_value(reader: UAssetReader, package: UAssetPackage, struct_name: str, size: int) -> Any:
    if struct_name == "Guid":
        return _read_guid(reader)
    if struct_name in {"Vector", "Vector3f", "Rotator", "Rotator3f"}:
        keys = ("x", "y", "z") if "Vector" in struct_name else ("pitch", "yaw", "roll")
        return _read_numeric_tuple(reader, size, keys)
    if struct_name in {"Vector2D", "Vector2f"}:
        return _read_numeric_tuple(reader, size, ("x", "y"))
    if struct_name == "Quat":
        return _read_numeric_tuple(reader, size, ("x", "y", "z", "w"))
    if struct_name == "LinearColor":
        return _read_numeric_tuple(reader, size, ("r", "g", "b", "a"))
    if struct_name == "Color":
        rgba = reader.read_bytes(min(size, 4))
        if len(rgba) == 4:
            return {"r": rgba[0], "g": rgba[1], "b": rgba[2], "a": rgba[3]}
        return {"raw_hex": rgba.hex(), "byte_size": len(rgba)}
    if struct_name == "Transform":
        return _read_transform(reader, size)
    if struct_name == "GameplayTag":
        return package.resolve_name_ref(reader.read_int64())
    if struct_name == "TopLevelAssetPath":
        return _read_top_level_asset_path(reader, package)
    if struct_name == "SoftObjectPath":
        return _read_soft_object_path(reader, package, size)
    if struct_name in {"DateTime", "Timespan"}:
        return _read_sized_int(reader, min(size, 8), signed=True)

    raise ValueError(f"Unsupported struct payload: {struct_name}")


def _read_enum_like_value(reader: UAssetReader, package: UAssetPackage, tag) -> Any:
    if tag.size >= 8 or (tag.enum_name and tag.enum_name != "None"):
        return package.resolve_name_ref(reader.read_int64())
    return _read_sized_int(reader, max(tag.size, 1), signed=False)


def _read_numeric_tuple(reader: UAssetReader, size: int, keys: tuple[str, ...]) -> dict[str, Any]:
    float_width = size // len(keys)
    if float_width == 4:
        values = [reader.read_float() for _ in keys]
    elif float_width == 8:
        values = [reader.read_double() for _ in keys]
    else:
        raise ValueError(f"Unsupported numeric tuple width: {size}")
    return dict(zip(keys, values, strict=True))


def _read_transform(reader: UAssetReader, size: int) -> Any:
    if size == 40:
        rotation = {"x": reader.read_float(), "y": reader.read_float(), "z": reader.read_float(), "w": reader.read_float()}
        translation = {"x": reader.read_float(), "y": reader.read_float(), "z": reader.read_float()}
        scale = {"x": reader.read_float(), "y": reader.read_float(), "z": reader.read_float()}
        return {"rotation": rotation, "translation": translation, "scale": scale}
    if size == 80:
        rotation = {"x": reader.read_double(), "y": reader.read_double(), "z": reader.read_double(), "w": reader.read_double()}
        translation = {"x": reader.read_double(), "y": reader.read_double(), "z": reader.read_double()}
        scale = {"x": reader.read_double(), "y": reader.read_double(), "z": reader.read_double()}
        return {"rotation": rotation, "translation": translation, "scale": scale}
    raise ValueError(f"Unsupported Transform size: {size}")


def _read_soft_object_path(reader: UAssetReader, package: UAssetPackage, size: int) -> str:
    start = reader.tell()
    package_name = package.resolve_name_ref(reader.read_int64())
    asset_name = package.resolve_name_ref(reader.read_int64())
    sub_path = ""
    if reader.tell() - start < size:
        sub_path = reader.read_fstring()

    path = ""
    if package_name and package_name != "None":
        path = package_name
    if asset_name and asset_name != "None":
        path = f"{path}.{asset_name}" if path else asset_name
    if sub_path:
        path = f"{path}:{sub_path}" if path else sub_path
    return path


def _read_top_level_asset_path(reader: UAssetReader, package: UAssetPackage) -> str:
    package_name = package.resolve_name_ref(reader.read_int64())
    asset_name = package.resolve_name_ref(reader.read_int64())
    if package_name and asset_name and package_name != "None" and asset_name != "None":
        return f"{package_name}.{asset_name}"
    return package_name if package_name != "None" else asset_name


def _read_object_ref(reader: UAssetReader, package: UAssetPackage) -> str:
    object_index = reader.read_int32()
    if object_index == 0:
        return ""
    return package.resolve_object_path(object_index)


def _read_raw_value(reader: UAssetReader, size: int) -> dict[str, Any]:
    raw = reader.read_bytes(size) if size > 0 else b""
    preview = raw[:64]
    payload = {
        "raw_hex": preview.hex(),
        "byte_size": len(raw),
    }
    if len(raw) > len(preview):
        payload["truncated"] = True
    return payload


def _read_guid(reader: UAssetReader) -> str:
    a, b, c, d = reader.read_fguid()
    raw = a.to_bytes(4, "little") + b.to_bytes(4, "little") + c.to_bytes(4, "little") + d.to_bytes(4, "little")
    hexed = raw.hex()
    return f"{hexed[0:8]}-{hexed[8:12]}-{hexed[12:16]}-{hexed[16:20]}-{hexed[20:32]}"


def _read_sized_int(reader: UAssetReader, size: int, *, signed: bool) -> int:
    width = max(size, 1)
    return int.from_bytes(reader.read_bytes(width), "little", signed=signed)


def _property_display_name(name: str, array_index: int) -> str:
    if array_index:
        return f"{name}[{array_index}]"
    return name


def _external_actor_source_hint(path: str | Path) -> str:
    asset_path = Path(path)
    parts = list(asset_path.parts)
    lowered = [part.lower() for part in parts]
    if "__externalactors__" not in lowered:
        return ""

    index = lowered.index("__externalactors__")
    relative = "/".join(parts[index:])
    return relative.replace("\\", "/")


def _bool_or_none(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    return None


def _list_of_strings(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if item not in {None, ""}]


def _string_or_empty(value: Any) -> str:
    if value is None:
        return ""
    return str(value)
