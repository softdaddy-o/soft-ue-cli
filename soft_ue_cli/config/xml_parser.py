"""Parser for UnrealBuildTool BuildConfiguration.xml files."""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

_NS = "https://www.unrealengine.com/BuildConfiguration"
_NS_PREFIX = f"{{{_NS}}}"


class BuildConfigXml:
    """Read/write BuildConfiguration.xml with section.key access."""

    def __init__(self, root: ET.Element, path: Path | None = None) -> None:
        self._root = root
        self.path = path

    @classmethod
    def from_string(cls, text: str, path: Path | None = None) -> BuildConfigXml:
        return cls(ET.fromstring(text), path=path)

    @classmethod
    def from_file(cls, path: str | Path) -> BuildConfigXml:
        file_path = Path(path)
        tree = ET.parse(file_path)
        return cls(tree.getroot(), path=file_path)

    def get(self, section: str, key: str) -> str | None:
        section_el = self._find_section(section)
        if section_el is None:
            return None
        key_el = section_el.find(f"{_NS_PREFIX}{key}")
        if key_el is None:
            key_el = section_el.find(key)
        if key_el is None:
            return None
        return key_el.text

    def sections(self) -> list[str]:
        return [child.tag.removeprefix(_NS_PREFIX) for child in self._root]

    def keys(self, section: str) -> list[str]:
        section_el = self._find_section(section)
        if section_el is None:
            return []
        return [child.tag.removeprefix(_NS_PREFIX) for child in section_el]

    def items(self, section: str) -> dict[str, str]:
        section_el = self._find_section(section)
        if section_el is None:
            return {}
        return {child.tag.removeprefix(_NS_PREFIX): (child.text or "") for child in section_el}

    def set(self, section: str, key: str, value: str) -> None:
        section_el = self._find_section(section)
        if section_el is None:
            section_el = ET.SubElement(self._root, f"{_NS_PREFIX}{section}")
        key_el = section_el.find(f"{_NS_PREFIX}{key}")
        if key_el is None:
            key_el = section_el.find(key)
        if key_el is None:
            key_el = ET.SubElement(section_el, f"{_NS_PREFIX}{key}")
        key_el.text = value

    def to_string(self) -> str:
        ET.register_namespace("", _NS)
        return ET.tostring(self._root, encoding="unicode", xml_declaration=True)

    def write(self, path: str | Path | None = None) -> None:
        output_path = Path(path) if path else self.path
        if output_path is None:
            raise ValueError("No path specified")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        ET.register_namespace("", _NS)
        ET.ElementTree(self._root).write(str(output_path), encoding="utf-8", xml_declaration=True)

    def _find_section(self, section: str) -> ET.Element | None:
        section_el = self._root.find(f"{_NS_PREFIX}{section}")
        if section_el is None:
            section_el = self._root.find(section)
        return section_el
