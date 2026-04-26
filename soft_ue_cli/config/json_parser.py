"""Parser for .uproject and .uplugin JSON files."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class ProjectJson:
    """Read/write project JSON using dot-path access."""

    def __init__(self, data: dict, path: Path | None = None) -> None:
        self._data = data
        self.path = path

    @classmethod
    def from_string(cls, text: str, path: Path | None = None) -> ProjectJson:
        return cls(json.loads(text), path=path)

    @classmethod
    def from_file(cls, path: str | Path) -> ProjectJson:
        file_path = Path(path)
        return cls(json.loads(file_path.read_text(encoding="utf-8-sig")), path=file_path)

    def get(self, key_path: str) -> Any | None:
        parts = key_path.split(".")
        current: Any = self._data
        for part in parts:
            if isinstance(current, dict):
                if part not in current:
                    return None
                current = current[part]
            elif isinstance(current, list):
                try:
                    current = current[int(part)]
                except (ValueError, IndexError):
                    return None
            else:
                return None
        return current

    def keys(self) -> list[str]:
        return list(self._data.keys())

    def items(self) -> dict[str, Any]:
        return dict(self._data)

    def set(self, key_path: str, value: Any) -> None:
        parts = key_path.split(".")
        if len(parts) == 1:
            self._data[parts[0]] = value
            return

        current: Any = self._data
        for part in parts[:-1]:
            if isinstance(current, dict):
                current = current.setdefault(part, {})
            elif isinstance(current, list):
                current = current[int(part)]
            else:
                raise ValueError(f"Cannot descend into non-container value for '{key_path}'")

        if not isinstance(current, dict):
            raise ValueError(f"Cannot set nested value for '{key_path}'")
        current[parts[-1]] = value

    def to_string(self) -> str:
        return json.dumps(self._data, indent="\t", ensure_ascii=False) + "\n"

    def write(self, path: str | Path | None = None) -> None:
        output_path = Path(path) if path else self.path
        if output_path is None:
            raise ValueError("No path specified")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(self.to_string(), encoding="utf-8")
