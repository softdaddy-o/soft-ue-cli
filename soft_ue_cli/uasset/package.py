"""Parse .uasset package headers, names, imports, and exports."""

from __future__ import annotations

from typing import BinaryIO

from .reader import UAssetReader
from .types import ExportEntry, ImportEntry, NameEntry, PACKAGE_MAGIC, PackageSummary, UAssetError


class UAssetPackage:
    """Representation of a parsed Unreal package header and object tables."""

    def __init__(self, stream: BinaryIO) -> None:
        self._reader = UAssetReader(stream)
        self.summary = PackageSummary()
        self.names: list[NameEntry] = []
        self.imports: list[ImportEntry] = []
        self.exports: list[ExportEntry] = []

        try:
            self._parse_header()
            self._parse_name_table()
            self._parse_import_table()
            self._parse_export_table()
        except EOFError as exc:
            raise UAssetError(str(exc), offset=self._reader.tell()) from exc

    def get_name(self, index: int) -> str:
        if 0 <= index < len(self.names):
            return self.names[index].name
        return f"<invalid_name_{index}>"

    def resolve_object_name(self, index: int) -> str:
        if index < 0:
            actual = -index - 1
            if 0 <= actual < len(self.imports):
                return self.imports[actual].object_name
        elif index > 0:
            actual = index - 1
            if 0 <= actual < len(self.exports):
                return self.exports[actual].object_name
        return f"<unresolved_{index}>"

    def resolve_import_class(self, index: int) -> str:
        if index < 0:
            actual = -index - 1
            if 0 <= actual < len(self.imports):
                return self.imports[actual].object_name
        elif index > 0:
            actual = index - 1
            if 0 <= actual < len(self.exports):
                return self.exports[actual].object_name
        return f"<unresolved_{index}>"

    def resolve_import_entry(self, index: int) -> ImportEntry | None:
        if index >= 0:
            return None
        actual = -index - 1
        if 0 <= actual < len(self.imports):
            return self.imports[actual]
        return None

    def resolve_import_object_path(self, index: int) -> str:
        entry = self.resolve_import_entry(index)
        if entry is None:
            return f"<unresolved_{index}>"

        outer_path = self._resolve_outer_path(entry.outer_index)
        if outer_path:
            return f"{outer_path}.{entry.object_name}"

        if entry.object_name.startswith("/"):
            return entry.object_name
        return entry.object_name

    def _resolve_outer_path(self, index: int) -> str:
        if index == 0:
            return ""

        if index < 0:
            entry = self.resolve_import_entry(index)
            if entry is None:
                return ""

            parent_path = self._resolve_outer_path(entry.outer_index)
            if entry.class_name == "Package":
                if entry.object_name.startswith("/"):
                    return entry.object_name
                if parent_path:
                    return f"{parent_path}/{entry.object_name}"
                return f"/Script/{entry.object_name}"

            if parent_path:
                return f"{parent_path}.{entry.object_name}"
            return entry.object_name

        actual = index - 1
        if 0 <= actual < len(self.exports):
            return self.exports[actual].object_name
        return ""

    def _parse_header(self) -> None:
        r = self._reader
        s = self.summary

        s.magic = r.read_uint32()
        if s.magic != PACKAGE_MAGIC:
            raise UAssetError(
                f"Invalid .uasset magic: 0x{s.magic:08X} (expected 0x{PACKAGE_MAGIC:08X})",
                offset=0,
            )

        r.read_int32()  # LegacyFileVersion
        r.read_int32()  # LegacyUE3Version
        s.file_version_ue4 = r.read_int32()
        s.file_version_ue5 = r.read_int32()
        r.read_int32()  # FileVersionLicenseeUE

        custom_version_count = r.read_int32()
        for _ in range(custom_version_count):
            r.skip(16 + 4)

        s.total_header_size = r.read_int32()
        r.read_fstring()  # FolderName
        s.package_flags = r.read_uint32()

        s.name_count = r.read_int32()
        s.name_offset = r.read_int32()

        r.read_int32()  # SoftObjectPathsCount
        r.read_int32()  # SoftObjectPathsOffset
        r.read_fstring()  # LocalizationId
        r.read_int32()  # GatherableTextDataCount
        r.read_int32()  # GatherableTextDataOffset

        s.export_count = r.read_int32()
        s.export_offset = r.read_int32()
        s.import_count = r.read_int32()
        s.import_offset = r.read_int32()
        s.depends_offset = r.read_int32()
        s.soft_package_references_count = r.read_int32()
        s.soft_package_references_offset = r.read_int32()

        r.read_int32()  # SearchableNamesOffset
        r.read_int32()  # ThumbnailTableOffset
        r.read_fguid()  # Guid
        r.read_fguid()  # PersistentGuid

        generation_count = r.read_int32()
        for _ in range(generation_count):
            r.read_int32()
            r.read_int32()

        r.read_uint32()
        r.read_uint32()
        r.read_uint32()
        r.read_uint32()
        r.read_fstring()
        r.read_uint32()
        r.read_uint32()
        r.read_uint32()
        r.read_uint32()
        r.read_fstring()

        r.read_uint32()  # CompressionFlags
        compressed_chunks_count = r.read_int32()
        for _ in range(compressed_chunks_count):
            r.skip(16)

        r.read_uint32()  # PackageSource

        additional_count = r.read_int32()
        for _ in range(additional_count):
            r.read_fstring()

        s.asset_registry_data_offset = r.read_int32()
        s.bulk_data_start_offset = r.read_int32()

    def _parse_name_table(self) -> None:
        if self.summary.name_count <= 0 or self.summary.name_offset <= 0:
            return
        self._reader.seek(self.summary.name_offset)
        for _ in range(self.summary.name_count):
            name = self._reader.read_fstring()
            non_case_hash = self._reader.read_uint16()
            case_hash = self._reader.read_uint16()
            self.names.append(NameEntry(name=name, non_case_preserving_hash=non_case_hash, case_preserving_hash=case_hash))

    def _parse_import_table(self) -> None:
        if self.summary.import_count <= 0 or self.summary.import_offset <= 0:
            return

        entry_size = None
        if self.summary.export_offset > self.summary.import_offset and self.summary.import_count > 0:
            span = self.summary.export_offset - self.summary.import_offset
            if span > 0:
                entry_size = span // self.summary.import_count

        for index in range(self.summary.import_count):
            if entry_size is not None:
                self._reader.seek(self.summary.import_offset + index * entry_size)

            class_package_ref = self._reader.read_int64()
            class_name_ref = self._reader.read_int64()
            outer_index = self._reader.read_int32()
            object_name_ref = self._reader.read_int64()

            self.imports.append(
                ImportEntry(
                    class_package=self.get_name(_name_ref_to_index(class_package_ref, len(self.names))),
                    class_name=self.get_name(_name_ref_to_index(class_name_ref, len(self.names))),
                    outer_index=outer_index,
                    object_name=self.get_name(_name_ref_to_index(object_name_ref, len(self.names))),
                )
            )

    def _parse_export_table(self) -> None:
        if self.summary.export_count <= 0 or self.summary.export_offset <= 0:
            return

        entry_size = None
        if self.summary.depends_offset > self.summary.export_offset and self.summary.export_count > 0:
            span = self.summary.depends_offset - self.summary.export_offset
            if span > 0:
                entry_size = span // self.summary.export_count

        for index in range(self.summary.export_count):
            if entry_size is not None:
                self._reader.seek(self.summary.export_offset + index * entry_size)

            class_index = self._reader.read_int32()
            super_index = self._reader.read_int32()
            template_index = self._reader.read_int32()
            outer_index = self._reader.read_int32()
            object_name_ref = self._reader.read_int64()
            self._reader.read_uint32()  # ObjectFlags
            serial_size = self._reader.read_int64()
            serial_offset = self._reader.read_int64()

            self.exports.append(
                ExportEntry(
                    class_index=class_index,
                    super_index=super_index,
                    template_index=template_index,
                    outer_index=outer_index,
                    object_name=self.get_name(_name_ref_to_index(object_name_ref, len(self.names))),
                    serial_offset=int(serial_offset),
                    serial_size=int(serial_size),
                )
            )


def _name_ref_to_index(value: int, name_count: int) -> int:
    if 0 <= value < name_count:
        return value
    unsigned = value & 0xFFFFFFFFFFFFFFFF
    low = unsigned & 0xFFFFFFFF
    high = (unsigned >> 32) & 0xFFFFFFFF
    if 0 <= low < name_count:
        return int(low)
    if 0 <= high < name_count:
        return int(high)
    return int(value)
