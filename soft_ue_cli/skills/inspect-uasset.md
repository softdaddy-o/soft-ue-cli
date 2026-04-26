---
name: inspect-uasset
description: Inspect a local Blueprint .uasset file offline without a running editor
version: 1.0.0
---

# Inspect UAsset

Inspect a local Blueprint `.uasset` file without launching Unreal Editor or the SoftUEBridge plugin.

This workflow is useful when you need a quick summary in CI, while debugging assets on disk, or when
the editor is unavailable. It is intentionally more conservative than `query-blueprint`: some sections
may be `partial` or `unavailable` because the parser reads package data directly instead of asking UE.

## Use It For

- Quick Blueprint summaries from disk
- Offline review of variables, functions, components, and events
- Batch inspection scripts in local tooling or CI

## Do Not Use It For

- Default object values or component overrides
- Full graph topology or node-to-node wiring
- Assets that only exist in cooked containers

## Commands

Quick summary:

```bash
soft-ue-cli inspect-uasset D:/Project/Content/Blueprints/BP_Character.uasset
```

All supported sections:

```bash
soft-ue-cli inspect-uasset D:/Project/Content/Blueprints/BP_Character.uasset --sections all
```

Human-readable output:

```bash
soft-ue-cli inspect-uasset D:/Project/Content/Blueprints/BP_Character.uasset --sections variables,functions --format table
```

## Output Notes

- `summary` is the default section
- Each detailed section returns a `fidelity` field
- `partial` means the parser found useful metadata but not the full UE runtime view
- `unavailable` means that section could not be extracted safely from the package layout

## Version Expectations

The command targets uncooked editor packages and expects standard `.uasset` data, with `.uexp`
used automatically when it exists beside the asset.
