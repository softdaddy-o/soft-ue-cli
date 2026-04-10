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

        # LegacyFileVersion meanings (from UE source PackageFileSummary.cpp):
        #   -2  enum-based custom versions
        #   -3  guid-based custom versions
        #   -4  removal of UE3 version (LegacyUE3Version is skipped)
        #   -5  replacement of UE3 version write
        #   -6  optimizations to custom version serialization
        #   -7  texture allocation info removed
        #   -8  UE5 version added to summary (FileVersionUE5)
        #   -9  contractual change in early-exit behavior for FileVersionTooNew
        legacy_file_version = r.read_int32()
        if legacy_file_version >= 0:
            raise UAssetError(
                f"Legacy UE3 .uasset format not supported (LegacyFileVersion={legacy_file_version})",
                offset=4,
            )

        if legacy_file_version != -4:
            r.read_int32()  # LegacyUE3Version

        s.file_version_ue4 = r.read_int32()

        if legacy_file_version <= -8:
            # UE 5.4+ adds FileVersionUE5 to the summary
            s.file_version_ue5 = r.read_int32()

        r.read_int32()  # FileVersionLicenseeUE

        # UE5 PACKAGE_SAVED_HASH (FileVersionUE5 >= 1016) serializes the
        # SavedHash (FIoHash, 20 bytes) and TotalHeaderSize BEFORE the
        # custom version container. The older format serializes neither
        # here — TotalHeaderSize comes after custom versions instead.
        saved_hash_serialized = s.file_version_ue5 >= 1016
        if saved_hash_serialized:
            r.skip(20)  # SavedHash (FIoHash)
            s.total_header_size = r.read_int32()

        # Custom versions (format depends on legacy_file_version):
        #   -2           : enum-based (int32 tag + int32 version)
        #   -3..-5       : guid-based (FGuid + int32 version)
        #   -6 and below : optimized (FGuid + int32 version + optional friendly name)
        # For our needs we only need to skip past them. The optimized format
        # adds an FString friendly name after each (GUID, version) pair.
        custom_version_count = r.read_int32()
        if legacy_file_version == -2:
            # Enum-based: int32 tag + int32 version
            for _ in range(custom_version_count):
                r.skip(8)
        else:
            # GUID-based: FGuid(16) + int32 version
            for _ in range(custom_version_count):
                r.skip(16 + 4)

        if not saved_hash_serialized:
            s.total_header_size = r.read_int32()

        r.read_fstring()  # PackageName (was FolderName in older versions)
        s.package_flags = r.read_uint32()

        s.name_count = r.read_int32()
        s.name_offset = r.read_int32()

        # UE 5.4+ (ADD_SOFTOBJECTPATH_LIST, FileVersionUE5 >= 1008) adds
        # soft object paths count/offset to the summary.
        if s.file_version_ue5 >= 1008:
            r.read_int32()  # SoftObjectPathsCount
            r.read_int32()  # SoftObjectPathsOffset

        # LocalizationId is only present when not filter-editor-only.
        # Assets saved from the editor always include it.
        r.read_fstring()  # LocalizationId

        # GatherableTextData (always serialized for versions we care about).
        r.read_int32()  # GatherableTextDataCount
        r.read_int32()  # GatherableTextDataOffset

        s.export_count = r.read_int32()
        s.export_offset = r.read_int32()
        s.import_count = r.read_int32()
        s.import_offset = r.read_int32()

        # UE 5.4+ VERSE_CELLS (FileVersionUE5 >= 1015) adds CellExport/Import.
        if s.file_version_ue5 >= 1015:
            r.read_int32()  # CellExportCount
            r.read_int32()  # CellExportOffset
            r.read_int32()  # CellImportCount
            r.read_int32()  # CellImportOffset

        # UE 5.4+ METADATA_SERIALIZATION_OFFSET (FileVersionUE5 >= 1014)
        if s.file_version_ue5 >= 1014:
            r.read_int32()  # MetaDataOffset

        s.depends_offset = r.read_int32()
        s.soft_package_references_count = r.read_int32()
        s.soft_package_references_offset = r.read_int32()

        # We don't need any fields beyond this point for current tooling
        # (name/import/export/depends are the only offsets we consume).
        # The remaining header fields vary significantly across versions
        # and are not worth parsing unless a caller needs them.

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
