"""Tests for offline UMG screenshot comparison."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


from soft_ue_cli.visual_compare import compare_umg_screenshots


def test_compare_identical_images_scores_one(tmp_path):
    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    Image.new("RGB", (12, 10), (30, 60, 90)).save(reference)
    Image.new("RGB", (12, 10), (30, 60, 90)).save(captured)

    result = compare_umg_screenshots(reference, captured)

    assert result["success"] is True
    assert result["similarity_score"] == 1.0
    assert result["mean_absolute_error"] == 0.0
    assert result["brightness_delta"] == 0.0
    assert len(result["layout_regions"]) == 9
    assert result["suggested_adjustments"] == []


def test_compare_can_crop_captured_image_to_viewport(tmp_path):
    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    Image.new("RGB", (4, 4), (10, 20, 30)).save(reference)
    image = Image.new("RGB", (8, 8), (200, 0, 0))
    for y in range(2, 6):
        for x in range(2, 6):
            image.putpixel((x, y), (10, 20, 30))
    image.save(captured)

    result = compare_umg_screenshots(reference, captured, crop=(2, 2, 4, 4))

    assert result["captured"]["crop"] == [2, 2, 4, 4]
    assert result["comparison_width"] == 4
    assert result["comparison_height"] == 4
    assert result["similarity_score"] == 1.0


def test_compare_writes_annotated_diff(tmp_path):
    reference = tmp_path / "reference.png"
    captured = tmp_path / "captured.png"
    annotated = tmp_path / "diff.png"
    Image.new("RGB", (6, 6), (0, 0, 0)).save(reference)
    Image.new("RGB", (6, 6), (255, 255, 255)).save(captured)

    result = compare_umg_screenshots(reference, captured, annotated_output=annotated)

    assert result["success"] is True
    assert result["annotated_diff_path"] == str(annotated)
    assert annotated.exists()
    assert result["similarity_score"] == 0.0
    assert result["suggested_adjustments"]
