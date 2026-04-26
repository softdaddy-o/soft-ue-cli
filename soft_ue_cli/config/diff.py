"""Diff engine for UE config files."""

from __future__ import annotations

from typing import Any

from .ini_parser import UeIniFile
from .xml_parser import BuildConfigXml


def diff_ini_files(a: UeIniFile, b: UeIniFile) -> dict[str, Any]:
    """Diff two resolved INI files."""
    all_sections = set(a.sections()) | set(b.sections())
    sections: dict[str, Any] = {}
    total_changes = 0

    for section in sorted(all_sections):
        items_a = a.items(section)
        items_b = b.items(section)
        all_keys = set(items_a.keys()) | set(items_b.keys())
        changes: list[dict[str, Any]] = []

        for key in sorted(all_keys):
            value_a = items_a.get(key)
            value_b = items_b.get(key)
            if value_a is None and value_b is not None:
                changes.append({"key": key, "type": "added", "value": value_b})
            elif value_a is not None and value_b is None:
                changes.append({"key": key, "type": "removed", "value": value_a})
            elif value_a != value_b:
                changes.append({"key": key, "type": "changed", "old": value_a, "new": value_b})

        if changes:
            sections[section] = {"changes": changes, "change_count": len(changes)}
            total_changes += len(changes)

    return {"sections": sections, "total_changes": total_changes}


def diff_xml_files(a: BuildConfigXml, b: BuildConfigXml) -> dict[str, Any]:
    """Diff two BuildConfiguration.xml files."""
    all_sections = set(a.sections()) | set(b.sections())
    sections: dict[str, Any] = {}
    total_changes = 0

    for section in sorted(all_sections):
        items_a = a.items(section)
        items_b = b.items(section)
        all_keys = set(items_a.keys()) | set(items_b.keys())
        changes: list[dict[str, Any]] = []

        for key in sorted(all_keys):
            value_a = items_a.get(key)
            value_b = items_b.get(key)
            if value_a is None and value_b is not None:
                changes.append({"key": key, "type": "added", "value": value_b})
            elif value_a is not None and value_b is None:
                changes.append({"key": key, "type": "removed", "value": value_a})
            elif value_a != value_b:
                changes.append({"key": key, "type": "changed", "old": value_a, "new": value_b})

        if changes:
            sections[section] = {"changes": changes, "change_count": len(changes)}
            total_changes += len(changes)

    return {"sections": sections, "total_changes": total_changes}
