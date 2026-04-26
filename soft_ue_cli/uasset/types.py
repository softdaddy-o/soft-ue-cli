"""Data types shared across the offline .uasset parser."""

from __future__ import annotations

from dataclasses import dataclass, field


PACKAGE_MAGIC = 0x9E2A83C1


class UAssetError(Exception):
    """Raised when a .uasset file cannot be parsed safely."""

    def __init__(self, message: str, offset: int | None = None) -> None:
        self.offset = offset
        prefix = f"[offset 0x{offset:X}] " if offset is not None else ""
        super().__init__(f"{prefix}{message}")


BLUEPRINT_TYPE_NAMES: dict[int, str] = {
    0: "Normal",
    1: "Const",
    2: "MacroLibrary",
    3: "Interface",
    4: "LevelScript",
    5: "FunctionLibrary",
}


@dataclass(slots=True)
class NameEntry:
    name: str
    non_case_preserving_hash: int = 0
    case_preserving_hash: int = 0


@dataclass(slots=True)
class ImportEntry:
    class_package: str
    class_name: str
    outer_index: int
    object_name: str


@dataclass(slots=True)
class ExportEntry:
    class_index: int
    super_index: int
    outer_index: int
    object_name: str
    serial_offset: int
    serial_size: int
    template_index: int = 0


@dataclass(slots=True)
class PackageSummary:
    magic: int = 0
    file_version_ue4: int = 0
    file_version_ue5: int = 0
    total_header_size: int = 0
    package_flags: int = 0
    name_count: int = 0
    name_offset: int = 0
    export_count: int = 0
    export_offset: int = 0
    import_count: int = 0
    import_offset: int = 0
    depends_offset: int = 0
    soft_package_references_count: int = 0
    soft_package_references_offset: int = 0
    asset_registry_data_offset: int = 0
    bulk_data_start_offset: int = 0


@dataclass(slots=True)
class PinType:
    category: str = ""
    sub_category_object: str = ""
    is_array: bool = False
    is_set: bool = False
    is_map: bool = False
    map_value_type: str = ""


@dataclass(slots=True)
class VariableDesc:
    name: str = ""
    pin_type: PinType = field(default_factory=PinType)
    flags: int = 0
    replication: str = "Unknown"
    rep_notify_func: str = ""
    category: str = ""


@dataclass(slots=True)
class ParameterDesc:
    name: str = ""
    type: str = ""
    sub_type: str = ""


@dataclass(slots=True)
class FunctionDesc:
    name: str = ""
    flags: list[str] = field(default_factory=list)
    parameters: list[ParameterDesc] = field(default_factory=list)


@dataclass(slots=True)
class ComponentDesc:
    name: str = ""
    component_class: str = ""
    parent: str = ""
    is_root: bool = False


@dataclass(slots=True)
class EventDesc:
    name: str = ""
    node_title: str = ""
    is_custom: bool = False
    parameters: list[ParameterDesc] = field(default_factory=list)
