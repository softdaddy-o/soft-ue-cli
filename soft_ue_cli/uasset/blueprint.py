"""Extract conservative Blueprint metadata from package tables."""

from __future__ import annotations

from .package import UAssetPackage
from .properties import read_property_tag
from .reader import UAssetReader
from .types import BLUEPRINT_TYPE_NAMES, UAssetError

_BLUEPRINT_EXPORT_CLASSES = {"Blueprint", "WidgetBlueprint", "AnimBlueprint"}
_PROPERTY_TYPE_MAP = {
    "BoolProperty": "bool",
    "ByteProperty": "byte",
    "ClassProperty": "class",
    "DoubleProperty": "double",
    "EnumProperty": "enum",
    "FloatProperty": "float",
    "IntProperty": "int",
    "Int64Property": "int64",
    "MapProperty": "map",
    "NameProperty": "name",
    "ObjectProperty": "object",
    "SetProperty": "set",
    "SoftClassProperty": "soft_class",
    "SoftObjectProperty": "soft_object",
    "StrProperty": "string",
    "StructProperty": "struct",
    "TextProperty": "text",
}


def extract_blueprint(
    package: UAssetPackage,
    reader: UAssetReader,
    *,
    offset_adjust: int = 0,
) -> dict:
    """Return summary metadata for the first Blueprint-like export in the package."""
    bp_export, bp_class_name = _find_blueprint_export(package)
    if bp_export is None:
        raise UAssetError("No Blueprint export found in this .uasset file")

    summary_props = _read_blueprint_summary_properties(package, reader, bp_export.serial_offset - offset_adjust)
    parent_index = summary_props.get("parent_class_index")
    if parent_index is None:
        parent_index = _guess_parent_class_index(package)

    parent_class = "Unknown"
    parent_class_path = ""
    if isinstance(parent_index, int) and parent_index < 0:
        parent_class = package.resolve_object_name(parent_index)
        parent_class_path = package.resolve_import_object_path(parent_index)

    return {
        "name": bp_export.object_name,
        "asset_class": bp_class_name,
        "parent_class": parent_class,
        "parent_class_path": parent_class_path,
        "blueprint_type": BLUEPRINT_TYPE_NAMES.get(summary_props.get("blueprint_type", 0), "Normal"),
    }


def extract_variables(
    package: UAssetPackage,
    reader: UAssetReader,
    *,
    offset_adjust: int = 0,
) -> dict:
    """Return a partial variables section using exported property objects."""
    del reader
    del offset_adjust

    try:
        items = []
        seen: set[str] = set()
        for export in package.exports:
            class_name = package.resolve_import_class(export.class_index)
            if not class_name.endswith("Property"):
                continue
            if export.object_name in {"UberGraphFrame", "None"}:
                continue
            if export.object_name in seen:
                continue
            seen.add(export.object_name)

            item = {
                "name": export.object_name,
                "type": _PROPERTY_TYPE_MAP.get(class_name, class_name.removesuffix("Property").lower()),
                "is_array": class_name == "ArrayProperty",
                "is_set": class_name == "SetProperty",
                "is_map": class_name == "MapProperty",
                "replication": "Unknown",
                "flags": [],
            }
            items.append(item)

        fidelity = "partial" if items else "unavailable"
        result: dict = {"count": len(items), "items": items, "fidelity": fidelity}
        if not items:
            result["error"] = "No exported Blueprint property objects were found"
        return result
    except Exception as exc:
        return {"count": 0, "items": [], "fidelity": "unavailable", "error": str(exc)}


def extract_functions(package: UAssetPackage) -> dict:
    """Return function-like exports from the package table."""
    try:
        items = []
        seen: set[str] = set()
        for export in package.exports:
            class_name = package.resolve_import_class(export.class_index)
            if not class_name.endswith("Function"):
                continue
            if export.object_name.startswith("ExecuteUbergraph_"):
                continue
            if export.object_name in seen:
                continue
            seen.add(export.object_name)
            items.append({"name": export.object_name, "flags": [], "parameters": []})

        fidelity = "partial" if items else "unavailable"
        result: dict = {"count": len(items), "items": items, "fidelity": fidelity}
        if not items:
            result["error"] = "No exported Blueprint functions were found"
        return result
    except Exception as exc:
        return {"count": 0, "items": [], "fidelity": "unavailable", "error": str(exc)}


