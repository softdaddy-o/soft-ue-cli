# Changelog

All notable changes to soft-ue-cli will be documented in this file.

## [1.10.0] - 2026-04-05

### Added
- `submit-testimonial` command — share feedback via GitHub Discussions with auto-collected metadata (CLI version, usage streak, top tools), consent prompt before posting
- Bug report nudge — unexpected errors now suggest filing a bug with a pre-filled `report-bug` command
- Daily usage streak tracking — after 3+ consecutive days of use, a one-time testimonial nudge appears
- MCP server returns structured `bug_report_hint` and `testimonial_nudge` payloads for LLM agents
- GitHub Discussions integration via GraphQL API for testimonial posting

### Changed
- `call_tool()` now raises `BridgeError` with error classification (expected vs unexpected) instead of calling `sys.exit(1)` directly — enables richer error handling downstream

## [1.9.0] - 2026-04-03

### Added
- `mcp-serve` command — run soft-ue-cli as an MCP server over stdio, exposing 60+ commands as MCP tools and skills as MCP prompts
- Compatible with Claude Desktop, Claude Code, Cursor, Windsurf, and other MCP clients
- Install with: `pip install soft-ue-cli[mcp]`

## [1.8.0] - 2026-04-03

### Added
- `skills list` command — discover LLM workflow prompts shipped with the CLI
- `skills get <name>` command — retrieve a skill's full content for LLM consumption
- `blueprint-to-cpp` skill — instructs an LLM to generate C++ `.h`/`.cpp` from Blueprint assets using Layer 1 (class scaffolding) and Layer 2 (graph logic translation with 100+ node type mappings)

### Fixed
- `compile-material` now uses `GMaxRHIShaderPlatform` instead of deprecated `GMaxRHIFeatureLevel` (UE 5.7 compatibility)

## [1.7.1] - 2026-04-02

### Fixed
- `query-level --class-filter` now matches inherited classes (e.g. `--class-filter Character` finds all Character subclasses)

## [1.7.0] - 2026-04-01

### Added
- `compile-material` command — trigger recompilation of Material, MaterialInstance, or MaterialFunction assets from the CLI
- MSYS/Git Bash path mangling detection — automatically reverses `/Game/` → `C:/Program Files/Git/Game/` conversion for asset paths

### Fixed
- `get-logs` and all output commands no longer crash with `UnicodeEncodeError` on Korean Windows (cp949 locale)

## [1.6.2] - 2026-03-29

### Added
- `query-material` now supports MaterialFunction assets — inspect expression graphs inside material functions

## [1.6.1] - 2026-03-25

### Fixed
- `create-asset AnimBlueprint --skeleton` now correctly creates an AnimBlueprint instead of a generic Actor Blueprint (subclass routing order fix)
- `add-graph-node --properties '{"Layer":"X"}'` on LinkedAnimLayer now sets InterfaceGuid for proper layer function binding and pin reconstruction

## [1.6.0] - 2026-03-25

### Added
- `set-node-property` command — set properties on graph nodes by GUID after creation, supporting UPROPERTY members, inner anim node structs, and pin defaults
- `query-mpc` command — read and write Material Parameter Collection scalar/vector values (both default and runtime)
- `save-asset --checkout` — auto-checkout from source control (Perforce, etc.) before saving
- `query-material --parent-chain` — walk full MaterialInstance inheritance chain from leaf to root Material
- `query-level --include-foliage` — list FoliageType instances with counts from InstancedFoliageActors
- `query-level --include-grass` — list LandscapeProxy actors with component counts and materials
- `create-asset AnimLayerInterface` (or `ALI`) — creates a Blueprint-compatible AnimLayerInterface using BPTYPE_Interface factory
- `query-asset` structured output for `ULandscapeGrassType` — parses GrassVarieties into per-variety JSON with mesh, density, culling, scaling fields

### Fixed
- `add-graph-node --properties '{"Layer":"X"}'` now correctly configures LinkedAnimLayer nodes by setting Interface and Layer on the inner FAnimNode struct and reconstructing pins
- `add-graph-node --properties '{"Alpha":0.08}'` now sets pin default values (Alpha, BlendWeight, etc.) when properties aren't found via reflection
- `create-asset` phantom registry deadlock resolved — force-rescans the package path to clear stale entries before creation

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
