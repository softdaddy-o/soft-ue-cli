"""Offline visual comparison helpers for UMG screenshot workflows."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageFilter, ImageStat


Crop = tuple[int, int, int, int]


def _load_rgb(path: Path) -> Image.Image:
    with Image.open(path) as image:
        return image.convert("RGB")


def _validate_crop(crop: Crop, width: int, height: int) -> None:
    x, y, w, h = crop
    if x < 0 or y < 0 or w <= 0 or h <= 0:
        raise ValueError("crop must be X,Y,W,H with non-negative origin and positive size")
    if x + w > width or y + h > height:
        raise ValueError("crop is outside the captured image bounds")


def _mean_rgb(image: Image.Image) -> list[float]:
    stat = ImageStat.Stat(image)
    return [round(channel, 3) for channel in stat.mean[:3]]


def _brightness(image: Image.Image) -> float:
    gray = image.convert("L")
    return float(ImageStat.Stat(gray).mean[0])


def _edge_density(image: Image.Image) -> float:
    edges = image.convert("L").filter(ImageFilter.FIND_EDGES)
    histogram = edges.histogram()
    active = sum(histogram[32:])
    total = image.width * image.height
    return 0.0 if total <= 0 else active / total


def _region_box(width: int, height: int, column: int, row: int) -> tuple[int, int, int, int]:
    left = round(width * column / 3)
    top = round(height * row / 3)
    right = round(width * (column + 1) / 3)
    bottom = round(height * (row + 1) / 3)
    return left, top, right, bottom


def _diff_metrics(reference: Image.Image, captured: Image.Image) -> dict[str, float]:
    diff = ImageChops.difference(reference, captured)
    mean_channels = ImageStat.Stat(diff).mean[:3]
    mae = sum(mean_channels) / 3.0
    similarity = max(0.0, min(1.0, 1.0 - (mae / 255.0)))
    return {
        "mean_absolute_error": round(mae, 6),
        "similarity_score": round(similarity, 6),
    }


def _layout_regions(reference: Image.Image, captured: Image.Image) -> list[dict[str, Any]]:
    regions: list[dict[str, Any]] = []
    for row in range(3):
        for column in range(3):
            box = _region_box(captured.width, captured.height, column, row)
            ref_region = reference.crop(box)
            cap_region = captured.crop(box)
            metrics = _diff_metrics(ref_region, cap_region)
            regions.append(
                {
                    "region": f"r{row}c{column}",
                    "box": list(box),
                    "similarity_score": metrics["similarity_score"],
                    "brightness_delta": round(_brightness(cap_region) - _brightness(ref_region), 3),
                }
            )
    return regions


def _write_annotated_diff(reference: Image.Image, captured: Image.Image, output: Path) -> None:
    diff = ImageChops.difference(reference, captured)
    heat = diff.convert("L")
    heat_rgb = Image.merge("RGB", (heat, Image.new("L", heat.size, 0), Image.new("L", heat.size, 0)))
    overlay = Image.blend(captured, heat_rgb, 0.45)

    margin = 8
    canvas = Image.new(
        "RGB",
        (reference.width * 3 + margin * 4, reference.height + margin * 2),
        (24, 24, 24),
    )
    canvas.paste(reference, (margin, margin))
    canvas.paste(captured, (reference.width + margin * 2, margin))
    canvas.paste(overlay, (reference.width * 2 + margin * 3, margin))
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)


def _suggest_adjustments(
    similarity: float,
    threshold: float,
    brightness_delta: float,
    color_distance: float,
    regions: list[dict[str, Any]],
) -> list[str]:
    suggestions: list[str] = []
    if similarity < threshold:
        suggestions.append("Overall screenshot differs from the reference; inspect layout, visibility, and styling.")
    if brightness_delta > 10.0:
        suggestions.append("Captured screenshot is brighter than the reference.")
    elif brightness_delta < -10.0:
        suggestions.append("Captured screenshot is darker than the reference.")
    if color_distance > 24.0:
        suggestions.append("Dominant color balance differs from the reference.")

    weak_regions = [region for region in regions if region["similarity_score"] < threshold]
    if weak_regions:
        worst = min(weak_regions, key=lambda region: region["similarity_score"])
        suggestions.append(f"Largest regional mismatch is {worst['region']} at box {worst['box']}.")
    return suggestions


def compare_umg_screenshots(
    reference_image: str | Path,
    captured_image: str | Path,
    *,
    crop: Crop | None = None,
    annotated_output: str | Path | None = None,
    threshold: float = 0.9,
) -> dict[str, Any]:
    """Compare a reference concept image against a captured UMG screenshot."""

    reference_path = Path(reference_image)
    captured_path = Path(captured_image)
    reference = _load_rgb(reference_path)
    captured = _load_rgb(captured_path)
    captured_crop: list[int] | None = None

    if crop is not None:
        _validate_crop(crop, captured.width, captured.height)
        x, y, w, h = crop
        captured = captured.crop((x, y, x + w, y + h))
        captured_crop = [x, y, w, h]

    if reference.size != captured.size:
        reference = reference.resize(captured.size, Image.Resampling.LANCZOS)

    metrics = _diff_metrics(reference, captured)
    reference_brightness = _brightness(reference)
    captured_brightness = _brightness(captured)
    brightness_delta = round(captured_brightness - reference_brightness, 3)
    reference_mean = _mean_rgb(reference)
    captured_mean = _mean_rgb(captured)
    delta_rgb = [round(captured_mean[index] - reference_mean[index], 3) for index in range(3)]
    color_distance = round(math.sqrt(sum(delta * delta for delta in delta_rgb)), 3)
    regions = _layout_regions(reference, captured)
    edge_reference = _edge_density(reference)
    edge_captured = _edge_density(captured)

    result: dict[str, Any] = {
        "success": True,
        "reference": {
            "path": str(reference_path),
            "width": reference.width,
            "height": reference.height,
        },
        "captured": {
            "path": str(captured_path),
            "width": captured.width,
            "height": captured.height,
            "crop": captured_crop,
        },
        "comparison_width": captured.width,
        "comparison_height": captured.height,
        "threshold": threshold,
        "similarity_score": metrics["similarity_score"],
        "mean_absolute_error": metrics["mean_absolute_error"],
        "brightness_delta": brightness_delta,
        "dominant_color_delta": {
            "reference_mean_rgb": reference_mean,
            "captured_mean_rgb": captured_mean,
            "delta_rgb": delta_rgb,
            "distance": color_distance,
        },
        "element_presence_delta": {
            "reference_edge_density": round(edge_reference, 6),
            "captured_edge_density": round(edge_captured, 6),
            "delta": round(edge_captured - edge_reference, 6),
        },
        "layout_regions": regions,
    }

    result["suggested_adjustments"] = _suggest_adjustments(
        metrics["similarity_score"],
        threshold,
        brightness_delta,
        color_distance,
        regions,
    )

    if annotated_output is not None:
        output_path = Path(annotated_output)
        _write_annotated_diff(reference, captured, output_path)
        result["annotated_diff_path"] = str(output_path)

    return result
