---
name: replay-changes
description: Resolve a conflicted Unreal `.uasset` by extracting base/local/remote revisions with Git or Perforce, inspecting offline diffs, syncing the incoming binary, and replaying the wanted local edits manually in Unreal.
version: 1.0.0
---

# replay-changes

Use this skill when a binary Unreal asset has a source-control conflict and you need a safe recovery workflow instead of hand-merging the `.uasset`.

This is a workflow skill. It does not rely on a dedicated `replay-changes` command. Use source control plus the offline Unreal inspection tools that already exist:

- `soft-ue-cli inspect-uasset`
- `soft-ue-cli diff-uasset`
- `git` or `p4`

## When to use

- A `.uasset` is in Git or Perforce conflict and cannot be text-merged.
- You need to preserve local Blueprint edits before accepting the remote version.
- You need a repeatable way to compare `base`, `local`, and `remote` revisions before re-applying edits in the editor.

## Current scope

- extract three comparable binary snapshots
- inspect each snapshot offline
- diff `base -> local`, `base -> remote`, and `remote -> local`
- sync the incoming revision into the workspace
- replay the intended local edits manually in Unreal

This flow is intentionally manual after diff extraction. It does **not** auto-merge or auto-apply binary changes back into the asset.

## Git workflow

### 1. Extract the three revisions

```bash
git show :1:Content/Blueprints/BP_Player.uasset > D:/tmp/BP_Player.base.uasset
git show :2:Content/Blueprints/BP_Player.uasset > D:/tmp/BP_Player.local.uasset
git show :3:Content/Blueprints/BP_Player.uasset > D:/tmp/BP_Player.remote.uasset
```

Stage meaning:

- `:1` = merge base
- `:2` = your local revision
- `:3` = incoming remote revision

### 2. Inspect and diff offline

```bash
soft-ue-cli inspect-uasset D:/tmp/BP_Player.local.uasset --sections summary,variables,functions,components
soft-ue-cli inspect-uasset D:/tmp/BP_Player.remote.uasset --sections summary,variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.base.uasset D:/tmp/BP_Player.local.uasset --sections variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.base.uasset D:/tmp/BP_Player.remote.uasset --sections variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.remote.uasset D:/tmp/BP_Player.local.uasset --sections variables,functions,components
```

Use the final `remote -> local` diff as the replay checklist after you sync the incoming binary.

### 3. Sync the incoming revision into the working tree

After the local revision is safely copied out, accept the incoming file:

```bash
git checkout --theirs -- Content/Blueprints/BP_Player.uasset
git add Content/Blueprints/BP_Player.uasset
```

If the asset has sidecars such as `.uexp` or `.ubulk`, handle them in the same pass.

### 4. Replay the desired local edits in Unreal

- variables that exist only in local
- function graph changes
- component additions or property overrides
- event wiring differences
- anim graph or Blueprint logic differences exposed by the offline diff

Re-run `inspect-uasset` or `diff-uasset` against a fresh snapshot if you need to verify parity as you go.

## Perforce workflow

Perforce does not provide Git-style conflict stages, so create explicit snapshot files before resolving.

### 1. Save base, local, and remote revisions

```bash
p4 print -o D:/tmp/BP_Player.base.uasset //depot/MyGame/Content/Blueprints/BP_Player.uasset#have
p4 print -o D:/tmp/BP_Player.remote.uasset //depot/MyGame/Content/Blueprints/BP_Player.uasset
Copy-Item Content/Blueprints/BP_Player.uasset D:/tmp/BP_Player.local.uasset
```

If you know the exact base changelist or revision number, prefer that instead of `#have`.

### 2. Inspect and diff offline

```bash
soft-ue-cli inspect-uasset D:/tmp/BP_Player.local.uasset --sections summary,variables,functions,components
soft-ue-cli inspect-uasset D:/tmp/BP_Player.remote.uasset --sections summary,variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.base.uasset D:/tmp/BP_Player.local.uasset --sections variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.base.uasset D:/tmp/BP_Player.remote.uasset --sections variables,functions,components
soft-ue-cli diff-uasset D:/tmp/BP_Player.remote.uasset D:/tmp/BP_Player.local.uasset --sections variables,functions,components
```

### 3. Sync the incoming revision

```bash
p4 sync Content/Blueprints/BP_Player.uasset
```

If the file is already in a resolve flow, use the equivalent `p4 resolve` or workspace update sequence that leaves the workspace holding the incoming binary revision before replaying edits.

### 4. Replay the desired local edits in Unreal

Use the `remote -> local` diff as the checklist and manually re-apply the wanted behavior in the editor. Save, compile, and inspect again if needed.

## Recommended practice

- Always capture `base`, `local`, and `remote` before accepting the incoming binary file.
- Keep the extracted files until the replayed asset is compiled and saved cleanly.
- Diff `base -> local` and `base -> remote` first, then `remote -> local` last.
- Treat the offline diff as a replay checklist, not as an auto-merge.

## Anti-patterns

- Accepting `theirs` or running `p4 sync` before saving the local binary revision.
- Treating `diff-uasset` output as a complete semantic merge.
- Replaying everything blindly instead of reconstructing intent section by section.
