---
name: plan-test-infrastructure
description: Scan a UE project and propose a committed test-infrastructure plan before writing project-specific Automation Specs. Use when the project lacks agreed test maps, module layout, or naming conventions for gameplay tests.
version: 1.0.0
---

# plan-test-infrastructure

Use this skill when `author-test` determines the project does not yet have stable testing conventions.

## Goal

Produce a committed planning document at `docs/testing/test-infrastructure.md` that future `author-*` skills can read instead of inventing project-specific conventions ad hoc.

## Scan first

Inspect only the project surfaces that inform reusable test infrastructure:

1. `*.uproject` for module names and plugins.
2. `Source/*Tests*.Build.cs` and existing test modules.
3. `Config/DefaultEngine.ini` for default maps and game modes.
4. Existing map assets matching patterns such as `*Test*`, `*FTEST*`, `*AITest*`, `*Automation*`.
5. Existing `GameMode`, pawn, controller, and character assets or classes.
6. Existing docs such as `docs/testing/`, `AGENTS.md`, or project test strategy pages.

Use local project files and engine/plugin inspection tools. Do not browse the web for this workflow when the repo and project files already answer the question.

## Produce

Write `docs/testing/test-infrastructure.md` with:

- map categories and their target map assets
- required GameMode and other critical world setup notes
- source directory conventions by test category
- spec-prefix and file-naming conventions
- any shared prerequisites such as PlayerStart, NavMesh, spawn points, or fixture directories
- explicit gaps or open decisions that still need user approval

## Output shape

Keep the document concrete and easy for later skills to consume:

```markdown
# Test Infrastructure

## Maps
| Category | Map | GameMode | Notes |
|---|---|---|---|

## Source Layout
| Category | Source Path | Spec Prefix |
|---|---|---|

## Naming
- Files: TEST_<PascalCase>.cpp
- Specs: <Project>.<Category>.<TestName>
- Maps: FTEST_<Category>.umap
```

## Rules

- Prefer reusing proven maps and modules over proposing a brand-new taxonomy.
- Call out risky assumptions explicitly instead of hiding them in the document.
- If the repo already has conventions, normalize and document them rather than replacing them.
- End by stating whether `setup-test-infrastructure` is needed next.
