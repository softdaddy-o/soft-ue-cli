---
name: setup-test-infrastructure
description: Execute a project-specific UE test-infrastructure plan by creating or validating test maps, directories, and module prerequisites after conventions are approved. Use when docs/testing/test-infrastructure.md exists but the infrastructure is not yet created or verified.
version: 1.0.0
---

# setup-test-infrastructure

Use this skill after `plan-test-infrastructure` or whenever `docs/testing/test-infrastructure.md` already exists and the project still needs the infrastructure created on disk.

## Preconditions

Before making changes, read:

1. `docs/testing/test-infrastructure.md`
2. Relevant `*.Build.cs` files for the target test module
3. Any existing test maps or fixtures the plan says should be reused

If the plan document is missing or too vague to execute safely, stop and route back to `plan-test-infrastructure`.

## Execute

Apply the agreed conventions rather than improvising:

1. Create required map assets and folders.
2. Set `WorldSettings` properties such as `DefaultGameMode` on each test map.
3. Create or validate source directories for the planned test categories.
4. Update `*.Build.cs` dependencies only when the plan requires them.
5. Add minimum world fixtures such as `PlayerStart` or NavMesh-related setup if the plan calls for them.
6. Run a focused smoke check that verifies the new infrastructure is usable.

## Verify

Verification should prove the infrastructure is not empty or broken:

- maps exist at the documented paths
- expected `WorldSettings.DefaultGameMode` values are present
- the target test module/build rules still make sense
- PIE or map-open smoke checks succeed for at least one representative map

## Rules

- Follow the committed plan document; do not silently drift from it.
- Keep edits minimal and infrastructure-focused. This skill prepares test scaffolding; it does not write the actual regression test.
- If a requested map or module change implies a broader convention change, stop and send the user back through `plan-test-infrastructure`.
- When finished, summarize what `author-test` can now assume safely.
