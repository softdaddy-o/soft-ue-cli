---
name: author-umg
description: Produce editable UMG WidgetBlueprint screens, named navigation contracts, PIE verification, and visual/layout comparison from a UI concept or spec.
version: 1.0.0
---

# author-umg

Use this skill when a UI concept must become real editable UMG screens, not a static screenshot. The output should be one or more `umg designer apply` JSON specs, stable widget-name contracts for interaction, and verification commands that prove the layout and navigation work.

## Workflow

1. Identify the target WidgetBlueprint asset path or paths.
2. Treat concept images as a 1920x1080 reference canvas unless the user declares another resolution.
3. Extract a region/bounding-box table for major areas: top nav, side rail, item grid, info panel, preview or drag area, bottom action bar, modals, and floating controls. Include approximate `x`, `y`, `w`, `h`, opacity, z-order, and whether each region should occlude the game scene.
4. Convert those boxes into UMG anchors and offsets before writing widgets. Preserve relative positions, transparent areas, occluding panels, margins, spacing, z-order, font scale, and control density.
5. Convert the hierarchy into a nested `root` object with stable widget names.
   - Prefix navigation buttons with intent, for example `Nav_StartButton`, `Nav_BackButton`.
   - Keep data/action buttons distinct, for example `Item_Weapon_0_Button` and `Action_EquipButton`.
   - Include `WidgetSwitcher` names when the flow stays inside one WidgetBlueprint.
6. Use supported widget classes first: `CanvasPanel`, `Overlay`, `Border`, `SizeBox`, `ScaleBox`, `Image`, `TextBlock`, `Button`, `HorizontalBox`, `VerticalBox`, `UniformGridPanel`, `GridPanel`, `ScrollBox`, `Spacer`, `WidgetSwitcher`, or a project WidgetBlueprint class path.
7. Put geometry under `slot`, not under widget properties. For a `CanvasPanel` child, use `position`, `size`, `alignment`, `anchors`, `offsets`, `z_order`, and `auto_size`.
8. Put visual/editable values on the widget object: `text`, `font_size`, `justification`, `wrap_at`, `auto_wrap_text`, `visibility`, `render_opacity`, `is_variable`, `tool_tip`, `color`, `tint`, `brush_color`, `background_color`, `padding`, `texture`, `material`, `width_override`, `height_override`, `stretch`, and `active_widget_index`.
9. Use `properties` only for reflected UMG fields not covered by first-class fields.
10. Leave unresolved images as neutral placeholders such as `/Game/UI/Textures/T_UI_Placeholder`, but keep the concept geometry, opacity, and z-order. Track each placeholder in a manifest.

Apply and inspect each Designer tree:

```bash
soft-ue-cli umg designer apply /Game/UI/WBP_MainMenu --spec-file main_menu_tree.json --compile --save
soft-ue-cli umg designer inspect /Game/UI/WBP_MainMenu --include-defaults --depth-limit 8
soft-ue-cli umg layout extract --source designer --asset-path /Game/UI/WBP_MainMenu --output umg_expected_layout.json
```

Prepare a navigation contract when the screen has interactive flow:

```json
[
  {
    "button": "Nav_StartButton",
    "mode": "switcher",
    "switcher": "ScreenSwitcher",
    "target_index": 1,
    "target_widget": "LoadoutPanel"
  },
  {
    "button": "Nav_BackButton",
    "mode": "viewport-replace",
    "target_widget_class": "/Game/UI/WBP_MainMenu.WBP_MainMenu_C"
  }
]
```

```bash
soft-ue-cli umg navigation wire /Game/UI/WBP_MainMenu --bindings-file navigation.json --compile --save
```

The command validates named buttons and targets, exposes required widgets as variables, and returns a `parent_binding_contract` a shared parent class can bind in `NativeConstruct`.

Verify the workflow in PIE when runtime interaction can be tested:

```json
[
  {
    "button": "Nav_StartButton",
    "switcher": "ScreenSwitcher",
    "expect_active_index": 1,
    "expect_active_widget": "LoadoutPanel"
  }
]
```

```bash
soft-ue-cli pie-session start --blueprint-error-action report
soft-ue-cli umg preview replace --widget-class /Game/UI/WBP_MainMenu.WBP_MainMenu_C --capture-after
soft-ue-cli umg verify widgets --root-widget WBP_MainMenu_C_0 --expected-widgets '["RootCanvas","Nav_StartButton","ScreenSwitcher","LoadoutPanel"]'
soft-ue-cli umg verify text --root-widget WBP_MainMenu_C_0 --expected-text '["Main Menu"]'
soft-ue-cli umg verify navigation --root-widget WBP_MainMenu_C_0 --click-sequence-file click_sequence.json
```

Compare runtime layout and capture the composited PIE viewport when visual evidence is available:

```bash
soft-ue-cli umg layout extract --source runtime --root-widget WBP_MainMenu_C_0 --full-geometry --output umg_runtime_layout.json
soft-ue-cli umg layout compare --mode geometry --subset umg_expected_layout.json umg_runtime_layout.json --output umg_layout_report.json
soft-ue-cli capture screenshot --source pie-window --output file --scale 50
soft-ue-cli umg layout compare --mode pixel concept.png captured.png --annotated-output visual_diff.png
```

Report visual gaps separately from functional gaps.

## Required Output

- A region/bounding-box table derived from the concept.
- Editable Designer-tree JSON for each WidgetBlueprint.
- A named widget contract for interactive controls.
- A navigation JSON contract when buttons change switchers, screens, or viewport widgets.
- `umg_expected_layout.json`, and when runtime inspection is available, `umg_runtime_layout.json` and `umg_layout_report.json`.
- A PIE verification command sequence that checks expected widgets, expected text, clickability, and active widget/class assertions.
- A placeholder asset manifest listing each unresolved texture, material, icon, font, or project WidgetBlueprint path, the intended final asset, and why the placeholder is acceptable for the current iteration.
- A visual-fidelity checklist covering position, occlusion, opacity, font scale, control density, intentional placeholders, and recommended layout/screenshot comparison evidence.

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
        "name": "Nav_StartButton",
        "is_variable": true,
        "slot": {
          "anchors": {"min": [0.5, 0.5], "max": [0.5, 0.5]},
          "alignment": [0.5, 0.5],
          "position": [0, 0],
          "size": [320, 72],
          "z_order": 20
        },
        "children": [
          {
            "class": "TextBlock",
            "name": "Nav_StartButton_Text",
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
