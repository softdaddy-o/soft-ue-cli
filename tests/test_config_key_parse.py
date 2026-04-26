"""Tests for config key parsing utilities."""

from __future__ import annotations

from pathlib import Path

import pytest


from soft_ue_cli.config import parse_ini_key


def test_parse_section_and_key():
    section, key = parse_ini_key("[/Script/Engine.RendererSettings]r.DefaultFeature.AutoExposure")
    assert section == "/Script/Engine.RendererSettings"
    assert key == "r.DefaultFeature.AutoExposure"


def test_parse_section_only():
    section, key = parse_ini_key("[/Script/Engine.RendererSettings]")
    assert section == "/Script/Engine.RendererSettings"
    assert key is None


def test_parse_simple_section():
    section, key = parse_ini_key("[Core.System]Paths")
    assert section == "Core.System"
    assert key == "Paths"


def test_parse_no_section_raises():
    with pytest.raises(ValueError, match="section"):
        parse_ini_key("r.DefaultFeature.AutoExposure")
