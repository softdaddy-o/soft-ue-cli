# Changelog

All notable changes to soft-ue-cli will be documented in this file.

## [1.30.0] - 2026-05-17

### Added
- New `add-anim-state-machine`, `add-anim-state`, and `add-anim-transition` commands and bridge tools create AnimBlueprint state-machine graphs, states, transitions, and transition rule graphs programmatically.

### Fixed
- `add-graph-node AnimGraphNode_StateMachine` now initializes the inner state-machine graph instead of leaving the node in the "Error: No Graph" state.
- `build-and-relaunch --wait` now respects `SOFT_UE_BRIDGE_TIMEOUT`, supports explicit build/relaunch timeout overrides, and reports `worker_failed_to_start` promptly if the detached worker never starts.
- `capture-viewport` is now registered during runtime module startup instead of through static DLL-load registration, avoiding Windows module initialization failures.
- Bridge module reload cleanup no longer shadows the registry singleton name, removing a C4458 build warning.

## [1.29.0] - 2026-05-17

### Added
- New `wait-for-ready` command, with `await-bridge` alias, polls the bridge health probe until it is ready and can optionally launch a `.uproject` first
- New `create-co-from-spec` command and bridge tool build a CustomizableObject graph from a JSON node/edge specification
- `compile-co` now supports `--gather-references` for the editor's Compile and Gather References mode when available
- `query-blueprint-graph --include-anim-props` now exposes AnimGraph wrapper cache properties, property bindings, fast-path metadata, and linked SaveCachedPose details
- `query-asset --pattern` is now an alias for name-pattern asset searches

### Changed
- `set-node-position` now supports CustomizableObject graph nodes in addition to Material, Blueprint, and AnimBlueprint graphs
- `compile-blueprint` now returns structured compiler diagnostics with severity, message, and error/warning counts

### Fixed
- SoftUEBridge runtime config tools are now registered during module startup, avoiding editor-launch `ERROR_DLL_INIT_FAILED` failures
- `add-widget` now supports single-child `ContentWidget` parents such as Border and rejects already-filled content parents before attachment
- `capture-screenshot window` now avoids the unsafe full-window Slate screenshot path while PIE has `bDisableWorldRendering` set and falls back to viewport capture
- `build-and-relaunch --wait` now follows staged worker progress, waits for a terminal status instead of treating the first status file as final, and reports the last stage plus log paths on timeout
- `trigger-live-coding` now returns an `unsupported_change` status with full-build recovery guidance when Unreal cancels Live Coding compilation
- AnimGraph SaveCachedPose cache-name edits now update the wrapper and inner anim node consistently

## [1.28.0] - 2026-05-03

### Added
- New `reload-bridge-module` command and bridge tool reload the bridge editor module from the rebuilt DLL on disk without a full editor restart
- New `wire-customizable-object-slot-from-table` command and bridge tool create a NodeTable -> Material -> ComponentMesh slot chain in one call
- `build-and-relaunch --wait` can now detect Unreal startup recovery prompts and apply, ask for, or remember recover/skip/manual handling
- `trigger-live-coding` now supports `--module` and `--plugin` scope filters so unrelated reflected header edits do not block bridge-only Live Coding attempts

### Fixed
- `capture-screenshot` now validates captured image dimensions and pixel count before compression to avoid editor crashes from invalid buffers
- Normal CLI bridge reconnects can now handle visible Unreal startup recovery prompts using a remembered or interactive choice

## [1.27.0] - 2026-05-03

### Added
- New `regenerate-co-node-pins` command and `regenerate-customizable-object-node-pins` bridge tool regenerate pins for one Mutable/CustomizableObject graph node and return the refreshed pin list
- New `remove-co-node` command and bridge tool remove CustomizableObject graph nodes by GUID, path, name, or title
- `connect-co-pins` now auto-regenerates missing source or target node pins once by default before failing; pass `--no-auto-regenerate` to keep the old strict lookup behavior
- Mutable parameter inspection now includes runtime enum option metadata when available

### Changed
- `compile-co` now prefers reflected asset compile APIs with compile-parameter configuration before falling back to editor function library compilation
- CustomizableObject NodeTable property edits now follow the editor details-panel change sequence and refresh dynamic pins after table/structure changes
- CustomizableObject property pin fallback now supports object reference defaults, enabling asset links such as mesh/object pin values
- `add-datatable-row` now forwards `row_data` as a JSON object and deserializes row fields individually

