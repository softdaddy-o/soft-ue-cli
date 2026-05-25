"""Tests for structured UMG layout normalization and comparison."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))


def test_normalize_layout_extracts_widget_bounds_and_contract():
    from soft_ue_cli.umg_layout import normalize_layout

    raw = {
        "asset_path": "/Game/UI/WBP_Menu",
        "all_widgets": ["RootCanvas", "StartButton"],
        "widgets": [
            {
                "name": "StartButton",
                "class": "Button",
                "slot": {"offsets": [120, 760, 260, 72], "z_order": 20},
                "visibility": "Visible",
                "render_opacity": 0.75,
            }
        ],
    }

    layout = normalize_layout(raw, canvas_size=[1920, 1080])

    widget = layout["widgets"][0]
    assert widget["name"] == "StartButton"
    assert widget["bounds"] == [120, 760, 260, 72]
    assert widget["normalized_bounds"] == [0.0625, 0.7037, 0.1354, 0.0667]
    assert widget["z_order"] == 20
    assert widget["opacity"] == 0.75


def test_compare_layouts_reports_bounds_z_order_and_opacity_deltas():
    from soft_ue_cli.umg_layout import compare_layouts

    expected = {
        "canvas_size": [1920, 1080],
        "widgets": [
            {
                "name": "StartButton",
                "normalized_bounds": [0.1, 0.7, 0.15, 0.08],
                "z_order": 10,
                "opacity": 1.0,
            }
        ],
    }
    actual = {
        "canvas_size": [1920, 1080],
        "widgets": [
            {
                "name": "StartButton",
                "normalized_bounds": [0.2, 0.7, 0.15, 0.08],
                "z_order": 2,
                "opacity": 0.5,
            }
        ],
    }

    report = compare_layouts(expected, actual, bounds_tolerance=0.02)

    assert report["success"] is False
    assert report["summary"]["matched_widgets"] == 1
    assert report["deltas"][0]["kind"] == "bounds"
    assert {finding["kind"] for finding in report["findings"]} >= {"bounds", "z_order", "opacity"}


def test_compare_layouts_round_trips_json_files(tmp_path):
    from soft_ue_cli.umg_layout import compare_layouts

    expected = tmp_path / "expected.json"
    actual = tmp_path / "actual.json"
    expected.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")
    actual.write_text(json.dumps({"widgets": [{"name": "A", "normalized_bounds": [0, 0, 1, 1]}]}), encoding="utf-8")

    report = compare_layouts(json.loads(expected.read_text()), json.loads(actual.read_text()))

    assert report["success"] is True
    assert report["summary"]["missing_widgets"] == 0


def test_compare_layouts_subset_ignores_decorative_actual_widgets():
    from soft_ue_cli.umg_layout import compare_layouts

    expected = {
        "widgets": [
            {"name": "PrimaryCTA", "normalized_bounds": [0.4, 0.8, 0.2, 0.08]},
        ],
    }
    actual = {
        "widgets": [
            {"name": "PrimaryCTA", "normalized_bounds": [0.4, 0.8, 0.2, 0.08]},
            {"name": "DecorativeGlow", "normalized_bounds": [0.35, 0.75, 0.3, 0.16]},
        ],
    }

    report = compare_layouts(expected, actual, subset=True)

    assert report["success"] is True
    assert report["summary"]["extra_widgets"] == 0
    assert report["summary"]["ignored_extra_widgets"] == 1


def test_extract_concept_image_layout_detects_foreground_regions(tmp_path):
    from PIL import Image, ImageDraw

    from soft_ue_cli.umg_layout import extract_concept_image_layout

    image_path = tmp_path / "concept.png"
    image = Image.new("RGB", (100, 80), (10, 10, 10))
    draw = ImageDraw.Draw(image)
    draw.rectangle((20, 10, 59, 29), fill=(240, 240, 240))
    image.save(image_path)

    layout = extract_concept_image_layout(str(image_path), min_region_area=50)

    assert layout["source_type"] == "concept-image"
    assert layout["canvas_size"] == [100, 80]
    assert layout["widgets"][0]["role"] == "region"
    assert layout["widgets"][0]["bounds"] == [20, 10, 40, 20]
    assert layout["widgets"][0]["normalized_bounds"] == [0.2, 0.125, 0.4, 0.25]


def test_fit_layout_to_spec_adjusts_canvas_slot_position():
    from soft_ue_cli.umg_layout import fit_layout_to_spec

    concept = {
        "canvas_size": [1000, 500],
        "widgets": [{"name": "Title", "bounds": [120, 50, 300, 40]}],
    }
    actual = {
        "canvas_size": [1000, 500],
        "widgets": [{"name": "Title", "bounds": [100, 60, 300, 40]}],
    }
    spec = {
        "root": {
            "class": "CanvasPanel",
            "name": "Root",
            "children": [
                {
                    "class": "TextBlock",
                    "name": "Title",
                    "slot": {"position": [100, 60], "size": [300, 40]},
                }
            ],
        }
    }

    result = fit_layout_to_spec(concept, actual, spec)

    corrected = result["corrected_spec"]["root"]["children"][0]
    assert corrected["slot"]["position"] == [120, 50]
    assert result["corrections"][0]["delta_position"] == [20, -10]