def extract_components(
    package: UAssetPackage,
    reader: UAssetReader,
    *,
    offset_adjust: int = 0,
) -> dict:
    """Return component-like exports from the package table."""
    del reader
    del offset_adjust

    try:
        items = []
        seen: set[tuple[str, str]] = set()
        for export in package.exports:
            class_name = package.resolve_import_class(export.class_index)
            if not class_name.endswith("Component"):
                continue
            if export.object_name.startswith("Default__"):
                continue
            key = (export.object_name, class_name)
            if key in seen:
                continue
            seen.add(key)

            clean_name = export.object_name.removesuffix("_GEN_VARIABLE")
            items.append(
                {
                    "name": clean_name,
                    "class": class_name,
                    "is_root": clean_name in {"DefaultSceneRoot", "SceneRoot"},
                }
            )

        fidelity = "partial" if items else "unavailable"
        result: dict = {"count": len(items), "items": items, "fidelity": fidelity}
        if not items:
            result["error"] = "No exported Blueprint components were found"
        return result
    except Exception as exc:
        return {"count": 0, "items": [], "fidelity": "unavailable", "error": str(exc)}


def extract_events(package: UAssetPackage) -> dict:
    """Return a partial event summary derived from exported functions."""
    try:
        functions = extract_functions(package).get("items", [])
        events = []
        custom_events = []
        for function in functions:
            name = function["name"]
            if name.startswith(("Receive", "InpActionEvt_", "InpAxisEvt_", "BndEvt__")):
                events.append({"name": name, "node_title": name})
            elif name.startswith(("CustomEvent", "CE_", "Event_")):
                custom_events.append({"name": name, "parameters": []})

        fidelity = "partial" if events or custom_events else "unavailable"
        result = {
            "events": events,
            "custom_events": custom_events,
            "event_count": len(events),
            "custom_event_count": len(custom_events),
            "fidelity": fidelity,
        }
        if not events and not custom_events:
            result["error"] = "No event-like Blueprint functions were found"
        return result
    except Exception as exc:
        return {
            "events": [],
            "custom_events": [],
            "event_count": 0,
            "custom_event_count": 0,
            "fidelity": "unavailable",
            "error": str(exc),
        }


def _find_blueprint_export(package: UAssetPackage):
    for export in package.exports:
        class_name = package.resolve_import_class(export.class_index)
        if class_name in _BLUEPRINT_EXPORT_CLASSES:
            return export, class_name
    return None, ""


def _guess_parent_class_index(package: UAssetPackage):
    excluded = {
        "Blueprint",
        "BlueprintCore",
        "BlueprintGeneratedClass",
        "WidgetBlueprintGeneratedClass",
        "AnimBlueprintGeneratedClass",
        "Package",
    }
    for offset, entry in enumerate(package.imports, start=1):
        if entry.class_name == "Class" and entry.object_name not in excluded:
            return -offset
    return None


def _read_blueprint_summary_properties(
    package: UAssetPackage,
    reader: UAssetReader,
    offset: int,
) -> dict:
    result: dict = {}
    if offset < 0:
        return result

    try:
        reader.seek(offset)
        while True:
            tag = read_property_tag(reader, package)
            if tag is None:
                break

            remaining = tag.size
            if tag.name == "ParentClass" and tag.type in {"ObjectProperty", "ClassProperty", "SoftClassProperty"}:
                if tag.size >= 4:
                    result["parent_class_index"] = reader.read_int32()
                    remaining -= 4
            elif tag.name == "BlueprintType":
                value = _read_blueprint_type_value(package, reader, tag)
                if value is not None:
                    result["blueprint_type"] = value
                remaining = 0

            if remaining > 0:
                reader.skip(remaining)
    except Exception:
        return result

    return result


def _read_blueprint_type_value(
    package: UAssetPackage,
    reader: UAssetReader,
    tag,
) -> int | None:
    if tag.size == 1:
        return reader.read_bytes(1)[0]
    if tag.size == 4:
        return reader.read_int32()
    if tag.size == 8:
        if tag.type == "EnumProperty":
            enum_name = package.get_name(reader.read_int64())
            return _blueprint_type_from_name(enum_name)
        return reader.read_int64()
    return None


def _blueprint_type_from_name(name: str) -> int | None:
    if not name:
        return None
    suffix = name.split("_")[-1]
    for value, label in BLUEPRINT_TYPE_NAMES.items():
        if label == suffix:
            return value
    return None