### Fixed
- UE 5.7 plugin builds no longer hit the ambiguous `SetIntPropertyValue` overload in Mutable runtime introspection
- `connect-co-pins` distinguishes node-not-found errors from pin-not-found-after-regenerate errors

## [1.26.0] - 2026-05-01

### Added
- New Mutable/CustomizableObject edit commands: `add-co-node`, `add-co-parameter`, `add-co-mesh-option`, `set-co-base-mesh`, `add-co-group-child`, `set-co-node-property`, `connect-co-pins`, and `compile-co`
- New bridge-backed CustomizableObject graph edit tools for adding nodes, setting node properties, connecting pins, and compiling assets
- Bridge health diagnostics now include registered tool names and loaded module paths to make missing-tool and stale-plugin issues easier to diagnose

### Changed
- Unknown bridge tool errors now report the requested tool, registered tool count, registered names, loaded module paths, bridge version, and recovery guidance
- MCP schemas now preserve native JSON object types for command parameters such as CustomizableObject node properties

## [1.25.10] - 2026-04-25

### Fixed
- CLI export: restored the public `inspect-customizable-object-graph`, `inspect-mutable-parameters`, and `inspect-mutable-diagnostics` command wrappers so the packaged CLI matches the documented Mutable inspection surface

## [1.25.9] - 2026-04-25

### Added
- new `inspect-customizable-object-graph` command returns machine-readable graph, node, pin, and edge data for Mutable/CustomizableObject assets
- new `inspect-mutable-parameters` command derives structured Mutable parameter metadata such as groups, defaults, options, tags, and related graph links
- new `inspect-mutable-diagnostics` command reports Mutable plugin availability plus best-effort capability and runtime diagnostics for a target asset

### Fixed
- plugin build: `AssetIntrospectionUtils.cpp` now compiles against UE 5.7 by including `UserDefinedStructEditorData.h` when available and passing a compatible parent object to `ExportText_Direct`

### Changed
- Mutable inspection commands are implemented without a hard compile-time Mutable dependency, so projects without the plugin return structured unavailable/limited results instead of failing to compile

## [1.25.8] - 2026-04-23

### Fixed
- plugin build: `GetLogsTool.cpp` no longer depends on `Misc/LexicalConversion.h`, avoiding a UE-version-specific include failure during plugin compilation

### Changed
- README now includes a project-origin note and a public-development status note explaining that future work may continue privately while the published code remains available as a prototype/reference

## [1.25.7] - 2026-04-23

### Added
- new `exec-console-command` command executes arbitrary UE console commands directly in editor, PIE, or game worlds without requiring a Python wrapper script
- new `inspect-pawn-possession` command returns structured controller/pawn possession state, AI auto-possession settings, and visibility data for runtime debugging
- new `validate-class-path` command verifies whether a soft class path exists, loads, resolves to a `UClass`, and reports its parent hierarchy
- new `request-gameplay-tag` helper resolves a registered gameplay tag name and returns validity plus export text
- new `reload-gameplay-tags` helper reloads GameplayTags settings and refreshes the in-memory tag tables
- new `release-asset-lock` best-effort command closes asset editors and forces GC to reduce editor-side file-handle conflicts during VCS workflows

### Changed
- `get-logs` now supports server-side `--contains`, cursor/timestamp-based `--since`, and `--tail-follow` polling for targeted runtime log inspection
- `run-python-script --world pie`, `exec-console-command`, and `inspect-pawn-possession` now fail fast with a structured `PIE_NOT_RUNNING` error unless PIE is already running or auto-start is requested
- `trigger-live-coding` now warns before dispatch when git reports changed reflected header files that are likely to make Live Coding cancel

## [1.25.6] - 2026-04-22

### Fixed
- `build-and-relaunch` now launches its detached Windows build worker through PowerShell in a way that survives editor shutdown, writes fresh status/log artifacts for `--wait`, and actually runs the requested build before optional relaunch
- Plugin: SoftUEBridge no longer calls `StartAllListeners()` every 10 seconds while idle; listener revival now happens only on PIE start/stop lifecycle hooks, preventing repeated `LogHttpServerModule` spam in UE 5.7 editor sessions

## [1.25.5] - 2026-04-16

