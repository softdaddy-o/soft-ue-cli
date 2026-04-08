---
type: skill
name: level-from-image
description: Populate a UE level with existing project assets based on a reference image
version: 1.0.0
tags: [level, placement, image, scene, layout, dressing, environment]
required_tools: [query-asset, batch-spawn-actors, batch-modify-actors, batch-delete-actors, query-level, set-viewport-camera, capture-viewport]
options:
  - name: iterate
    description: Enter refinement loop after initial placement
    default: true
  - name: max_auto_passes
    description: Maximum autonomous correction passes before switching to human feedback
    default: 3
---

## Overview

Analyze a reference image (concept art, photo, or screenshot) and populate the current UE level
with matching static mesh assets from the project. All placed actors are labeled with the `LFI_`
prefix for easy tracking and cleanup.

When the user provides an image and asks to populate, fill, dress, or build a level from it,
follow the steps below.

## Step 1: Analyze the Reference Image

Study the provided image and identify:
- **Scene elements**: trees, rocks, buildings, fences, props, vegetation, etc.
- **Estimated counts** per element type
- **Spatial zones**: foreground/midground/background, left/center/right
- **Mood and density**: sparse forest, dense urban, scattered desert, etc.
- **Camera angle**: determine the viewing angle of the reference image (top-down, isometric,
  perspective with estimated pitch/yaw). This angle will be used for feedback screenshots.

Present findings as a structured list:

```
Scene Analysis:
- Pine trees: ~8, mostly midground-left and background
- Large rocks: ~5, foreground-right cluster
- Grass clumps: ~12, scattered foreground
- Wooden fence: ~3, midground-center line
- Camera angle: isometric (~-45 pitch, ~45 yaw)
```

## Step 2: Discover Project Assets

Search for matching assets using query-asset:

```
soft-ue-cli query-asset --class StaticMesh --query "*tree*"
soft-ue-cli query-asset --class StaticMesh --query "*rock*"
soft-ue-cli query-asset --class StaticMesh --query "*grass*"
```

For each scene element, find the best matching StaticMesh in the project.
If no match is found, inform the user and skip that element.

## Step 3: Present Asset Mapping

Show the user a mapping table and ask for confirmation:

```
Scene Element  | Count | Asset                           | Alternatives
---------------|-------|---------------------------------|------------------
Pine tree      | 8     | /Game/Meshes/SM_Tree_Pine       | SM_Tree_Oak
Large rock     | 5     | /Game/Meshes/SM_Rock_Large      | SM_Boulder_01
Grass clump    | 12    | /Game/Meshes/SM_Grass_Clump     | SM_Bush_Small
Wooden fence   | 3     | /Game/Meshes/SM_Fence_Wood      | —
```

Wait for user to confirm or adjust before proceeding.

## Step 4: Generate Placement Plan

Based on the image composition, generate world-space coordinates for each actor:
- Map image spatial zones to UE world coordinates
- Add slight random variation to rotation (yaw) for organic elements
- Vary scale slightly (0.8-1.2x) for natural objects like trees and rocks
- Use the `LFI_` prefix for all labels: `LFI_Tree_01`, `LFI_Rock_03`, etc.

## Step 5: Place Actors

Use batch-spawn-actors to place all actors in a single undo transaction:

```
soft-ue-cli batch-spawn-actors --actors '[
  {"class":"StaticMeshActor","mesh":"/Game/Meshes/SM_Tree_Pine","location":[100,200,0],"rotation":[0,45,0],"scale":[1,1,1],"label":"LFI_Tree_01"},
  {"class":"StaticMeshActor","mesh":"/Game/Meshes/SM_Rock_Large","location":[300,-100,0],"rotation":[0,0,0],"scale":[1.5,1.5,1.5],"label":"LFI_Rock_01"}
]'
```

## Step 6: Visual Feedback Loop

After initial placement, enter the visual feedback loop. This has two phases:
autonomous correction (2-3 passes), then human-in-the-loop refinement.

### Phase A: Autonomous Correction (up to 3 passes)

For each pass:

1. **Set viewport camera** to match the reference image angle:

