"""Tests for soft_ue_cli.uasset.reader."""

from __future__ import annotations

import io
import struct
from pathlib import Path

import pytest


from soft_ue_cli.uasset.reader import UAssetReader


def test_read_int32_positive():
    reader = UAssetReader(io.BytesIO(struct.pack("<i", 42)))
    assert reader.read_int32() == 42


def test_read_int32_negative():
    reader = UAssetReader(io.BytesIO(struct.pack("<i", -1)))
    assert reader.read_int32() == -1


def test_read_uint32():
    reader = UAssetReader(io.BytesIO(struct.pack("<I", 0xDEADBEEF)))
    assert reader.read_uint32() == 0xDEADBEEF


def test_read_int64():
    reader = UAssetReader(io.BytesIO(struct.pack("<q", 2**40)))
    assert reader.read_int64() == 2**40


def test_read_uint64():
    reader = UAssetReader(io.BytesIO(struct.pack("<Q", 2**60)))
    assert reader.read_uint64() == 2**60


def test_read_float():
    reader = UAssetReader(io.BytesIO(struct.pack("<f", 3.14)))
    assert abs(reader.read_float() - 3.14) < 0.001


def test_read_fstring_ascii():
    data = struct.pack("<i", 6) + b"Hello\x00"
    reader = UAssetReader(io.BytesIO(data))
    assert reader.read_fstring() == "Hello"


def test_read_fstring_empty():
    reader = UAssetReader(io.BytesIO(struct.pack("<i", 0)))
    assert reader.read_fstring() == ""


def test_read_fstring_utf16():
    text = "Hi"
    encoded = text.encode("utf-16-le") + b"\x00\x00"
    data = struct.pack("<i", -(len(text) + 1)) + encoded
    reader = UAssetReader(io.BytesIO(data))
    assert reader.read_fstring() == "Hi"


def test_read_fguid():
    expected = (0x12345678, 0x9ABCDEF0, 0x11223344, 0x55667788)
    reader = UAssetReader(io.BytesIO(struct.pack("<IIII", *expected)))
    assert reader.read_fguid() == expected


def test_tell_and_seek():
    data = b"\x00" * 16 + struct.pack("<i", 99)
    reader = UAssetReader(io.BytesIO(data))
    assert reader.tell() == 0
    reader.seek(16)
    assert reader.read_int32() == 99


def test_read_past_end_raises():
    reader = UAssetReader(io.BytesIO(b"\x00\x00"))
    with pytest.raises(EOFError):
        reader.read_int32()
