"""Tests for the inspect-uasset CLI command."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[1]))

from soft_ue_cli.__main__ import build_parser


def test_subcommand_exists():
    parser = build_parser()
    args = parser.parse_args(["inspect-uasset", "test.uasset"])
    assert hasattr(args, "func")


def test_file_path_required():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["inspect-uasset"])


def test_sections_flag():
    parser = build_parser()
    args = parser.parse_args(["inspect-uasset", "test.uasset", "--sections", "summary,properties"])
    assert args.sections == "summary,properties"


def test_format_flag():
    parser = build_parser()
    args = parser.parse_args(["inspect-uasset", "test.uasset", "--format", "table"])
    assert args.format == "table"


def test_default_format_is_json():
    parser = build_parser()
    args = parser.parse_args(["inspect-uasset", "test.uasset"])
    assert args.format == "json"
