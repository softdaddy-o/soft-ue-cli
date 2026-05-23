---
name: author-umg-designer
description: Convert a UI concept image plus text requirements into an editable UMG Designer tree JSON spec for apply-widget-tree.
version: 1.0.0
---

# author-umg-designer

Use this skill when a user wants an editable Unreal UMG WidgetBlueprint, not a static screenshot.

The output should be a JSON spec for:

```bash
soft-ue-cli apply-widget-tree /Game/UI/WBP_Name --spec-file widget_tree.json --compile --save
```

## Workflow

1. Identify the target WidgetBlueprint asset path.
2. Treat the concept image as a 1920x1080 reference canvas unless the user declares a different resolution.
3. Extract a region/bounding-box table for major areas: top nav, side rail, item grid, info panel, preview or drag area, bottom action bar, modals, and floating controls. Include approximate `x`, `y`, `w`, `h`, opacity, z-order, and whether each region should occlude the game scene.
4. Convert those boxes into UMG anchors and offsets before writing widgets. Preserve relative positions, transparent areas, occluding panels, margins, spacing, and z-order.
5. Convert the hierarchy into a nested `root` object with stable widget names.
6. Use concept-like font sizes, line density, and control density first. Avoid oversized placeholder typography unless the concept itself uses it.
7. Use supported widget classes first: `CanvasPanel`, `Overlay`, `Border`, `SizeBox`, `ScaleBox`, `Image`, `TextBlock`, `Button`, `HorizontalBox`, `VerticalBox`, `UniformGridPanel`, `GridPanel`, `ScrollBox`, `Spacer`, `WidgetSwitcher`, or a project WidgetBlueprint class path.
8. Put geometry under `slot`, not under widget properties. For a `CanvasPanel` child, use `position`, `size`, `alignment`, `anchors`, `offsets`, `z_order`, and `auto_size`.
9. Put visual/editable values on the widget object: `text`, `font_size`, `justification`, `wrap_at`, `auto_wrap_text`, `visibility`, `render_opacity`, `is_variable`, `tool_tip`, `color`, `tint`, `brush_color`, `background_color`, `padding`, `texture`, `material`, `width_override`, `height_override`, `stretch`, and `active_widget_index`.
10. Use `properties` only for reflected UMG fields not covered by the first-class fields.
11. Leave unresolved images as neutral placeholders such as `/Game/UI/Textures/T_UI_Placeholder`, but keep the concept geometry, opacity, and z-order. Tell the user which assets must be replaced.
12. Before final apply/save, include a visual-fidelity checklist covering position, occlusion, opacity, font scale, control density, and intentional placeholders.
13. After applying the spec, verify designer layout and write a required expected layout artifact:

```bash
soft-ue-cli inspect-widget-blueprint /Game/UI/WBP_Name --include-defaults --depth-limit 8
soft-ue-cli umg-layout extract --source designer --asset-path /Game/UI/WBP_Name --output umg_expected_layout.json
```

14. Keep a placeholder asset manifest for every unresolved texture, material, icon, font, or project-specific WidgetBlueprint. The manifest must list the placeholder path, intended final asset, and why the placeholder is acceptable for the current iteration.
15. When PIE or preview capture is available, run a layout and screenshot comparison loop:

```bash
soft-ue-cli umg-layout extract --source runtime --root-widget WBP_Name_C_0 --full-geometry --output umg_runtime_layout.json
soft-ue-cli umg-layout compare --mode geometry --subset umg_expected_layout.json umg_runtime_layout.json --output umg_layout_report.json
soft-ue-cli capture-pie-screenshot --output rendered.png
soft-ue-cli umg-layout compare --mode pixel concept.png rendered.png --annotated-output visual_diff.png
```

Report visual gaps separately from functional gaps.

## Required Output

- A region/bounding-box table derived from the concept.
- A named widget contract for interactive controls.
- The apply-widget-tree JSON spec.
- `umg_expected_layout.json` derived from the designer hierarchy.
- A placeholder asset manifest for unresolved visual assets.
- A visual-fidelity checklist stating what matches and what is intentionally placeholder.
- A recommended layout and screenshot comparison loop when PIE or preview capture is available, including `umg-layout compare`.

## Minimal Spec

```json
{
  "root": {
    "class": "CanvasPanel",
    "name": "RootCanvas",
    "children": [
      {
        "class": "TextBlock",
        "name": "TitleText",
        "text": "Main Menu",
        "font_size": 42,
        "justification": "center",
        "slot": {
          "anchors": {"min": [0.5, 0.0], "max": [0.5, 0.0]},
          "alignment": [0.5, 0.0],
          "position": [0, 64],
          "size": [640, 80],
          "z_order": 10
        }
      },
      {
        "class": "Button",
        "name": "StartButton",
        "background_color": [0.1, 0.25, 0.6, 1.0],
        "slot": {
          "anchors": {"min": [0.5, 0.5], "max": [0.5, 0.5]},
          "alignment": [0.5, 0.5],
          "position": [0, 0],
          "size": [280, 72]
        },
        "children": [
          {
            "class": "TextBlock",
            "name": "StartButtonLabel",
            "text": "Start",
            "font_size": 28,
            "justification": "center"
          }
        ]
      }
    ]
  }
}
```

## Navigation Notes

For navigation between WidgetBlueprints, include stable button names in the JSON spec and then wire click behavior with project-specific Blueprint graph tooling or a reusable parent-class pattern. Verify graph wiring with `query-blueprint-graph` and designer hierarchy with `inspect-widget-blueprint`.