```
# For top-down reference:
soft-ue-cli set-viewport-camera --preset top --ortho-width 8000

# For isometric reference:
soft-ue-cli set-viewport-camera --location 2000,2000,2000 --rotation -45,45,0

# For perspective reference — estimate the angle from the image:
soft-ue-cli set-viewport-camera --location 0,-2000,1000 --rotation -30,90,0
```

2. **Capture a screenshot** from the same angle:

```
soft-ue-cli capture-viewport --source editor --output file
```

3. **Compare the screenshot to the reference image** focusing on spatial layout accuracy:
   - Are objects in roughly the right positions relative to each other?
   - Are proportions correct (e.g., a large building shouldn't be smaller than a tree)?
   - Are there obvious gaps or overlapping clusters?
   - Is the overall shape/footprint of the layout correct?

4. **If corrections are needed**, apply them:

```
# Move misplaced actors
soft-ue-cli batch-modify-actors --modifications '[{"actor":"LFI_Wall_01","location":[new_x,new_y,new_z]}]'

# Remove excess actors
soft-ue-cli batch-delete-actors --actors '["LFI_Extra_01"]'

# Add missing actors
soft-ue-cli batch-spawn-actors --actors '[...]'
```

5. **If layout looks spatially accurate**, exit autonomous phase and move to Phase B.

After 3 autonomous passes (or earlier if converged), always move to Phase B regardless.

### Phase B: Human-in-the-Loop Refinement

1. **Capture a final screenshot** from the reference angle:

```
soft-ue-cli set-viewport-camera --location ... --rotation ...
soft-ue-cli capture-viewport --source editor --output file
```

2. **Present the screenshot to the user** alongside a summary of what was placed:

```
Placed N actors. Here's the current state from the reference angle:
[screenshot]

Reference image for comparison:
[original image]

What would you like to adjust?
```

3. **Wait for user feedback** and translate it into batch operations:
   - **"too many trees on the left"** → query existing LFI_Tree actors, select some on the left side, use batch-delete-actors
   - **"rocks are too small"** → use batch-modify-actors to increase scale
   - **"add bushes near the river"** → use batch-spawn-actors with new entries
   - **"rotate the building"** → use batch-modify-actors with new rotation
   - **"move everything 500 units north"** → batch-modify-actors on all LFI_ actors
   - **"looks good"** or **"done"** → exit loop

4. **After each adjustment, take a new screenshot** and present it:

```
soft-ue-cli set-viewport-camera --location ... --rotation ...
soft-ue-cli capture-viewport --source editor --output file
```

Show the updated screenshot and ask: **"Here's the updated layout. Anything else to adjust?"**

5. Repeat until the user says done.

### Querying current placements

To inspect what's currently placed:

```
soft-ue-cli query-level --search "LFI_*"
```

This returns all skill-placed actors with their transforms, useful for deciding
which actors to modify or delete based on user feedback.

## Batch Actor Tools Reference

This skill relies on three batch tools for efficient level manipulation.
All batch operations are wrapped in a single undo transaction (one Ctrl+Z to revert).

### batch-spawn-actors

Place multiple actors at once. Each entry supports class, mesh, location, rotation, scale, and label.

```
soft-ue-cli batch-spawn-actors --actors '[
  {"class":"StaticMeshActor","mesh":"/Game/Meshes/SM_Rock","location":[100,0,0],"rotation":[0,45,0],"scale":[1.5,1.5,1.5],"label":"LFI_Rock_01"},
  {"class":"PointLight","location":[0,0,200],"label":"LFI_Light_01"}
]'
```

### batch-modify-actors

Modify transforms of existing actors by name or label. Only specified fields are changed.

```
soft-ue-cli batch-modify-actors --modifications '[
  {"actor":"LFI_Rock_01","location":[200,0,0],"scale":[2,2,2]},
  {"actor":"LFI_Light_01","rotation":[0,90,0]}
]'
```

### batch-delete-actors

Delete multiple actors by name or label.

```
soft-ue-cli batch-delete-actors --actors '["LFI_Rock_01","LFI_Light_01"]'
```

## Cleanup

If the user wants to start over, delete all skill-placed actors:

```
soft-ue-cli query-level --search "LFI_*"
```

Collect all actor labels from the result, then:

```
soft-ue-cli batch-delete-actors --actors '["LFI_Tree_01","LFI_Rock_01",...]'
```
