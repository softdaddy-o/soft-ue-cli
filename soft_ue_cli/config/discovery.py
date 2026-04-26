"""Discover and resolve UE config file hierarchy paths."""

from __future__ import annotations

import os
import platform
from dataclasses import dataclass
from pathlib import Path

_INI_LAYERS = [
    ("AbsoluteBase", "{ENGINE}/Config/Base.ini", True),
    ("Base", "{ENGINE}/Config/Base{TYPE}.ini", False),
    ("BasePlatform", "{ENGINE}/Config/{PLATFORM}/Base{PLATFORM}{TYPE}.ini", False),
    ("ProjectDefault", "{PROJECT}/Config/Default{TYPE}.ini", False),
    ("ProjectGenerated", "{PROJECT}/Config/Generated{TYPE}.ini", False),
    ("CustomConfig", "{PROJECT}/Config/Custom/{CUSTOMCONFIG}/Default{TYPE}.ini", False),
    ("EnginePlatform", "{ENGINE}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini", False),
    ("ProjectPlatform", "{PROJECT}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini", False),
    ("ProjectPlatformGenerated", "{PROJECT}/Config/{PLATFORM}/Generated{PLATFORM}{TYPE}.ini", False),
    ("CustomConfigPlatform", "{PROJECT}/Config/{PLATFORM}/Custom/{CUSTOMCONFIG}/{PLATFORM}{TYPE}.ini", False),
    ("UserSettingsDir", "{USERSETTINGS}/Unreal Engine/Engine/Config/User{TYPE}.ini", False),
    ("UserDir", "{USER}/Unreal Engine/Engine/Config/User{TYPE}.ini", False),
    ("GameDirUser", "{PROJECT}/Config/User{TYPE}.ini", False),
]
_RESTRICTED_DIRS = ["Restricted/NotForLicensees", "Restricted/NoRedist", "Restricted/LimitedAccess"]
_XML_LAYERS = [
    ("engine", "{ENGINE}/Saved/UnrealBuildTool/BuildConfiguration.xml"),
    ("user-global", "{APPDATA}/Unreal Engine/UnrealBuildTool/BuildConfiguration.xml"),
    ("project-local", "{PROJECT}/Saved/Config/UnrealBuildTool/BuildConfiguration.xml"),
]
CONFIG_TYPES = [
    "Engine",
    "Game",
    "Input",
    "Editor",
    "EditorPerProjectUserSettings",
    "GameUserSettings",
    "Scalability",
    "Hardware",
]


@dataclass
class ConfigLayer:
    """A discovered config layer."""

    name: str
    path: Path
    exists: bool
    size: int = 0
    mtime: float = 0.0


@dataclass
class ProjectJsonFile:
    """A discovered ``.uproject`` or ``.uplugin`` file."""

    path: Path
    file_type: str


class ConfigDiscovery:
    """Resolve UE config hierarchy paths and probe file existence."""

    def __init__(self, engine_path: str | Path | None = None, project_path: str | Path | None = None) -> None:
        self.engine_path = Path(engine_path) if engine_path else None
        self.project_path = Path(project_path) if project_path else None

    def ini_layers(
        self,
        config_type: str = "Engine",
        platform: str | None = None,
        custom_config: str | None = None,
    ) -> list[ConfigLayer]:
        tokens = self._build_tokens(config_type, platform, custom_config)
        layers: list[ConfigLayer] = []

        for name, pattern, no_expand in _INI_LAYERS:
            if "{PLATFORM}" in pattern and not platform:
                continue
            if "{CUSTOMCONFIG}" in pattern and not custom_config:
                continue

            path = self._resolve_path(pattern, tokens)
            if path is None:
                continue
            layers.append(self._make_layer(name, path))

            if not no_expand and "{ENGINE}" in pattern:
                for restricted in _RESTRICTED_DIRS:
                    layers.append(self._make_layer(f"{name}_Restricted", path.parent / restricted / path.name))

        if platform and self.project_path:
            layers.append(self._make_layer("Saved", self.project_path / "Saved" / "Config" / platform / f"{config_type}.ini"))

        return layers

    def xml_layers(self) -> list[ConfigLayer]:
        tokens = self._build_tokens()
        layers: list[ConfigLayer] = []
        for name, pattern in _XML_LAYERS:
            path = self._resolve_path(pattern, tokens)
            if path is not None:
                layers.append(self._make_layer(name, path))
        return layers

    def project_json_files(self) -> list[ProjectJsonFile]:
        results: list[ProjectJsonFile] = []
        if not self.project_path:
            return results

        results.extend(ProjectJsonFile(path=path, file_type="uproject") for path in self.project_path.glob("*.uproject"))
        plugins_dir = self.project_path / "Plugins"
        if plugins_dir.is_dir():
            results.extend(ProjectJsonFile(path=path, file_type="uplugin") for path in plugins_dir.rglob("*.uplugin"))
        return results

    def detect_platforms(self) -> list[str]:
        platforms: set[str] = set()
        for base in (self.engine_path, self.project_path):
            if not base:
                continue
            config_dir = base / "Config"
            if not config_dir.is_dir():
                continue
            for child in config_dir.iterdir():
                if child.is_dir() and child.name and child.name[0].isupper():
                    if child.name not in {"Custom", "Layouts", "Localization"}:
                        platforms.add(child.name)
        return sorted(platforms)

    def _build_tokens(
        self,
        config_type: str = "",
        platform_name: str | None = None,
        custom_config: str | None = None,
    ) -> dict[str, str]:
        tokens = {
            "{TYPE}": config_type,
            "{PLATFORM}": platform_name or "",
            "{CUSTOMCONFIG}": custom_config or "",
        }
        if self.engine_path:
            tokens["{ENGINE}"] = str(self.engine_path)
        if self.project_path:
            tokens["{PROJECT}"] = str(self.project_path)

        if platform.system() == "Windows":
            tokens["{USERSETTINGS}"] = os.environ.get("LOCALAPPDATA", "")
            tokens["{USER}"] = str(Path.home() / "Documents")
            tokens["{APPDATA}"] = os.environ.get("APPDATA", "")
        else:
            home = str(Path.home())
            tokens["{USERSETTINGS}"] = home
            tokens["{USER}"] = home
            tokens["{APPDATA}"] = home
        return tokens

    def _resolve_path(self, pattern: str, tokens: dict[str, str]) -> Path | None:
        value = pattern
        for token, replacement in tokens.items():
            if token in value:
                if not replacement:
                    return None
                value = value.replace(token, replacement)
        return Path(value)

    def _make_layer(self, name: str, path: Path) -> ConfigLayer:
        exists = path.is_file()
        stat = path.stat() if exists else None
        return ConfigLayer(
            name=name,
            path=path,
            exists=exists,
            size=stat.st_size if stat else 0,
            mtime=stat.st_mtime if stat else 0.0,
        )
