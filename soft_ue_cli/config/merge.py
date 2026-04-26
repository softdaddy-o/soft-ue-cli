"""Offline merge helpers for UE INI config files."""

from __future__ import annotations

from typing import Any

from .ini_parser import UeIniFile


def merge_ini_layers(layers: list[UeIniFile]) -> UeIniFile:
    """Merge INI layers in ascending priority order."""
    result = UeIniFile()
    all_sections: set[str] = set()
    for layer in layers:
        all_sections.update(layer.sections())

    for section in all_sections:
        for layer in layers:
            for entry in layer.raw_entries(section):
                if entry.op == "=":
                    result.set(section, entry.key, entry.value)
                elif entry.op == "+":
                    result.append(section, entry.key, entry.value)
                elif entry.op == "-":
                    result.remove(section, entry.key, entry.value)
                elif entry.op == ".":
                    result.delete(section, entry.key)
                    result.append(section, entry.key, entry.value)
                elif entry.op == "!":
                    result.delete(section, entry.key)

    return result


def trace_key(layers: list[UeIniFile], section: str, key: str) -> list[dict[str, Any]]:
    """Return a trace of key mutations across layers."""
    trace: list[dict[str, Any]] = []
    for index, layer in enumerate(layers):
        for entry in layer.raw_entries(section):
            if entry.key != key:
                continue
            trace.append(
                {
                    "layer_index": index,
                    "layer_path": str(layer.path) if layer.path else f"<layer {index}>",
                    "op": entry.op,
                    "value": entry.value,
                },
            )
    return trace
