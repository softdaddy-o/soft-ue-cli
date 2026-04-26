"""Tests for config CLI command registration."""

from __future__ import annotations

from pathlib import Path

import pytest


from soft_ue_cli.__main__ import build_parser


def test_config_tree_subcommand():
    args = build_parser().parse_args(["config", "tree"])
    assert hasattr(args, "func")
    assert args.config_action == "tree"


def test_config_tree_format_flag():
    assert build_parser().parse_args(["config", "tree", "--format", "ini"]).config_format == "ini"


def test_config_tree_type_flag():
    assert build_parser().parse_args(["config", "tree", "--type", "Engine"]).config_type == "Engine"


def test_config_tree_exists_only_flag():
    assert build_parser().parse_args(["config", "tree", "--exists-only"]).exists_only is True


def test_config_get_subcommand():
    args = build_parser().parse_args(["config", "get", "[S]K"])
    assert args.config_action == "get"
    assert args.key == "[S]K"


def test_config_get_layer_flag():
    assert build_parser().parse_args(["config", "get", "[S]K", "--layer", "ProjectDefault"]).layer == "ProjectDefault"


def test_config_get_trace_flag():
    assert build_parser().parse_args(["config", "get", "[S]K", "--trace"]).trace is True


def test_config_get_source_flag():
    assert build_parser().parse_args(["config", "get", "EngineAssociation", "--source", "project"]).source == "project"


def test_config_get_search_flag():
    assert build_parser().parse_args(["config", "get", "--search", "Bloom"]).search == "Bloom"


def test_config_set_subcommand():
    args = build_parser().parse_args(["config", "set", "[S]K", "V", "--layer", "ProjectDefault"])
    assert args.config_action == "set"
    assert args.key == "[S]K"
    assert args.value == "V"
    assert args.layer == "ProjectDefault"


def test_config_diff_subcommand():
    args = build_parser().parse_args(["config", "diff", "--audit"])
    assert args.config_action == "diff"
    assert args.audit is True


def test_config_audit_subcommand():
    assert build_parser().parse_args(["config", "audit"]).config_action == "audit"


def test_config_requires_subcommand():
    with pytest.raises(SystemExit):
        build_parser().parse_args(["config"])
