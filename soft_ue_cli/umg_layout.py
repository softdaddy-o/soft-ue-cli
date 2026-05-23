"""Structured UMG layout normalization and comparison helpers."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path
from typing import Any


def _round4(value: float) -> float:
    return round(float(value), 4)


def _round_layout(value: float) -> int | float:
    rounded = round(float(value), 4)
    return int(rounded) if rounded.is_integer() else rounded


def _as_float_list(value: Any, length: int) -> list[float] | None:
    if not isinstance(value, (list, tuple)) or len(value) != length:
        return None
    try:
        return [float(part) for part in value]
    except (TypeError, ValueError):
        return None


def _widget_name(widget: dict[str, Any]) -> str:
    return str(widget.get("path") or widget.get("name") or widget.get("widget_name") or "")


def _extract_bounds(widget: dict[str, Any]) -> list[float] | None:
    for key in ("bounds", "absolute_bounds", "normalized_bounds"):
        bounds = _as_float_list(widget.get(key), 4)
        if bounds is not None:
            return bounds

    geometry = widget.get("geometry")
    if isinstance(geometry, dict):
        position = _as_float_list(geometry.get("absolute_position") or geometry.get("position"), 2)
        size = _as_float_list(geometry.get("local_size") or geometry.get("size"), 2)
        if position and size:
            return [position[0], position[1], size[0], size[1]]

    slot = widget.get("slot")
    if isinstance(slot, dict):
        offsets = slot.get("offsets")
        if isinstance(offsets, dict):
            try:
                return [
                    float(offsets.get("left", 0.0)),
                    float(offsets.get("top", 0.0)),
                    float(offsets.get("right", 0.0)),
                    float(offsets.get("bottom", 0.0)),
                ]
            except (TypeError, ValueError):
                return None
        return _as_float_list(offsets, 4)

    return None


def _normalize_bounds(bounds: list[float] | None, canvas_size: list[float]) -> list[float] | None:
    if bounds is None:
        return None
    width = canvas_size[0] or 1.0
    height = canvas_size[1] or 1.0
    return [_round4(bounds[0] / width), _round4(bounds[1] / height), _round4(bounds[2] / width), _round4(bounds[3] / height)]


def _absolute_bounds(widget: dict[str, Any], canvas_size: list[float]) -> list[float] | None:
    bounds = _as_float_list(widget.get("bounds") or widget.get("absolute_bounds"), 4)
    if bounds is not None:
        return bounds

    normalized = _as_float_list(widget.get("normalized_bounds"), 4)
    if normalized is not None:
        return [
            normalized[0] * canvas_size[0],
            normalized[1] * canvas_size[1],
            normalized[2] * canvas_size[0],
            normalized[3] * canvas_size[1],
        ]

    return _extract_bounds(widget)


def _iter_widgets(raw: Any) -> list[dict[str, Any]]:
    if isinstance(raw, dict):
        widgets = raw.get("widgets") or raw.get("runtime_widgets") or raw.get("designer_widgets")
        if isinstance(widgets, list):
            return [widget for widget in widgets if isinstance(widget, dict)]
        root = raw.get("root") or raw.get("root_widget")
        if isinstance(root, dict):
            collected: list[dict[str, Any]] = []

            def visit(node: dict[str, Any]) -> None:
                collected.append(node)
                children = node.get("children")
                if isinstance(children, list):
                    for child in children:
                        if isinstance(child, dict):
                            visit(child)

            visit(root)
            return collected
    return []


def normalize_layout(raw: dict[str, Any], canvas_size: list[int] | None = None) -> dict[str, Any]:
    """Convert common UMG inspection payloads into canonical layout JSON."""
    canvas = [float(part) for part in (canvas_size or raw.get("canvas_size") or [1920, 1080])]
    widgets: list[dict[str, Any]] = []

    for widget in _iter_widgets(raw):
        name = _widget_name(widget)
        if not name:
            continue
        bounds = _extract_bounds(widget)
        normalized_bounds = widget.get("normalized_bounds")
        if _as_float_list(normalized_bounds, 4) is None:
            normalized_bounds = _normalize_bounds(bounds, canvas)

        entry: dict[str, Any] = {
            "name": name,
            "class": widget.get("class") or widget.get("type") or widget.get("widget_class"),
        }
        if bounds is not None:
            entry["bounds"] = [_round4(part) for part in bounds]
        if normalized_bounds is not None:
            entry["normalized_bounds"] = [_round4(part) for part in normalized_bounds]
        if "z_order" in widget:
            entry["z_order"] = int(widget["z_order"])
        elif isinstance(widget.get("slot"), dict) and "z_order" in widget["slot"]:
            entry["z_order"] = int(widget["slot"]["z_order"])
        if "visibility" in widget:
            entry["visibility"] = widget["visibility"]
        if "opacity" in widget:
            entry["opacity"] = float(widget["opacity"])
        elif "render_opacity" in widget:
            entry["opacity"] = float(widget["render_opacity"])
        if "text" in widget:
            entry["text"] = widget["text"]
        widgets.append(entry)

    return {
        "schema": "soft-ue.umg-layout.v1",
        "canvas_size": [int(canvas[0]), int(canvas[1])],
        "source_type": raw.get("source_type") or raw.get("source_kind"),
        "source": raw.get("asset_path") or raw.get("world_name") or raw.get("source"),
        "widgets": widgets,
    }


def _index_widgets(layout: dict[str, Any]) -> dict[str, dict[str, Any]]:
    normalized = normalize_layout(layout, layout.get("canvas_size")) if "widgets" not in layout else layout
    return {_widget_name(widget): widget for widget in normalized.get("widgets", []) if _widget_name(widget)}


def _ignore_names(ignore_masks: list[Any] | None) -> set[str]:
    names: set[str] = set()
    for mask in ignore_masks or []:
        if isinstance(mask, str):
            names.add(mask)
        elif isinstance(mask, dict):
            name = mask.get("widget") or mask.get("name") or mask.get("path")
            if name:
                names.add(str(name))
    return names


def compare_layouts(
    expected: dict[str, Any],
    actual: dict[str, Any],
    *,
    bounds_tolerance: float = 0.02,
    opacity_tolerance: float = 0.05,
    subset: bool = False,
    ignore_masks: list[Any] | None = None,
) -> dict[str, Any]:
    """Compare expected and actual canonical UMG layout JSON."""
    expected_widgets = _index_widgets(expected)
    actual_widgets = _index_widgets(actual)
    ignored_names = _ignore_names(ignore_masks)
    compared_expected_names = set(expected_widgets) - ignored_names

    findings: list[dict[str, Any]] = []
    deltas: list[dict[str, Any]] = []

    for name, expected_widget in expected_widgets.items():
        if name in ignored_names:
            continue
        actual_widget = actual_widgets.get(name)
        if not actual_widget:
            findings.append({"kind": "missing_widget", "widget": name, "message": "Expected widget is missing"})
            continue

        expected_bounds = _as_float_list(expected_widget.get("normalized_bounds"), 4)
        actual_bounds = _as_float_list(actual_widget.get("normalized_bounds"), 4)
        if expected_bounds and actual_bounds:
            delta = [_round4(abs(a - b)) for a, b in zip(expected_bounds, actual_bounds)]
            if max(delta) > bounds_tolerance:
                deltas.append({"kind": "bounds", "widget": name, "delta": delta})
                findings.append({"kind": "bounds", "widget": name, "message": "Normalized bounds exceed tolerance"})

        for key in ("z_order", "visibility", "text"):
            if key in expected_widget and key in actual_widget and expected_widget[key] != actual_widget[key]:
                findings.append({
                    "kind": key,
                    "widget": name,
                    "expected": expected_widget[key],
                    "actual": actual_widget[key],
                })

        if "opacity" in expected_widget and "opacity" in actual_widget:
            opacity_delta = abs(float(expected_widget["opacity"]) - float(actual_widget["opacity"]))
            if opacity_delta > opacity_tolerance:
                findings.append({
                    "kind": "opacity",
                    "widget": name,
                    "expected": expected_widget["opacity"],
                    "actual": actual_widget["opacity"],
                    "delta": _round4(opacity_delta),
                })

    extra_widgets = sorted((set(actual_widgets) - set(expected_widgets)) - ignored_names)
    ignored_extra_widgets = 0
    if subset:
        ignored_extra_widgets = len(extra_widgets)
        extra_widgets = []
    for name in extra_widgets:
        findings.append({"kind": "extra_widget", "widget": name, "message": "Actual layout has an extra widget"})

    return {
        "success": not findings,
        "summary": {
            "expected_widgets": len(expected_widgets),
            "actual_widgets": len(actual_widgets),
            "matched_widgets": len(compared_expected_names & set(actual_widgets)),
            "missing_widgets": len(compared_expected_names - set(actual_widgets)),
            "extra_widgets": len(extra_widgets),
            "ignored_extra_widgets": ignored_extra_widgets,
            "ignored_widgets": len(ignored_names),
        },
        "deltas": deltas,
        "findings": findings,
    }


def extract_concept_image_layout(
    image_path: str,
    *,
    min_region_area: int = 64,
    color_tolerance: int = 24,
    alpha_threshold: int = 16,
) -> dict[str, Any]:
    """Extract coarse foreground regions from a concept image into layout JSON."""
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - exercised only in minimal installs
        raise RuntimeError("Pillow is required for concept-image layout extraction") from exc

    path = Path(image_path)
    image = Image.open(path).convert("RGBA")
    width, height = image.size
    pixels = image.load()
    background = pixels[0, 0]
    visited: set[tuple[int, int]] = set()
    widgets: list[dict[str, Any]] = []

    def is_foreground(x: int, y: int) -> bool:
        pixel = pixels[x, y]
        if pixel[3] < alpha_threshold:
            return False
        diff = abs(pixel[0] - background[0]) + abs(pixel[1] - background[1]) + abs(pixel[2] - background[2])
        return diff > color_tolerance

    for y in range(height):
        for x in range(width):
            if (x, y) in visited or not is_foreground(x, y):
                continue

            stack = [(x, y)]
            visited.add((x, y))
            min_x = max_x = x
            min_y = max_y = y
            area = 0

            while stack:
                cx, cy = stack.pop()
                area += 1
                min_x = min(min_x, cx)
                max_x = max(max_x, cx)
                min_y = min(min_y, cy)
                max_y = max(max_y, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if nx < 0 or ny < 0 or nx >= width or ny >= height or (nx, ny) in visited:
                        continue
                    if is_foreground(nx, ny):
                        visited.add((nx, ny))
                        stack.append((nx, ny))

            if area < min_region_area:
                continue

            bounds = [min_x, min_y, max_x - min_x + 1, max_y - min_y + 1]
            widgets.append({
                "name": f"ConceptRegion{len(widgets) + 1}",
                "class": "ConceptRegion",
                "role": "region",
                "bounds": bounds,
                "normalized_bounds": _normalize_bounds([float(part) for part in bounds], [float(width), float(height)]),
                "z_order": len(widgets),
                "opacity": 1.0,
            })

    return {
        "schema": "soft-ue.umg-layout.v1",
        "source_type": "concept-image",
        "source": str(path),
        "canvas_size": [width, height],
        "widgets": widgets,
    }


def normalize_figma_layout(raw: dict[str, Any]) -> dict[str, Any]:
    """Map common Figma/Stitch layer JSON into the normalized layout schema."""
    widgets: list[dict[str, Any]] = []
    canvas_size = raw.get("canvas_size") or raw.get("document", {}).get("size") or [1920, 1080]
    canvas = [float(canvas_size[0]), float(canvas_size[1])]

    def visit(node: dict[str, Any], z_order: int = 0) -> None:
        name = str(node.get("name") or node.get("id") or f"Layer{len(widgets) + 1}")
        bounds = node.get("absoluteBoundingBox") or node.get("absolute_bounds") or node.get("bounds")
        if isinstance(bounds, dict):
            extracted_bounds = [
                float(bounds.get("x", 0.0)),
                float(bounds.get("y", 0.0)),
                float(bounds.get("width", 0.0)),
                float(bounds.get("height", 0.0)),
            ]
        else:
            extracted_bounds = _as_float_list(bounds, 4)

        entry: dict[str, Any] = {
            "name": name,
            "class": node.get("type") or node.get("class") or "Layer",
            "role": str(node.get("role") or node.get("type") or "layer").lower(),
            "z_order": z_order,
        }
        if extracted_bounds is not None:
            entry["bounds"] = [_round_layout(part) for part in extracted_bounds]
            entry["normalized_bounds"] = _normalize_bounds(extracted_bounds, canvas)
        if "characters" in node:
            entry["text"] = node["characters"]
        elif "text" in node:
            entry["text"] = node["text"]
        if "opacity" in node:
            entry["opacity"] = float(node["opacity"])
        widgets.append(entry)

        children = node.get("children")
        if isinstance(children, list):
            for child in children:
                if isinstance(child, dict):
                    visit(child, len(widgets))

    roots = raw.get("children") or raw.get("layers")
    if isinstance(roots, list):
        for root in roots:
            if isinstance(root, dict):
                visit(root, len(widgets))
    else:
        visit(raw)

    return {
        "schema": "soft-ue.umg-layout.v1",
        "source_type": "figma",
        "source": raw.get("name") or raw.get("id") or raw.get("source"),
        "canvas_size": [int(canvas[0]), int(canvas[1])],
        "widgets": widgets,
    }


def fit_layout_to_spec(
    concept: dict[str, Any],
    actual: dict[str, Any],
    spec: dict[str, Any],
) -> dict[str, Any]:
    """Adjust an apply-widget-tree spec using concept-vs-runtime geometry deltas."""
    concept_layout = normalize_layout(concept, concept.get("canvas_size")) if "widgets" not in concept else concept
    actual_layout = normalize_layout(actual, actual.get("canvas_size")) if "widgets" not in actual else actual
    concept_canvas = [float(part) for part in concept_layout.get("canvas_size", [1920, 1080])]
    actual_canvas = [float(part) for part in actual_layout.get("canvas_size", concept_canvas)]
    concept_widgets = _index_widgets(concept_layout)
    actual_widgets = _index_widgets(actual_layout)
    corrected_spec = deepcopy(spec)
    spec_widgets: dict[str, dict[str, Any]] = {}

    def visit_spec(node: dict[str, Any]) -> None:
        name = node.get("name")
        if name:
            spec_widgets[str(name)] = node
        children = node.get("children")
        if isinstance(children, list):
            for child in children:
                if isinstance(child, dict):
                    visit_spec(child)

    root = corrected_spec.get("root") if isinstance(corrected_spec.get("root"), dict) else corrected_spec
    if isinstance(root, dict):
        visit_spec(root)

    corrections: list[dict[str, Any]] = []
    for name, concept_widget in concept_widgets.items():
        actual_widget = actual_widgets.get(name)
        spec_widget = spec_widgets.get(name)
        if not actual_widget or not spec_widget:
            continue
        concept_bounds = _absolute_bounds(concept_widget, concept_canvas)
        actual_bounds = _absolute_bounds(actual_widget, actual_canvas)
        if concept_bounds is None or actual_bounds is None:
            continue

        dx = concept_bounds[0] - actual_bounds[0]
        dy = concept_bounds[1] - actual_bounds[1]
        dw = concept_bounds[2] - actual_bounds[2]
        dh = concept_bounds[3] - actual_bounds[3]
        slot = spec_widget.setdefault("slot", {})
        if not isinstance(slot, dict):
            continue

        position = _as_float_list(slot.get("position"), 2)
        if position is not None:
            slot["position"] = [_round_layout(position[0] + dx), _round_layout(position[1] + dy)]

        size = _as_float_list(slot.get("size"), 2)
        if size is not None:
            slot["size"] = [_round_layout(size[0] + dw), _round_layout(size[1] + dh)]

        offsets = slot.get("offsets")
        if isinstance(offsets, dict):
            offsets["left"] = _round_layout(float(offsets.get("left", 0.0)) + dx)
            offsets["top"] = _round_layout(float(offsets.get("top", 0.0)) + dy)
            offsets["right"] = _round_layout(float(offsets.get("right", concept_bounds[2])) + dw)
            offsets["bottom"] = _round_layout(float(offsets.get("bottom", concept_bounds[3])) + dh)
        elif _as_float_list(offsets, 4) is not None:
            offset_values = _as_float_list(offsets, 4) or [0.0, 0.0, 0.0, 0.0]
            slot["offsets"] = [
                _round_layout(offset_values[0] + dx),
                _round_layout(offset_values[1] + dy),
                _round_layout(offset_values[2] + dw),
                _round_layout(offset_values[3] + dh),
            ]

        corrections.append({
            "widget": name,
            "delta_position": [_round_layout(dx), _round_layout(dy)],
            "delta_size": [_round_layout(dw), _round_layout(dh)],
        })

    return {
        "success": True,
        "corrections": corrections,
        "corrected_spec": corrected_spec,
    }
