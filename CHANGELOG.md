# Changelog

All notable changes to soft-ue-cli will be documented in this file.

## [1.16.0] - 2026-04-07

### Fixed
- Plugin: bridge HTTP listener dropped after PIE startup — `USoftUEBridgeSubsystem` now registers a 10-second `FTSTicker` that calls `StartAllListeners()` to revive listeners silently stopped by PIE world initialization; subsequent bridge calls no longer fail with WinError 10054 after a PIE session
- `test-tools` skill: teardown script called `AssetEditorSubsystem.close_all_asset_editors()` which does not exist in UE 5.7 — replaced with a `getattr` probe that tries `close_all_asset_editors` and `close_all_editors` in order; `SystemLibrary.collect_garbage()` now always runs regardless of whether an editor-close method is found

## [1.15.0] - 2026-04-07

### Fixed
- MCP: `set-console-var` rejected integer/float values — MCP schema now declares `value` as `any` type so pydantic accepts strings, ints, and floats
- MCP: `batch-delete-actors` rejected list for `actors` field — schema now declares `actors` as array type; same fix applied to `batch-spawn-actors.actors`, `batch-modify-actors.modifications`, `spawn-actor.location/rotation`, `add-graph-node.position`, and `set-node-position.positions`
- MCP: `capture-screenshot` required `mode` field — schema now makes `mode` optional with default `"viewport"`
- MCP: `report-bug` hung indefinitely via MCP transport — `gh auth token` subprocess was inheriting the MCP stdin pipe and blocking; fixed with `stdin=subprocess.DEVNULL`
- MCP: `test-tools --mode mcp` queue desync after client-side timeout — `MCPClient._recv` now matches responses by ID, discarding stale responses from previous timed-out calls

## [1.14.0] - 2026-04-07

### Fixed
- `pie-session start` timed out after 30 s even when a longer timeout was requested — the tool's server-side `WaitForPIEReady` has its own `timeout` argument (default 30 s); the test now passes it explicitly so PIE gets the full allotted time
- `open-asset` for World assets crashed the editor with "World Memory Leaks: 1 leaked objects" (triggered by Niagara holding a world reference during level switch) — fatal error is now suppressed via a custom `FOutputDeviceError` device for the duration of `OpenEditorForAsset`, allowing the world switch to complete and Niagara to update its reference normally on the next tick
- `modify-interface`: interface class paths like `/Game/Path/BPI_Name` now resolve correctly; the tool tries the `_C`-suffixed class path and Blueprint `GeneratedClass` fallback when the initial load fails

### Added
- `test-tools` skill v2.0 — `--mode cli` (default, direct HTTP), `--mode mcp` (via `mcp-serve` stdio), `--mode all` (runs both and combines report); MCP mode exercises the full MCP server layer without any LLM in the loop
- `call_tool()` now accepts an optional `timeout` parameter to override the per-request HTTP timeout (falls back to `SOFT_UE_BRIDGE_TIMEOUT` env var or 30 s)
- Plugin: Windows Structured Exception handling (SEH) in the bridge root — unhandled C++ exceptions from tools are caught and returned as JSON-RPC errors instead of crashing the editor

## [1.13.0] - 2026-04-06

### Fixed
- `mcp-serve`: `report-bug` (and other client-side tools) crashed with `NameError: name 'io' is not defined` — added missing `import io` to `mcp_server.py`
- `mcp-serve`: `query-asset` with `asset_class` filter returned 0 results — MCP was forwarding the parameter as `asset_class` but the bridge expects `class`; added per-tool parameter rename mapping
- `capture-screenshot --mode window` failed via MCP with "No active editor window found" — the editor is never the foreground window when an agent calls via mcp-serve; now uses `IMainFrameModule::GetParentWindow()` with fallback to the active window

### Added
- `create-asset` now supports `World` asset type — create new levels from the CLI: `soft-ue-cli create-asset /Game/Maps/LV_New World`
- `create-asset --template PATH` — duplicate an existing level instead of creating a blank one

## [1.12.0] - 2026-04-06

### Fixed
- `mcp-serve`: tool arguments were silently dropped — MCP client now receives the correct JSON schema for every tool and arguments are forwarded to the bridge as expected
- `mcp-serve`: `class-hierarchy` and `project-info` routed to wrong bridge tool name, always returning "Unknown tool"
- `mcp-serve`: `status`, `check-setup`, `setup`, `report-bug`, `request-feature`, `submit-testimonial` returned "Unknown tool" because they are client-side operations; they now run their existing handlers directly and return output to the MCP client

### Changed
- `submit-testimonial` now posts via REST API instead of GitHub Discussions GraphQL

## [1.11.1] - 2026-04-06

### Fixed
- Architecture diagram missing on GitHub — use relative path instead of absolute URL to private repo
- Python version badge showing "missing" on PyPI — added Python 3.10–3.13 classifiers to pyproject.toml

## [1.11.0] - 2026-04-05

### Added
- `set-viewport-camera` command — programmatically control the editor viewport camera with presets (top, bottom, front, back, left, right, perspective), custom location/rotation, and orthographic zoom
- `level-from-image` skill — populate a UE level from a reference image using existing project assets, with autonomous visual feedback loop and human-in-the-loop refinement
- Batch actor tool reference section in level-from-image skill documentation

### Fixed
- Skills frontmatter parser now skips nested YAML lines, correctly displaying skill descriptions

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
