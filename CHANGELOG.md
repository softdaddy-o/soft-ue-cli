# Changelog

All notable changes to soft-ue-cli will be documented in this file.

## [1.5.0] - 2026-03-25

### Added
- `save-asset` command — save modified assets to disk after mutations, preventing data loss from editor crashes
- `compile-blueprint` command — trigger Blueprint/AnimBlueprint compilation and return status (success, warnings, errors)
- `insert-graph-node` command — atomically insert a new node between two connected nodes with auto pin detection, single undo transaction, and rollback on failure
- `disconnect-graph-pin --target-node --target-pin` — disconnect a specific pin-to-pin connection while preserving other wires (without these flags, existing break-all behavior is unchanged)

## [1.4.0] - 2026-03-24

### Added
- `get-property` command — read UPROPERTY values from runtime actors/components using UE reflection with dot notation for component properties
- `query-level --include-properties` — inspect actor and component property values with optional `--property-filter` wildcard filtering
- `create-asset --skeleton` — dedicated flag for specifying skeleton asset path when creating AnimBlueprints
- `query-blueprint-graph` now returns interface implementation graphs (type `"interface"`) in addition to event, function, and macro graphs

### Fixed
- Phantom asset handling: `create-asset` and `delete-asset` now correctly detect and clean up phantom assets (registry entry with no loadable object)
- `modify-interface add` now auto-generates anim layer function graphs with Root and LinkedInputPose nodes when adding AnimLayerInterface to AnimBlueprints
- `add-graph-node` properties now correctly resolve through inner `Node` struct for animation graph nodes (e.g. AnimGraphNode_SpringBone BoneToModify, SpringStiffness)
- `add-graph-node` now returns `property_warnings` in JSON response instead of silently ignoring invalid properties
- `add-graph-node AnimGraphNode_LinkedAnimLayer` no longer crashes; pins are reconstructed after properties are set

## [1.3.2] - 2026-03-23

### Changed
- Updated supported UE version to 5.7 (active development target)

## [1.3.1] - 2026-03-23

### Fixed
- Plugin now auto-dismisses blocking UE editor dialogs during bridge tool execution, preventing game thread hangs and timeouts
- CLI timeout error message now hints at modal dialogs as a possible cause

### Improved
- `add-graph-node` help text documents `AnimLayerFunction` usage for creating anim layer functions on AnimLayerInterface assets

## [1.3.0] - 2026-03-22

### Changed
- `trigger-live-coding` now waits for compilation by default and returns the actual result (success, failure, cancelled). Use `--no-wait` for fire-and-forget.

### Added
- `build-and-relaunch --wait` — monitors build progress, waits for the editor to come back up, and returns build result with duration. On failure, returns compiler errors directly.

## [1.2.0] - 2026-03-20

### Added
- `inspect-runtime-widgets` command — inspect live UMG widget geometry during PIE sessions
  - Query by widget name or class with keyword search
  - Returns computed geometry (absolute position, local size, accumulated render transform)
  - Includes slot properties and render settings
  - Optional `--include-slate` flag for underlying Slate widget data
  - Supports multiple PIE instances via `--pie-index`

## [1.1.1] - 2026-03-19

### Fixed
- UE5 build error: replaced deprecated `FAssetRegistryModule` with `IAssetRegistry::GetChecked()` in ModifyInterfaceTool
- UE5 build error: fixed `AddFunctionGraph` template deduction failure in AddGraphNodeTool

## [1.1.0] - 2026-03-19

### Added
- `modify-interface` command — add or remove implemented interfaces on Blueprints and AnimBlueprints
- `add-graph-node AnimLayerFunction` — create anim layer function graphs on AnimLayerInterface assets with Root and Input Pose nodes
- `query-blueprint --include interfaces` — list implemented interfaces on a Blueprint

### Fixed
- `.gitignore` `build/` rule no longer excludes `Tools/Build/` plugin source files

## [1.0.0] - 2026-03-15

### Added
- Initial release with 50+ commands for controlling Unreal Engine 5
- Actor spawning, Blueprint editing, material inspection, PIE control
- Screenshot capture, performance profiling, Python scripting
- StateTree editing, widget inspection, DataTable editing
- Build and Live Coding support
- Automatic server discovery via `.soft-ue-bridge/instance.json`
