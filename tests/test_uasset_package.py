"""Tests for soft_ue_cli.uasset.package."""

from __future__ import annotations

import io
import struct
from pathlib import Path

import pytest


from soft_ue_cli.uasset.package import UAssetPackage
from soft_ue_cli.uasset.types import PACKAGE_MAGIC, UAssetError


def _build_minimal_header(
    *,
    magic: int = PACKAGE_MAGIC,
    file_version_ue4: int = 522,
    file_version_ue5: int = 1008,
    name_count: int = 0,
    name_offset: int = 0,
    export_count: int = 0,
    export_offset: int = 0,
    import_count: int = 0,
    import_offset: int = 0,
) -> bytes:
    buf = io.BytesIO()

    def w32(value: int) -> None:
        buf.write(struct.pack("<i", value))

    def wu32(value: int) -> None:
        buf.write(struct.pack("<I", value))

    wu32(magic)
    w32(-7)
    w32(0)
    w32(file_version_ue4)
    w32(file_version_ue5)
    w32(0)
    w32(0)
    w32(0)
    w32(0)
    wu32(0)
    w32(name_count)
    w32(name_offset)
    w32(0)
    w32(0)
    w32(0)
    w32(0)
    w32(export_count)
    w32(export_offset)
    w32(import_count)
    w32(import_offset)
    w32(0)
    w32(0)
    w32(0)
    w32(0)
    w32(0)
    buf.write(b"\x00" * 16)
    buf.write(b"\x00" * 16)
    w32(1)
    w32(export_count)
    w32(name_count)
    wu32(0)
    wu32(0)
    wu32(0)
    wu32(0)
    w32(0)
    wu32(0)
    wu32(0)
    wu32(0)
    wu32(0)
    w32(0)
    wu32(0)
    w32(0)
    wu32(0)
    w32(0)
    w32(0)
    return buf.getvalue()


def test_valid_magic_accepted():
    pkg = UAssetPackage(io.BytesIO(_build_minimal_header()))
    assert pkg.summary.magic == PACKAGE_MAGIC


def test_invalid_magic_raises():
    with pytest.raises(UAssetError, match="magic"):
        UAssetPackage(io.BytesIO(_build_minimal_header(magic=0x0BADF00D)))


def test_ue5_version_parsed():
    pkg = UAssetPackage(io.BytesIO(_build_minimal_header(file_version_ue5=1008)))
    assert pkg.summary.file_version_ue5 == 1008
