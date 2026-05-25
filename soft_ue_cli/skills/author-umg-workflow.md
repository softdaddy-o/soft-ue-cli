---
name: author-umg-workflow
description: Produce editable UMG WidgetBlueprint screens, named navigation contracts, and PIE interaction verification from a UI concept/spec.
version: 1.0.0
---

# author-umg-workflow

Use this skill when a UI concept must become real editable UMG screens with a stable widget-name contract and runtime interaction checks.

## Workflow

1. Convert the UI concept/spec into one or more `umg designer apply` JSON files.
   - Use stable names for every interactive widget.
   - Prefix navigation buttons with intent, for example `Nav_StartButton`, `Nav_BackButton`.
   - Keep data/action buttons distinct, for example `Item_Weapon_0_Button` and `Action_EquipButton`.
   - Include `WidgetSwitcher` names when the flow stays inside one WidgetBlueprint.

2. Apply and inspect each Designer tree.

```bash
soft-ue-cli umg designer apply /Game/UI/WBP_MainMenu --spec-file main_menu_tree.json --compile --save
soft-ue-cli umg designer inspect /Game/UI/WBP_MainMenu --include-defaults
soft-ue-cli umg layout extract --source designer --asset-path /Game/UI/WBP_MainMenu --output umg_expected_layout.json
```

3. Prepare the stable widget-name contract for a reusable parent class.

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

The command validates named buttons/targets and exposes required widgets as variables. The returned `parent_binding_contract` is the contract a shared parent class should bind in `NativeConstruct`.

4. Verify the workflow in PIE.

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

5. Compare runtime layout and capture the composited PIE viewport for visual comparison when `capture_after` is returned.

```bash
soft-ue-cli umg layout extract --source runtime --root-widget WBP_MainMenu_C_0 --full-geometry --output umg_runtime_layout.json
soft-ue-cli umg layout compare --mode geometry --subset umg_expected_layout.json umg_runtime_layout.json --output umg_layout_report.json
soft-ue-cli capture screenshot --source pie-window --output file --scale 50
soft-ue-cli umg layout compare --mode pixel concept.png captured.png --annotated-output visual-diff.png
```

## Output Contract

The workflow should produce:

- Editable Designer-tree JSON for each WidgetBlueprint.
- `umg_expected_layout.json`, `umg_runtime_layout.json`, and `umg_layout_report.json` when runtime layout can be inspected.
- A navigation JSON contract with button names, target switchers, target indices/widgets, or target widget classes.
- A PIE verification command that checks expected widgets, expected text, clickability, and active widget/class assertions.
- Optional composited viewport capture and visual diff evidence after interaction checks pass.
