"""Parser for Unreal Engine extended INI format."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

_SECTION_RE = re.compile(r"^\[(.+)\]\s*$")
_OP_RE = re.compile(r"^([+\-.!])?(\S.*?)(?:=(.*))?$")


@dataclass
class _Entry:
    """A single INI operation."""

    op: str
    key: str
    value: str


@dataclass
class UeIniFile:
    """Parsed UE-style INI file with array and deletion semantics."""

    _sections: dict[str, list[_Entry]] = field(default_factory=dict)
    path: Path | None = None

    @classmethod
    def from_string(cls, text: str, path: Path | None = None) -> UeIniFile:
        ini = cls(path=path)
        ini._parse(text)
        return ini

    @classmethod
    def from_file(cls, path: str | Path) -> UeIniFile:
        file_path = Path(path)
        return cls.from_string(file_path.read_text(encoding="utf-8-sig"), path=file_path)

    def get(self, section: str, key: str) -> str | None:
        resolved = self._resolve(section)
        value = resolved.get(key)
        if value is None:
            return None
        if isinstance(value, list):
            return value[-1] if value else None
        return value

    def get_array(self, section: str, key: str) -> list[str]:
        resolved = self._resolve(section)
        value = resolved.get(key)
        if value is None:
            return []
        if isinstance(value, list):
            return list(value)
        return [value]

    def sections(self) -> list[str]:
        return list(self._sections.keys())

    def keys(self, section: str) -> list[str]:
        return list(self._resolve(section).keys())

    def items(self, section: str) -> dict[str, str | list[str]]:
        return dict(self._resolve(section))

    def raw_entries(self, section: str) -> list[_Entry]:
        return list(self._sections.get(section, []))

    def set(self, section: str, key: str, value: str) -> None:
        entries = self._sections.setdefault(section, [])
        entries[:] = [entry for entry in entries if entry.key != key]
        entries.append(_Entry(op="=", key=key, value=value))

    def append(self, section: str, key: str, value: str) -> None:
        self._sections.setdefault(section, []).append(_Entry(op="+", key=key, value=value))

    def remove(self, section: str, key: str, value: str) -> None:
        self._sections.setdefault(section, []).append(_Entry(op="-", key=key, value=value))

    def delete(self, section: str, key: str) -> None:
        self._sections.setdefault(section, []).append(_Entry(op="!", key=key, value=""))

    def to_string(self) -> str:
        lines: list[str] = []
        for section_name, entries in self._sections.items():
            lines.append(f"[{section_name}]")
            for entry in entries:
                if entry.op == "!":
                    lines.append(f"!{entry.key}")
                elif entry.op == "=":
                    lines.append(f"{entry.key}={entry.value}")
                else:
                    lines.append(f"{entry.op}{entry.key}={entry.value}")
            lines.append("")
        return "\n".join(lines)

    def write(self, path: str | Path | None = None) -> None:
        output_path = Path(path) if path else self.path
        if output_path is None:
            raise ValueError("No path specified and no path associated with this file")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(self.to_string(), encoding="utf-8")

    def _parse(self, text: str) -> None:
        current_section: str | None = None
        continuation_key: str | None = None

        for raw_line in text.splitlines():
            line = raw_line.rstrip()

            if not line or line.lstrip().startswith(";") or line.lstrip().startswith("#"):
                continuation_key = None
                continue

            if raw_line[:1] in (" ", "\t") and continuation_key and current_section:
                entries = self._sections[current_section]
                if entries and entries[-1].key == continuation_key:
                    entries[-1].value += "\n" + line.strip()
                continue

            section_match = _SECTION_RE.match(line)
            if section_match:
                current_section = section_match.group(1)
                continuation_key = None
                self._sections.setdefault(current_section, [])
                continue

            if current_section is None:
                continue

            entry_match = _OP_RE.match(line)
            if not entry_match:
                continue

            op_char = entry_match.group(1) or ""
            key = entry_match.group(2)
            raw_value = entry_match.group(3)

            if op_char == "!":
                self._sections[current_section].append(_Entry(op="!", key=key, value=""))
                continuation_key = None
                continue

            value = raw_value if raw_value is not None else ""
            if len(value) >= 2 and value.startswith('"') and value.endswith('"'):
                value = value[1:-1]

            op = op_char if op_char in ("+", "-", ".") else "="
            self._sections[current_section].append(_Entry(op=op, key=key, value=value))
            continuation_key = key

    def _resolve(self, section: str) -> dict[str, str | list[str]]:
        entries = self._sections.get(section, [])
        result: dict[str, str | list[str]] = {}

        for entry in entries:
            if entry.op == "!":
                result.pop(entry.key, None)
            elif entry.op == "+":
                current = result.get(entry.key)
                if current is None:
                    result[entry.key] = [entry.value]
                elif isinstance(current, list):
                    current.append(entry.value)
                else:
                    result[entry.key] = [current, entry.value]
            elif entry.op == "-":
                current = result.get(entry.key)
                if isinstance(current, list):
                    try:
                        current.remove(entry.value)
                    except ValueError:
                        pass
                    if not current:
                        result.pop(entry.key, None)
            elif entry.op == ".":
                result[entry.key] = [entry.value]
            else:
                result[entry.key] = entry.value

        return result