### Changed
- removed the extra yellow `Support this project` README badge from the public badge strip to keep the header more compact while preserving the existing support links below

## [1.25.4] - 2026-04-15

### Fixed
- SoftUEBridge's editor-only dependency plugins are now limited to Editor targets in the plugin descriptor, preventing Python/editor scripting/StateTree editor dependencies from being enabled for packaged game targets

## [1.25.3] - 2026-04-15

### Changed
- refreshed the public README badge strip to highlight AI agent support, shipped skills, tool count, MCP mode, and direct support links without implying the package is tied to a single client

## [1.25.2] - 2026-04-14

### Fixed
- `run-python-script` CLI tests now assert the shipped `script_path` dispatch behavior used by `--name` and `--script-path`, keeping the release smoke suite aligned with the actual bridge request payloads

## [1.25.1] - 2026-04-14

### Fixed
- `query-asset --asset-path /Game/...` world inspection now builds cleanly on UE 5.7 by switching to `FSoftObjectPath` asset lookups and including the concrete `AGameModeBase` definition needed by `WorldSettings.DefaultGameMode`

## [1.25.0] - 2026-04-14

### Added
- `query-asset --asset-path /Game/...` now exposes `world_settings.default_game_mode` and related map metadata for `UWorld` assets, so test maps can be checked offline for `WorldSettings` overrides
- new `plan-test-infrastructure` and `setup-test-infrastructure` skills centralize project-specific test-map/module convention work under the `author-test` workflow

### Fixed
- `run-python-script --script-path` now executes files as files again instead of flattening them into inline source, preserving `__file__`, `__future__` imports, and normal quoting/docstring behavior
- `run-python-script` now rejects editor map-loading APIs such as `EditorLoadingAndSavingUtils.load_map()` and `EditorLevelLibrary.load_level()` before execution to avoid tearing down the active Python context
- `inspect-anim-instance` now populates `slots` from active montage slot tracks instead of always returning an empty array
- `set-asset-property` property-path traversal now descends into `InstancedStruct` payloads, enabling nested edits for assets such as ChooserTables
- authoring skill templates now use `EAutomationTestFlags::EditorContext` and explicitly avoid bare `GetWorld()` calls from generated Automation Spec bodies for UE 5.7 compatibility

## [1.24.1] - 2026-04-14

### Fixed
- offline External Actor tagged property parsing now resolves combined `FName` references in property headers, so fields such as `DataLayerAssets` no longer fall back to `invalid_name_*` when the upper 32-bit name number is populated

## [1.24.0] - 2026-04-14

### Added
- `inspect-uasset --sections properties` and `diff-uasset --sections properties` now expose and diff External Actor tagged UPROPERTY payloads offline, including common scalar, object, array, and struct values when parsable
- README architecture and testing docs now use text diagrams and explicitly show the split between bridge-backed exploration flows and offline/local capabilities

### Changed
- test authoring skills now target committed C++ Automation Spec outputs; CLI + bridge + Python workflows are documented as exploration inputs rather than the final regression artifact
- `run-test` skill has been removed; execution guidance now belongs with the generated C++ test workflow rather than a generic skill

### Fixed
- `run-python-script --script-path` now resolves the file locally before dispatch, avoiding bridge-side path misinterpretation
- `run-python-script` now supports explicit world targeting and injects world helpers for editor/PIE/game execution
- `inspect-widget-blueprint` now surfaces referenced Input Mapping Context bindings and resolved keys for Input Action references
- SoftUEBridge now defaults to Unreal's `DeveloperTool` module type so Shipping builds exclude it by default
- `query-material` no longer risks an unbounded `GetInput()` iteration when inspecting material graphs

## [1.23.0] - 2026-04-13

### Added
- `rewind-start`, `rewind-stop`, `rewind-status` commands to control UE Rewind Debugger recording sessions with channel and actor filtering
- `rewind-list-tracks`, `rewind-overview`, `rewind-snapshot` commands for LLM-driven animation debugging — list recorded actors, get track-level summaries, and drill down to animation state at a specific time or frame
- `rewind-save` command to persist in-memory recordings to `.utrace` files
- `inspect-uasset` / `diff-uasset` now support non-Blueprint assets (AnimSequence, PoseSearchDatabase, DataTable, etc.) via a generic export/import summary fallback
- `inspect-uasset` now extracts actor label/class/path, GUID, folder, runtime grid, tags, and data-layer hints from External Actor `.uasset` packages for offline history review
- `inspect-uasset --sections properties` and `diff-uasset --sections properties` now expose and diff External Actor tagged UPROPERTY payloads offline, including common scalar, object, array, and struct values when parsable

