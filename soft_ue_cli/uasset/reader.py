"""Binary reader for Unreal Engine package primitives."""

from __future__ import annotations

import struct
from typing import BinaryIO, Callable, Sequence


class UAssetReader:
    """Wrap a binary stream with little-endian UE-oriented read helpers."""

    def __init__(self, stream: BinaryIO) -> None:
        self._stream = stream

    def tell(self) -> int:
        return self._stream.tell()

    def seek(self, offset: int) -> None:
        self._stream.seek(offset)

    def skip(self, count: int) -> None:
        self._stream.seek(count, 1)

    def read_bytes(self, count: int) -> bytes:
        offset = self.tell()
        data = self._stream.read(count)
        if len(data) != count:
            raise EOFError(f"Expected {count} bytes at offset 0x{offset:X}, got {len(data)}")
        return data

    def read_int32(self) -> int:
        return struct.unpack("<i", self.read_bytes(4))[0]

    def read_uint32(self) -> int:
        return struct.unpack("<I", self.read_bytes(4))[0]

    def read_int64(self) -> int:
        return struct.unpack("<q", self.read_bytes(8))[0]

    def read_uint64(self) -> int:
        return struct.unpack("<Q", self.read_bytes(8))[0]

    def read_uint16(self) -> int:
        return struct.unpack("<H", self.read_bytes(2))[0]

    def read_float(self) -> float:
        return struct.unpack("<f", self.read_bytes(4))[0]

    def read_double(self) -> float:
        return struct.unpack("<d", self.read_bytes(8))[0]

    def read_bool(self) -> bool:
        return self.read_bytes(1)[0] != 0

    def read_fstring(self) -> str:
        length = self.read_int32()
        if length == 0:
            return ""
        if length < 0:
            char_count = -length
            data = self.read_bytes(char_count * 2)
            return data[: (char_count - 1) * 2].decode("utf-16-le", errors="replace")
        data = self.read_bytes(length)
        return data[: length - 1].decode("utf-8", errors="replace")

    def read_fguid(self) -> tuple[int, int, int, int]:
        return (
            self.read_uint32(),
            self.read_uint32(),
            self.read_uint32(),
            self.read_uint32(),
        )

    def read_fname(
        self,
        resolver: Sequence[str] | Callable[[int], str],
    ) -> tuple[str, int]:
        index = self.read_int32()
        number = self.read_int32()
        return (_resolve_name(resolver, index), number)

    def read_mapped_name(
        self,
        resolver: Sequence[str] | Callable[[int], str],
    ) -> tuple[str, int]:
        raw = self.read_uint64()
        index = raw & 0xFFFFFFFF
        number = (raw >> 32) & 0xFFFFFFFF
        return (_resolve_name(resolver, int(index)), int(number))


def _resolve_name(resolver: Sequence[str] | Callable[[int], str], index: int) -> str:
    if callable(resolver):
        return resolver(index)
    if 0 <= index < len(resolver):
        return resolver[index]
    return f"<invalid_name_{index}>"
