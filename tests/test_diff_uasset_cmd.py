"""Tests for the diff-uasset CLI command."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[1]))

from soft_ue_cli.__main__ import build_parser


def test_subcommand_exists():
    parser = build_parser()
    args = parser.parse_args(["diff-uasset", "left.uasset", "right.uasset"])
    assert hasattr(args, "func")


def test_file_paths_required():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["diff-uasset", "left.uasset"])


def test_sections_flag():
    parser = build_parser()
    args = parser.parse_args(["diff-uasset", "left.uasset", "right.uasset", "--sections", "summary,properties"])
    assert args.sections == "summary,properties"


def test_format_flag():
    parser = build_parser()
    args = parser.parse_args(["diff-uasset", "left.uasset", "right.uasset", "--format", "table"])
    assert args.format == "table"
