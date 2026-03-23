# Changelog

All notable changes to soft-ue-cli will be documented in this file.

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
