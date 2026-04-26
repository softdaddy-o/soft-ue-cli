"""Unified UE configuration read/write/audit tools."""

from __future__ import annotations

import re

from .diff import diff_ini_files, diff_xml_files
from .discovery import CONFIG_TYPES, ConfigDiscovery, ConfigLayer, ProjectJsonFile
from .ini_parser import UeIniFile
from .json_parser import ProjectJson
from .merge import merge_ini_layers, trace_key
from .xml_parser import BuildConfigXml

_KEY_RE = re.compile(r"^\[(.+?)\](.*)$")


def parse_ini_key(key_str: str) -> tuple[str, str | None]:
    """Parse ``[Section]Key`` into ``(section, key)``."""
    match = _KEY_RE.match(key_str)
    if not match:
        raise ValueError(
            f"Invalid INI key format: '{key_str}'. Expected '[section]Key' "
            "(e.g. '[/Script/Engine.RendererSettings]r.Bloom')",
        )
    section = match.group(1)
    key = match.group(2) or None
    return section, key


__all__ = [
    "BuildConfigXml",
    "CONFIG_TYPES",
    "ConfigDiscovery",
    "ConfigLayer",
    "ProjectJson",
    "ProjectJsonFile",
    "UeIniFile",
    "diff_ini_files",
    "diff_xml_files",
    "merge_ini_layers",
    "parse_ini_key",
    "trace_key",
]