### Fixed
- `pie-tick` no longer crashes with a re-entrant TaskGraph assertion when UMassEntityEditorSubsystem (or any `FTickableEditorObject` that waits on game-thread tasks) is active. World ticks are now deferred through FTSTicker and driven by the engine's normal Slate tick loop instead of running directly inside the bridge server's AsyncTask handler.

### Notes
- Rewind Debugger commands require the **Animation Insights (GameplayInsights)** plugin to be enabled in Edit > Plugins. Error messages guide users to enable it when not active.
- README architecture docs now use text diagrams and explicitly show which commands bypass the bridge and operate on local files offline.

## [1.22.0] - 2026-04-12

### Added
- `batch-call` command for composing multiple bridge tool invocations into one in-process batch request
- `pie-tick` command for deterministic PIE stepping by frame count
- `inspect-anim-instance` command for one-shot snapshots of live anim state machines, montages, and blend weights
- `run-python-script` bridge helper import support via `from soft_ue_bridge import call`
- `replay-changes` skill for Git and Perforce binary-asset conflict recovery using manual `base/local/remote` extraction plus offline `.uasset` inspection

### Changed
- `call-function` now supports class-targeted invocation flows including class default object and transient-instance execution, plus batch JSON input
- `test-tools` now exercises the new automation surface for `batch-call`, `pie-tick`, `inspect-anim-instance`, and transient `call-function`
- `replay-changes` is now documented as a CLI skill workflow instead of a dedicated command

### Fixed
- `call-function` latent-function rejection now uses a portable `LatentActionInfo` struct-name check across engine versions
- `inspect-anim-instance` resolves state machine descriptors more reliably and avoids engine-version-specific transition query breakage during bridge builds
- public CLI export sync now hard-fails if embargoed private names appear in examples, docs, tests, prompts, or code

## [1.21.0] - 2026-04-10

### Added
- `query-enum` command for UserDefinedEnum introspection: authored names, display names, tooltips, and numeric values
- `query-struct` command for UserDefinedStruct introspection: authored member names, defaults, metadata, and reflected type info
- `blueprint-to-cpp` skill now starts with dependency-first planning guidance, including enum/struct inspection and promotion-first conversion strategy

### Fixed
- `query-asset --asset-path` now inspects `UserDefinedEnum` and `UserDefinedStruct` assets instead of failing with a generic load error
- `query-asset --asset-path` now inspects Blueprint-generated `UDataAsset` / `UPrimaryDataAsset` assets via their generated class default object
- `capture-screenshot tab --window-name ...` now falls back to visible tab labels and matching top-level window titles, so asset editor tabs opened by label can be captured more reliably

### Changed
- Removed the standalone `inspect-uasset` skill prompt; offline `.uasset` inspection remains available as a command and as part of `test-tools`

## [1.20.6] - 2026-04-10

### Changed
- Clarified `inspect-uasset` and `diff-uasset` help text, skill metadata, and README wording: they operate on local `.uasset` files offline, with best support currently for Blueprint assets rather than Blueprint-only support

## [1.20.5] - 2026-04-10

### Fixed
- `open-asset` no longer dereferences a stale `UWorld` pointer after `LoadLevel()`, fixing the access violation that could occur when loading World assets through the level editor path

## [1.20.4] - 2026-04-10

### Fixed
- `inspect-uasset` and `diff-uasset` now parse UE 5.4+ package headers (`LegacyFileVersion -8/-9`), so offline Blueprint inspection works on modern UE 5.4-5.7 assets instead of failing on the package summary

## [1.20.3] - 2026-04-10

### Fixed
- Public repo `README.md` and `CHANGELOG.md` are no longer stripped of newlines during sync, so the GitHub project page and PyPI description render correctly again (regression introduced in 1.20.0)

## [1.20.2] - 2026-04-10

### Fixed
- `get-config-value` and `validate-config-key` now share the same resolved section-entry lookup path, allowing bridge reads for keys such as `r.Bloom` that may not round-trip through `GConfig->GetString()`
- `test-tools` now validates `set-config-value` against the bridge tool's actual `{"status":"ok"}` response payload instead of expecting a nonexistent `success` field

