"""Offline .uasset parser for Unreal Engine Blueprint files."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

from .blueprint import (
    extract_blueprint,
    extract_components,
    extract_events,
    extract_functions,
    extract_variables,
)
from .package import UAssetPackage
from .reader import UAssetReader
from .types import UAssetError

VALID_SECTIONS = frozenset({"summary", "variables", "functions", "components", "events", "all"})


def inspect_uasset(
    path: str | Path,
    sections: Sequence[str] | str = ("summary",),
) -> dict:
    """Parse a local .uasset file and return structured Blueprint metadata."""
    asset_path = Path(path)
    if not asset_path.exists():
        raise FileNotFoundError(f"File not found: {asset_path}")

    if isinstance(sections, str):
        sections = [part.strip() for part in sections.split(",") if part.strip()]

    requested = {section.strip() for section in sections if section.strip()}
    if not requested:
        requested = {"summary"}

    unknown = requested - VALID_SECTIONS
    if unknown:
        raise ValueError(f"Unknown sections: {sorted(unknown)}. Valid sections: {sorted(VALID_SECTIONS)}")

    want_all = "all" in requested

    with asset_path.open("rb") as asset_stream:
        package = UAssetPackage(asset_stream)

    uexp_path = asset_path.with_suffix(".uexp")
    data_path = uexp_path if uexp_path.exists() else asset_path
    offset_adjust = package.summary.total_header_size if uexp_path.exists() else 0

    with data_path.open("rb") as data_stream:
        reader = UAssetReader(data_stream)
        result: dict = {
            "file": str(asset_path.resolve()),
            "name": asset_path.stem,
            "ue_version": _format_ue_version(package),
            "asset_class": "Unknown",
            "parent_class": "Unknown",
            "parent_class_path": "",
            "blueprint_type": "Unknown",
        }

        try:
            result.update(extract_blueprint(package, reader, offset_adjust=offset_adjust))
            is_blueprint = True
        except UAssetError:
            is_blueprint = False
            result.update(_extract_generic_summary(package))

        variables_section = None
        functions_section = None
        components_section = None
        events_section = None

        if is_blueprint:
            if want_all or "summary" in requested or "variables" in requested:
                variables_section = extract_variables(package, reader, offset_adjust=offset_adjust)
            if want_all or "summary" in requested or "functions" in requested:
                functions_section = extract_functions(package)
            if want_all or "summary" in requested or "components" in requested:
                components_section = extract_components(package, reader, offset_adjust=offset_adjust)
            if want_all or "summary" in requested or "events" in requested:
                events_section = extract_events(package)

        if want_all or "summary" in requested:
            result["variable_count"] = 0 if variables_section is None else variables_section.get("count", 0)
            result["function_count"] = 0 if functions_section is None else functions_section.get("count", 0)
            result["component_count"] = 0 if components_section is None else components_section.get("count", 0)
            result["event_count"] = (
                0
                if events_section is None
                else events_section.get("event_count", 0) + events_section.get("custom_event_count", 0)
            )

        if want_all or "variables" in requested:
            result["variables"] = variables_section or {"count": 0, "items": [], "fidelity": "unavailable"}
        if want_all or "functions" in requested:
            result["functions"] = functions_section or {"count": 0, "items": [], "fidelity": "unavailable"}
        if want_all or "components" in requested:
            result["components"] = components_section or {"count": 0, "items": [], "fidelity": "unavailable"}
        if want_all or "events" in requested:
            result["events"] = events_section or {
                "events": [],
                "custom_events": [],
                "event_count": 0,
                "custom_event_count": 0,
                "fidelity": "unavailable",
            }

    return result


def diff_uasset(
    left_path: str | Path,
    right_path: str | Path,
    sections: Sequence[str] | str = ("summary",),
) -> dict:
    """Diff two local .uasset files via their offline inspected metadata."""
    if isinstance(sections, str):
        sections = [part.strip() for part in sections.split(",") if part.strip()]

    requested = {section.strip() for section in sections if section.strip()}
    if not requested:
        requested = {"summary"}

    unknown = requested - VALID_SECTIONS
    if unknown:
        raise ValueError(f"Unknown sections: {sorted(unknown)}. Valid sections: {sorted(VALID_SECTIONS)}")

    inspect_sections = sorted((requested - {"all"}) | {"summary"})
    if "all" in requested:
        inspect_sections = ["all"]
        requested = {"summary", "variables", "functions", "components", "events"}

    left = inspect_uasset(left_path, sections=inspect_sections)
    right = inspect_uasset(right_path, sections=inspect_sections)

    changes: dict = {}
    total_changes = 0

    if "summary" in requested:
        summary = _diff_summary(left, right)
        changes["summary"] = summary
        total_changes += summary["change_count"]

    for section in ("variables", "functions", "components"):
        if section in requested:
            payload = _diff_named_items(left.get(section), right.get(section))
            changes[section] = payload
            total_changes += payload["change_count"]

    if "events" in requested:
        payload = _diff_events(left.get("events"), right.get("events"))
        changes["events"] = payload
        total_changes += payload["change_count"]

    return {
        "left_file": left["file"],
        "right_file": right["file"],
        "sections": sorted(requested),
        "has_changes": total_changes > 0,
        "total_changes": total_changes,
        "changes": changes,
    }


def _extract_generic_summary(package: UAssetPackage) -> dict:
    """Fallback summary for non-Blueprint assets using the export/import tables."""
    asset_class = "Unknown"
    for exp in package.exports:
        class_name = package.resolve_import_class(exp.class_index)
        if class_name and not class_name.startswith("<") and class_name != "Package":
            asset_class = class_name
            break

    exports = []
    for exp in package.exports:
        class_name = package.resolve_import_class(exp.class_index)
        if class_name.startswith("<"):
            class_name = "Unknown"
        exports.append({
            "class": class_name,
            "name": exp.object_name,
            "size": exp.serial_size,
        })

    imports = []
    for imp in package.imports:
        imports.append({
            "class": imp.class_name,
            "name": imp.object_name,
        })

    return {
        "asset_class": asset_class,
        "blueprint_type": "N/A",
        "parent_class": "N/A",
        "parent_class_path": "",
        "name_count": len(package.names),
        "import_count": len(package.imports),
        "export_count": len(package.exports),
        "exports": exports,
        "imports": imports,
    }


def _format_ue_version(package: UAssetPackage) -> str:
    version = package.summary.file_version_ue5
    if version >= 1012:
        return "5.7"
    if version >= 1010:
        return "5.6"
    if version >= 1009:
        return "5.5"
    if version >= 1007:
        return "5.4"
    if version >= 1005:
        return "5.3"
    if version >= 1003:
        return "5.2"
    if version >= 1001:
        return "5.1"
    if version > 0:
        return f"5.x (build {version})"
    if package.summary.file_version_ue4 > 0:
        return f"4.x (build {package.summary.file_version_ue4})"
    return "Unknown"


def _diff_summary(left: dict, right: dict) -> dict:
    fields = (
        "name",
        "ue_version",
        "asset_class",
        "parent_class",
        "parent_class_path",
        "blueprint_type",
        "variable_count",
        "function_count",
        "component_count",
        "event_count",
    )
    modified = {}
    for field in fields:
        if left.get(field) != right.get(field):
            modified[field] = {"old": left.get(field), "new": right.get(field)}
    return {"modified": modified, "change_count": len(modified)}


def _diff_named_items(left_section: dict | None, right_section: dict | None) -> dict:
    left_section = left_section or {"items": [], "fidelity": "unavailable"}
    right_section = right_section or {"items": [], "fidelity": "unavailable"}

    left_items = {str(item.get("name")): item for item in left_section.get("items", []) if item.get("name")}
    right_items = {str(item.get("name")): item for item in right_section.get("items", []) if item.get("name")}

    added = [right_items[name] for name in sorted(right_items.keys() - left_items.keys())]
    removed = [left_items[name] for name in sorted(left_items.keys() - right_items.keys())]

    modified = []
    for name in sorted(left_items.keys() & right_items.keys()):
        if left_items[name] == right_items[name]:
            continue
        modified.append(
            {
                "name": name,
                "old": left_items[name],
                "new": right_items[name],
            }
        )

    fidelity_changed = left_section.get("fidelity") != right_section.get("fidelity")
    result = {
        "added": added,
        "removed": removed,
        "modified": modified,
        "change_count": len(added) + len(removed) + len(modified) + (1 if fidelity_changed else 0),
        "left_fidelity": left_section.get("fidelity"),
        "right_fidelity": right_section.get("fidelity"),
    }
    if fidelity_changed:
        result["fidelity_changed"] = {
            "old": left_section.get("fidelity"),
            "new": right_section.get("fidelity"),
        }
    return result


def _diff_events(left_section: dict | None, right_section: dict | None) -> dict:
    left_section = left_section or {"events": [], "custom_events": [], "fidelity": "unavailable"}
    right_section = right_section or {"events": [], "custom_events": [], "fidelity": "unavailable"}

    left_events = {str(item.get("name")): item for item in left_section.get("events", []) if item.get("name")}
    right_events = {str(item.get("name")): item for item in right_section.get("events", []) if item.get("name")}
    left_custom = {str(item.get("name")): item for item in left_section.get("custom_events", []) if item.get("name")}
    right_custom = {str(item.get("name")): item for item in right_section.get("custom_events", []) if item.get("name")}

    added = [right_events[name] for name in sorted(right_events.keys() - left_events.keys())]
    removed = [left_events[name] for name in sorted(left_events.keys() - right_events.keys())]
    added_custom = [right_custom[name] for name in sorted(right_custom.keys() - left_custom.keys())]
    removed_custom = [left_custom[name] for name in sorted(left_custom.keys() - right_custom.keys())]

    fidelity_changed = left_section.get("fidelity") != right_section.get("fidelity")
    result = {
        "added": added,
        "removed": removed,
        "added_custom": added_custom,
        "removed_custom": removed_custom,
        "change_count": len(added) + len(removed) + len(added_custom) + len(removed_custom) + (1 if fidelity_changed else 0),
        "left_fidelity": left_section.get("fidelity"),
        "right_fidelity": right_section.get("fidelity"),
    }
    if fidelity_changed:
        result["fidelity_changed"] = {
            "old": left_section.get("fidelity"),
            "new": right_section.get("fidelity"),
        }
    return result


__all__ = [
    "UAssetError",
    "VALID_SECTIONS",
    "build_replay_bundle",
    "diff_uasset",
    "extract_git_conflict_stages",
    "inspect_uasset",
    "sync_remote_version",
    "write_replay_bundle",
]