### Changed
- `test-tools` now resolves offline `.uasset` paths from `get-project-info.project_directory` and the asset path created earlier in the run, so `inspect-uasset` and `diff-uasset` exercise the saved asset directly
- `test-tools` now seeds a project config key before running `config get --search`, so the offline config search path is exercised against known on-disk input

## [1.20.1] - 2026-04-10

### Fixed
- Bridge config tools now use public `FConfigCacheIni` section accessors instead of the unavailable `GetSectionPrivate()` helper, fixing UE builds that failed when compiling `get-config-value` and `validate-config-key`

### Changed
- `test-tools` skill now exercises the new config bridge tools and CLI config subcommands during live integration runs

## [1.20.0] - 2026-04-09

### Added
- Unified `config` command group for Unreal configuration inspection and editing: `config tree`, `config get`, `config set`, `config diff`, and `config audit`
- Offline config parsing and diff support for UE INI, `BuildConfiguration.xml`, and `.uproject` / `.uplugin` JSON files
- Runtime bridge config helpers: `get-config-value`, `set-config-value`, and `validate-config-key`

### Changed
- MCP now exposes the nested client-side `config` tool cleanly via schema extraction and command dispatch

## [1.19.0] - 2026-04-08

### Added
- `inspect-uasset` command for offline inspection of local Blueprint `.uasset` files without a running Unreal Editor
- `diff-uasset` command for offline file-to-file diff of extracted Blueprint metadata sections
- `inspect-uasset` skill for LLM workflows that need conservative offline package inspection

### Changed
- `test-tools` skill now exercises `inspect-uasset` and `diff-uasset` against a real on-disk Blueprint generated during the integration run

### Fixed
- Offline Blueprint summary extraction now resolves `parent_class_path` from the imported object's outer package chain instead of reporting incorrect `/Script/CoreUObject.*` paths for common engine parents
- Offline Blueprint summary extraction now parses `blueprint_type` instead of hardcoding `Normal`
- `inspect-uasset` now fails fast on non-Blueprint packages instead of returning a misleading successful `Unknown` summary

## [1.18.0] - 2026-04-08

### Fixed
- CLI argument validation: `capture-screenshot --region` and `add-graph-node --position` now fail with a clear comma-separated-integer error instead of surfacing a raw Python `ValueError`
- CLI argument validation: `query-mpc --value` now reports a friendly numeric/JSON parsing error for malformed scalar input instead of crashing with a raw `ValueError`

## [1.17.0] - 2026-04-08

### Fixed
- `report-bug` and `request-feature`: `gh auth token` lookup now times out cleanly instead of hanging indefinitely when GitHub CLI credentials are unavailable or blocked
- MCP schema: `set-property` now accepts any JSON value for `value`, matching the CLI and bridge behavior for scalar, array, and object payloads
- MCP server: `add-graph-node` now maps `--no-auto-position` correctly, surfaces normalized `node_guid` values for special node creation cases, and returns cleaner client-side command errors
- MCP server: `pie-session start` now forwards tool-level timeout to the HTTP request and attempts a best-effort stop after startup timeouts to avoid leaving PIE half-initialized
- `test-tools`: teardown now treats `delete-asset` reporting `Asset not found` as an idempotent success during cleanup, avoiding false failures after restore flows that already removed the temporary test asset
- `test-tools`: `insights-capture stop` now treats already-idle or auto-stopped traces as a pass in both CLI and MCP paths, avoiding false failures when trace state changes between status polling and stop
- `test-tools`: MCP all-mode now reads `mcp-serve` stdout as UTF-8 with replacement semantics, avoiding Windows `cp949` decode crashes on non-ASCII output
- `test-tools`: setup now retries the first `open-asset` call for a freshly created temporary World asset, reducing false setup failures while the editor finishes registering the new level
- `test-tools`: restore flow now saves the temporary test level before switching back, avoiding modal unsaved-level prompts that can block or crash automation
- `open-asset`: World assets now load through the level editor path with extra GC passes, reducing map-switch crashes and stale-world failures during automation
- `pie-session`: start/stop now return request-based transitional states instead of blocking the request thread while UE finishes entering or leaving PIE
- `insights-capture`: trace start now uses the documented filename-first console command form and stop/status treat already-idle traces consistently, reducing false stop failures in automation

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
