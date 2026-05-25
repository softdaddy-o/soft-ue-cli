# soft-ue-cli (+mcp)

[![PyPI version](https://img.shields.io/pypi/v/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![Python 3.10+](https://img.shields.io/pypi/pyversions/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![AI agents](https://img.shields.io/badge/AI_agents-ready-7c3aed)](#why-soft-ue-cli)
[![skills](https://img.shields.io/badge/skills-13-84cc16)](#skills-llm-workflow-prompts)
[![commands](https://img.shields.io/badge/commands-120%2B-f97316)](#complete-command-reference)
[![MCP](https://img.shields.io/badge/MCP-server-0ea5e9)](#mcp-server-mode)
[![AI built for coding agents](https://img.shields.io/badge/AI_built_for-coding_agents-6b7280)](#why-soft-ue-cli)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub_Sponsors-Support_this_project-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/softdaddy-o)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Buy_me_a_coffee-ff5e5b?logo=ko-fi&logoColor=white)](https://ko-fi.com/softdaddy)

Built and maintained by a solo developer. [Support this project](#support-this-project) if it saves you time.


**Control Unreal Engine 5 from your AI agent or terminal.** soft-ue-cli gives any LLM -- via **MCP server** or **CLI** -- 120+ commands and tools to spawn actors, edit Blueprints, inspect materials, build UMG screens, read and patch UE config files, run Play-In-Editor sessions, capture token-efficient screenshots, profile performance, and inspect local Unreal assets.

Two connection paths. One package. Bridge tools when Unreal is running, offline tools when it is not, and a command catalog that tells agents which surface is canonical, which removed flat names map to which canonical tools, and which optional Unreal plugins are required.


```text
LLM client / shell / CI
    |
    v
soft-ue-cli  (CLI or MCP server)
    |
    +-- Live bridge path ----------------------------------------------+
    |      HTTP / JSON-RPC
    |      -> SoftUEBridge plugin inside UE editor / PIE / dev build
    |      -> Actor, Blueprint, material, widget, PIE, profiling tools
    |
    +-- Offline local path --------------------------------------------+
           Direct local parsing
           -> .uasset / .uexp / .ini / .uproject / BuildConfiguration.xml
           -> asset inspect-file, asset diff-file, config tree/get/diff/audit, skills
```

---

## Why soft-ue-cli?

- **MCP server + CLI in one package** -- use as an MCP server (`mcp-serve`) for Claude Desktop, Cursor, Windsurf, and other MCP clients, **or** as a standard CLI for Claude Code, shell scripts, and CI/CD. Same 120+ tool surface either way.
- **AI-native UE automation** -- purpose-built so LLM agents can read, modify, and test Unreal Engine projects without a human touching the editor.
- **120+ commands and tools** covering actors, Blueprints, materials, StateTrees, Mutable/CustomizableObject, widgets, assets, config files, PIE sessions, profiling, screenshots, and local Unreal file analysis.
- **Canonical command families only** -- UMG, capture, Mutable, StateTree, animation, asset, and Blueprint workflows are grouped under `umg`, `capture`, `mutable`, `statetree`, `anim`, `asset`, and `blueprint`. Removed flat names are discoverable with `commands --include-removed`.
- **Plugin-aware metadata** -- `soft-ue-cli commands --json` reports bridge/editor/PIE requirements plus optional Unreal plugin dependencies, and bridge tools return structured `plugin_unavailable` errors when a plugin is missing.
- **Token-aware visual feedback** -- viewport and screenshot capture can resize output by scale, width, or height and can emit color, grayscale, or monochrome images for lower LLM token cost.
- **Online + offline workflows** -- bridge-backed UE mutation and runtime inspection when Unreal is open, plus direct local inspection, diff, and config tooling when it is not.
- **Config-aware workflows** ??inspect hierarchy, trace overrides, diff layers, and patch `.ini`, `BuildConfiguration.xml`, and `.uproject` data from one `config` command group.
- **UMG authoring loop** -- draft editable Widget Blueprint trees, wire navigation contracts, run PIE interaction checks, and compare designer/runtime layout artifacts.
- **LLM skill prompts** -- ships with markdown workflows (e.g. Blueprint-to-C++ conversion) exposed as MCP prompts or CLI commands.
- **Works everywhere UE runs** -- editor, cooked builds, Windows, macOS, Linux.
- **Small dependency footprint** -- only requires `httpx` and `Pillow`. Add `[mcp]` extra for MCP server mode.
- **Team-friendly** -- conditional compilation via `SOFT_UE_BRIDGE` environment variable means only developers who need the bridge get it compiled in.

## Current Direction

Recent releases moved soft-ue-cli from a flat list of one-off bridge calls toward a discoverable automation surface for agents:

- `commands` is the source of truth for command status, replacement names, runtime requirements, optional plugin requirements, and examples.
- Canonical families (`umg`, `capture`, `mutable`, `statetree`, `anim`, `asset`, `blueprint`) are now the supported public command surface. Removed flat commands are listed only as migration metadata via `commands --include-removed`.
- UMG and screenshot workflows now support the full agent loop: author, inspect, run PIE, capture visual evidence, compare layout, and keep image payloads small.
- Optional plugin workflows are expected to compile cleanly without those plugins installed, then fail at runtime with actionable `plugin_unavailable` diagnostics.
- CLI/Python/bridge probing is treated as the exploration layer; durable gameplay regressions should move into project-native C++ Automation Specs.

## UE 5.8 MCP Positioning

UE 5.8 is adding first-party Unreal MCP support. Use it when you want an Epic-managed, UE 5.8-native MCP endpoint and it already covers the workflow you need.

soft-ue-cli is intentionally a different layer rather than a replacement for first-party MCP:

- It works as both a normal terminal CLI and an MCP server, so the same automation can run from Claude Code, shell scripts, CI, or any MCP client.
- It includes offline commands for `.uasset`, `.uexp`, `.ini`, `.uproject`, `.uplugin`, and `BuildConfiguration.xml` files, even when the editor is closed.
- It ships curated LLM skill prompts and a `test-tools` smoke workflow for repeatable agent execution, not just raw editor operations.
- It supports UE 5.7 today and also runs in Development/DebugGame cooked builds, while still exposing a normal terminal CLI and an MCP server from the same package.
- It reports removed flat command migrations through `commands --include-removed` instead of keeping duplicate public command names alive.
- It reports optional plugin requirements and missing-plugin failures in structured JSON so agents can recover instead of guessing.

The two can coexist: use UE's first-party MCP for native UE 5.8 editor coverage when it fits, and use soft-ue-cli for UE 5.7 projects, cooked Development/DebugGame builds, CLI/CI automation, offline inspection, curated workflows, visual capture transforms, optional plugin diagnostics, and bridge tools that move independently of engine releases.

---

## Quick Start

### 1. Install

```bash
pip install soft-ue-cli          # CLI only
pip install soft-ue-cli[mcp]     # CLI + MCP server
```

### 2. Install the plugin into your UE project

Run the setup command **inside your LLM client** (Claude Code, Cursor, etc.) ??it outputs step-by-step instructions that the AI agent will follow to copy the plugin, edit your `.uproject`, and configure itself:

```bash
soft-ue-cli setup /path/to/YourProject
```

If you're running manually (not via an LLM), follow the printed instructions yourself: copy the plugin directory, add the `"Plugins"` entry to your `.uproject`, and create the `CLAUDE.md` snippet.

### 3. Rebuild and launch Unreal Engine

After regenerating project files and rebuilding, launch the editor. Look for this log line to confirm the bridge is running:

```
LogSoftUEBridge: Bridge server started on port 8080
```

### 4. Verify the connection

```bash
soft-ue-cli check-setup
```

You should see all checks pass:

```
[OK]   Plugin files found.
[OK]   SoftUEBridge enabled in YourGame.uproject.
[OK]   Bridge server reachable.
```

### 5. (Optional) Connect your MCP client

Add to your MCP client config (Claude Desktop, Cursor, Windsurf, etc.):

```json
{
  "mcpServers": {
    "soft-ue-cli": {
      "command": "soft-ue-cli",
      "args": ["mcp-serve"]
    }
  }
}
```

The AI editor now has direct access to 120+ UE tools and skill prompts -- no terminal needed.

---

## How It Works

```text
soft-ue-cli command
    |
    +-- Bridge-backed commands ----------------------------------------+
    |      HTTP / JSON-RPC
    |      -> SoftUEBridge plugin (UGameInstanceSubsystem inside UE)
    |      -> UE APIs on the game thread
    |      -> Runtime/editor operations such as spawn-actor, PIE, query-level
    |
    +-- Offline commands ----------------------------------------------+
           Local parsers and file readers
           -> Package tables / tagged properties / config hierarchy
           -> asset inspect-file, asset diff-file, config *, skills get/list
```

The **SoftUEBridge** plugin is a lightweight C++ `UGameInstanceSubsystem` that starts an embedded HTTP server on port 8080 when UE launches. Bridge-backed commands send JSON-RPC requests to this server, and the plugin executes the corresponding UE operations on the game thread, returning structured JSON responses. Offline commands bypass the bridge entirely and operate directly on local files.

Automation commands return structured JSON to stdout (except `get-logs --raw`). Discovery and support commands use human-readable output by default where that is more useful, and expose `--json` where applicable. Exit code 0 means success, 1 means error.

### Skills Architecture

```
LLM client (Claude Code, Cursor, etc.)
    |
    |  soft-ue-cli skills get <name>
    v
Skill file  (markdown shipped with CLI pip package)
    |
    |  LLM reads instructions, type mappings, pre-filled commands
    v
LLM executes soft-ue-cli commands (blueprint inspect, blueprint graph inspect, ...)
    |
    v
LLM generates output (e.g. .h/.cpp files) following the skill's rules
```

Skills are **markdown files** at `cli/soft_ue_cli/skills/*.md`, shipped as package data in the pip distribution. Each skill is self-contained: workflow instructions, reference tables, example CLI commands, and verification test cases. The CLI discovers them via `skills list` / `skills get`. When running as an MCP server, the same files are exposed via the `prompts/list` and `prompts/get` protocol.

### Test Workflow

Use soft-ue-cli to explore and debug a gameplay bug quickly, then move the final regression into the project's C++ Automation Spec suite.

```text
CLI + bridge + Python exploration
    -> find the signal
    -> validate the repro
    -> identify the exact assertion
    -> write the committed C++ Automation Spec in the project test module
```

The CLI is the exploration layer. The committed regression gate should live in project-native C++ tests rather than depending on the CLI, bridge, or Python runtime.

### MCP Server Architecture

```
MCP Client (Claude Desktop, Cursor, Windsurf, etc.)
    |
    |  stdio (JSON-RPC, MCP protocol)
    v
soft-ue-cli mcp-serve  (FastMCP server)
    |
    |  Reuses call_tool() ??HTTP/JSON-RPC
    v
SoftUEBridge plugin (inside UE)
```

Running `soft-ue-cli mcp-serve` starts an MCP server over stdio. It auto-generates MCP tool schemas from the CLI's argparse parser and forwards tool calls to the UE bridge. Skills are exposed as MCP prompts. Install the optional extra: `pip install soft-ue-cli[mcp]`.

---

## Command Discovery And Taxonomy

Use `soft-ue-cli commands` to inspect the public command surface without connecting to Unreal:

```bash
soft-ue-cli commands
soft-ue-cli commands --json
soft-ue-cli commands --category umg
soft-ue-cli commands --category mutable
soft-ue-cli commands --category statetree
soft-ue-cli commands --plugin Mutable --json
soft-ue-cli commands --include-removed --json
```

The README uses a conservative `120+` count. For exact numbers in the installed version, use `soft-ue-cli commands --json` for the full catalog and `soft-ue-cli mcp-serve` for the MCP-exposed leaf tools.

The JSON output marks each command as canonical, removed, or deprecated, and includes whether it needs the bridge, editor, PIE, or optional Unreal plugins. Removed entries are hidden by default; pass `--include-removed` to show the old flat command and its `canonical_command` migration target. Plugin-dependent bridge tools use a structured unavailable response when a plugin is missing:

```json
{
  "success": false,
  "error_code": "plugin_unavailable",
  "plugin": "StateTree",
  "command": "statetree inspect",
  "recovery": "Enable the StateTree plugin, rebuild if prompted, and restart the editor."
}
```

UMG, capture, Mutable, StateTree, animation, asset, and Blueprint workflows now use canonical command families. Old one-off command names were removed from the public parser; use this migration table or `soft-ue-cli commands --include-removed --json` when updating scripts:

| Removed flat command | Migrate to |
|----------------------|------------|
| `apply-widget-tree` | `umg designer apply` |
| `inspect-widget-blueprint` | `umg designer inspect` |
| `wire-widget-navigation` | `umg navigation wire` |
| `verify-umg-workflow` | `umg workflow run` |
| `extract-umg-layout` | `umg layout extract` |
| `compare-umg-layout` | `umg layout compare --mode geometry` |
| `compare-umg-screenshot` | `umg layout compare --mode pixel` |
| `capture-viewport` | `capture viewport` |
| `capture-screenshot` | `capture screenshot --source <mode>` |
| `capture-pie-screenshot` | `capture screenshot --source pie-window` |
| `inspect-customizable-object-graph` | `mutable inspect graph` |
| `inspect-mutable-parameters` | `mutable inspect parameters` |
| `inspect-mutable-diagnostics` | `mutable inspect diagnostics` |
| `add-co-node` | `mutable graph add-node` |
| `add-co-parameter` | `mutable graph add-parameter` |
| `add-co-mesh-option` | `mutable graph add-mesh-option` |
| `set-co-base-mesh` | `mutable graph set-base-mesh` |
| `add-co-group-child` | `mutable graph add-group-child` |
| `set-co-node-property` | `mutable graph set-node-property` |
| `connect-co-pins` | `mutable graph connect-pins` |
| `regenerate-co-node-pins` | `mutable graph regenerate-node-pins` |
| `remove-co-node` | `mutable graph remove-node` |
| `create-co-from-spec` | `mutable graph create-from-spec` |
| `wire-customizable-object-slot-from-table` | `mutable graph wire-slot-from-table` |
| `compile-co` | `mutable compile` |
| `query-statetree` | `statetree inspect` |
| `add-statetree-state` | `statetree state add` |
| `remove-statetree-state` | `statetree state remove` |
| `add-statetree-task` | `statetree task add` |
| `add-statetree-transition` | `statetree transition add` |
| `inspect-anim-instance` | `anim instance inspect` |
| `inspect-sync-markers` | `anim sync-marker inspect` |
| `compare-sync-markers` | `anim sync-marker compare` |
| `add-sync-marker` | `anim sync-marker add` |
| `remove-sync-marker` | `anim sync-marker remove` |
| `add-anim-state-machine` | `anim state-machine add` |
| `add-anim-state` | `anim state add` |
| `add-anim-transition` | `anim transition add` |
| `rewind-start` | `anim rewind start` |
| `rewind-stop` | `anim rewind stop` |
| `rewind-status` | `anim rewind status` |
| `rewind-list-tracks` | `anim rewind list-tracks` |
| `rewind-overview` | `anim rewind overview` |
| `rewind-snapshot` | `anim rewind snapshot` |
| `rewind-save` | `anim rewind save` |
| `query-asset` | `asset query` |
| `delete-asset` | `asset delete` |
| `release-asset-lock` | `asset release-lock` |
| `get-asset-diff` | `asset diff` |
| `get-asset-preview` | `asset preview` |
| `open-asset` | `asset open` |
| `set-asset-property` | `asset set-property` |
| `inspect-uasset` | `asset inspect-file` |
| `diff-uasset` | `asset diff-file` |
| `save-asset` | `asset save` |
| `create-asset` | `asset create` |
| `query-blueprint` | `blueprint inspect` |
| `query-blueprint-graph` | `blueprint graph inspect` |
| `compile-blueprint` | `blueprint compile` |
| `add-graph-node` | `blueprint node add` |
| `remove-graph-node` | `blueprint node remove` |
| `set-node-position` | `blueprint node position` |
| `set-node-property` | `blueprint node property` |
| `connect-graph-pins` | `blueprint pin connect` |
| `disconnect-graph-pin` | `blueprint pin disconnect` |
| `modify-interface` | `blueprint interface modify` |

## Complete Command Reference

Canonical commands are grouped under command families such as `blueprint`, `asset`, `mutable`, `anim`, `capture`, `umg`, and `statetree`. Run `soft-ue-cli <family> --help` or `soft-ue-cli <family> <subcommand> --help` for detailed options.

### Setup and Diagnostics

| Command | Description |
|---------|-------------|
| `setup` | Copy SoftUEBridge plugin into a UE project |
| `check-setup` | Verify plugin files, .uproject settings, and bridge server reachability |
| `status` | Health check -- returns server status |
| `wait-for-ready` | Poll the same bridge health probe as `status` until it is ready (`await-bridge` alias) |
| `project-info` | Get project name, engine version, target platforms, and module info |

### Actor and Level Operations

| Command | Description |
|---------|-------------|
| `spawn-actor` | Spawn an actor by class at a given location and rotation |
| `query-level` | List actors in the current level with transforms, filtering by class or name |
| `call-function` | Call any `BlueprintCallable` `UFUNCTION` on an actor, class default object, or transient instance |
| `batch-call` | Dispatch multiple bridge tool calls in-process with one HTTP roundtrip |
| `set-property` | Set a `UPROPERTY` value on an actor by name |
| `get-property` | Read a `UPROPERTY` value from an actor or component using reflection |
| `add-component` | Add a component to an existing actor |

### Blueprint Inspection and Editing

| Command | Description |
|---------|-------------|
| `blueprint` | Canonical Blueprint inspection, graph, node, pin, interface, and compile command family |
| `insert-graph-node` | Atomically insert a node between two connected nodes |
| `blueprint inspect` | Inspect a Blueprint asset -- components, variables, functions, interfaces, event dispatchers |
| `blueprint graph inspect` | Inspect event graphs, function graphs, nested AnimBlueprint graphs, node connections, positions, and class-filtered nodes |
| `asset inspect-file` | Inspect a local `.uasset` file offline by parsed metadata, with best support for Blueprint and External Actor assets |
| `asset diff-file` | Diff two local `.uasset` files offline by parsed metadata, with best support for Blueprint and External Actor assets |
| `blueprint node add` | Add a node to a Blueprint or Material graph (supports `AnimLayerFunction` for ALIs) |
| `blueprint node remove` | Remove a node from a graph |
| `blueprint node position` | Batch-set node positions for graph layout |
| `blueprint node property` | Set properties on a graph node by GUID (UPROPERTY, inner structs, pin defaults) |
| `blueprint pin connect` | Connect two pins between graph nodes |
| `blueprint pin disconnect` | Disconnect pin connections (all or specific with `--target-node`/`--target-pin`) |
| `blueprint interface modify` | Add or remove an implemented interface on a Blueprint or AnimBlueprint |
| `blueprint compile` | Compile a Blueprint or AnimBlueprint and return the result |
| `anim state-machine add` | Add an initialized AnimBlueprint state machine with optional initial state |
| `anim state add` | Add a state and state content graph to an AnimBlueprint state machine |
| `anim transition add` | Add a transition and transition rule graph between AnimBlueprint states |
| `compile-material` | Compile a Material, MaterialInstance, or MaterialFunction |
| `asset save` | Save a modified asset to disk (with optional `--checkout` for source control) |

### Asset Management

| Command | Description |
|---------|-------------|
| `asset` | Canonical asset query, preview, source-control, offline file, creation, and save command family |
| `mutable` | Canonical Mutable/CustomizableObject inspection, graph authoring, and compile command family |
| `asset query` | Search the Content Browser by name, class, or path -- also inspect DataTables and map `WorldSettings` such as `DefaultGameMode` |
| `query-enum` | Inspect a UserDefinedEnum asset -- authored names, display names, tooltips, numeric values |
| `query-struct` | Inspect a UserDefinedStruct asset -- authored member names, defaults, and metadata |
| `mutable inspect graph` | Inspect a Mutable/CustomizableObject graph and return graphs, nodes, pins, edges, and derived node roles |
| `mutable inspect parameters` | Derive structured Mutable parameter metadata such as groups, defaults, runtime enum options, tags, and related graph links |
| `mutable inspect diagnostics` | Report Mutable plugin availability and best-effort capability/runtime diagnostics for a target asset |
| `mutable graph add-node` | Add a node to a Mutable/CustomizableObject graph by class name, with optional position and properties |
| `mutable graph add-parameter` | Add a common Mutable parameter node such as float, color, enum, projector, texture, transform, or mesh |
| `mutable graph add-mesh-option` | Add a skeletal or static mesh option node and assign its mesh reference |
| `mutable graph set-base-mesh` | Set the mesh reference on an existing CustomizableObject node |
| `mutable graph add-group-child` | Connect a child object node into an object group using Mutable's default `Object` -> `Objects` pins |
| `mutable graph set-node-property` | Set reflected properties or matching pin defaults on a CustomizableObject graph node |
| `mutable graph connect-pins` | Connect two CustomizableObject graph pins by node GUID and pin name, with one automatic pin regeneration retry by default |
| `mutable graph regenerate-node-pins` | Regenerate pins for one Mutable/CustomizableObject graph node and return the refreshed pin list |
| `mutable compile` | Compile a CustomizableObject asset and return structured status |
| `mutable graph remove-node` | Remove a CustomizableObject graph node by GUID, object path, object name, or title |
| `asset create` | Create new Blueprint, Material, DataTable, World (Level), or other asset types |
| `asset delete` | Delete an asset |
| `asset release-lock` | Best-effort close editors and release UE file handles for a specific asset |
| `asset set-property` | Set a property on a Blueprint CDO or component, including nested `InstancedStruct` members |
| `asset diff` | Get property-level diff of an asset vs. source control |
| `asset preview` | Get a thumbnail/preview image of an asset |
| `asset open` | Open an asset in the editor |
| `find-references` | Find assets, variables, or functions referencing a given asset |

### Material Inspection

| Command | Description |
|---------|-------------|
| `query-material` | Inspect Material, Material Instance, or Material Function -- parameters, nodes, connections, `--parent-chain` |
| `query-mpc` | Read or write Material Parameter Collection scalar/vector values |

### Class and Type Inspection

| Command | Description |
|---------|-------------|
| `class-hierarchy` | Inspect class inheritance chains -- ancestors, descendants, or both |
| `validate-class-path` | Verify that a soft class path exists, loads, and resolves to a `UClass` |

### Play-In-Editor (PIE) Control

| Command | Description |
|---------|-------------|
| `exec-console-command` | Execute arbitrary UE console commands directly in editor, PIE, or game worlds |
| `pie-session` | Start, stop, pause, resume PIE -- also query actor state during play |
| `pie-tick` | Start PIE if needed and advance the world deterministically by frame count |
| `anim` | Canonical animation instance, sync marker, graph authoring, transition, and Rewind command family |
| `anim instance inspect` | Snapshot a target actor's live `UAnimInstance` state or inspect static AnimBlueprint topology with `--asset-path` |
| `anim sync-marker inspect` | List AuthoredSyncMarkers on an AnimSequence asset |
| `anim sync-marker compare` | Compare sync marker names and timing across AnimSequence assets |
| `anim sync-marker add` | Add an AuthoredSyncMarker to an AnimSequence asset |
| `anim sync-marker remove` | Remove AuthoredSyncMarkers from an AnimSequence asset |
| `inspect-pawn-possession` | Inspect controller/pawn links, AI auto-possession, and hidden state in a running world |
| `trigger-input` | Send input events to a running game (PIE or packaged build) |

### Screenshot and Visual Capture

| Command | Description |
|---------|-------------|
| `capture` | Canonical capture command family for viewport and screenshot capture |
| `capture screenshot` | Capture the editor viewport, PIE window, or a specific editor panel |
| `capture viewport` | Capture the current viewport (auto-detects PIE, standalone, or editor) |
| `set-viewport-camera` | Set editor viewport camera position, rotation, or preset view (top/front/right/perspective) |

### Logging and Console Variables

| Command | Description |
|---------|-------------|
| `get-logs` | Read the UE output log with substring filters, cursors, and follow mode |
| `get-console-var` | Read the value of a console variable (CVar) |
| `set-console-var` | Set a console variable value |

### Gameplay Tags

| Command | Description |
|---------|-------------|
| `request-gameplay-tag` | Resolve a registered GameplayTag by name and return validity/export text |
| `reload-gameplay-tags` | Reload GameplayTags settings and refresh tag tables where supported |

### Python Scripting in UE

| Command | Description |
|---------|-------------|
| `run-python-script` | Execute a Python script inside UE's embedded Python interpreter, preserving normal file semantics for `--script-path` and exposing optional PIE-world helpers |
| `save-script` | Save a reusable Python script to the local script library |
| `list-scripts` | List all saved Python scripts |
| `delete-script` | Delete a saved script |

### StateTree Editing

| Command | Description |
|---------|-------------|
| `statetree` | Canonical StateTree inspection, state, task, and transition command family |
| `statetree inspect` | Inspect a StateTree asset -- states, tasks, transitions |
| `statetree state add` | Add a state to a StateTree |
| `statetree task add` | Add a task to a StateTree state |
| `statetree transition add` | Add a transition between StateTree states |
| `statetree state remove` | Remove a state from a StateTree |

### Widget Blueprint Inspection

| Command | Description |
|---------|-------------|
| `umg` | Canonical UMG command family for designer apply/inspect, navigation, preview lifecycle, verification, layout, and workflows |
| `umg designer inspect` | Inspect UMG Widget Blueprint hierarchy, bindings, properties, and input mapping key bindings |
| `inspect-runtime-widgets` | Inspect live UMG widget geometry during PIE sessions |
| `umg designer apply` | Build or replace a Widget Blueprint Designer hierarchy from a declarative JSON spec |
| `umg navigation wire` | Validate named Widget Blueprint buttons, switchers, and target widgets while exposing parent-class navigation binding contracts |
| `umg verify widgets` | Validate expected runtime widget names in PIE |
| `umg verify text` | Validate expected runtime TextBlock strings in PIE |
| `umg verify navigation` | Broadcast named UMG button clicks and assert switcher or visibility outcomes |
| `umg workflow run` | Run a UMG workflow plan artifact |
| `umg layout` | Unified concept-to-runtime layout pipeline for extraction, geometry/pixel comparison, subset matching, ignore masks, and spec fitting |
| `umg layout extract` | Normalize designer or runtime UMG geometry into a layout JSON artifact |
| `umg layout compare` | Compare expected and actual UMG layout artifacts offline |
| `add-widget` | Add a widget to a Widget Blueprint |

### DataTable Editing

| Command | Description |
|---------|-------------|
| `add-datatable-row` | Add or update a row in a DataTable asset from a JSON object keyed by row struct field name |

### Performance Profiling (UE Insights)

| Command | Description |
|---------|-------------|
| `insights-capture` | Start or stop a UE Insights trace capture |
| `insights-list-traces` | List available trace files |
| `insights-analyze` | Analyze a trace file for CPU, GPU, or memory hotspots |

### Rewind Debugger (Animation Debugging)

Requires the **Animation Insights (GameplayInsights)** plugin enabled in Edit > Plugins.

| Command | Description |
|---------|-------------|
| `anim rewind` | Canonical Rewind Debugger recording and inspection command family |
| `rewind-start` | Start a Rewind Debugger recording with channel and actor filtering, or load an existing `.utrace` file with `--load` |
| `rewind-stop` | Stop the current recording |
| `rewind-status` | Query current recording state (detects recordings from CLI or editor UI) |
| `rewind-list-tracks` | List all recorded actors and their available track types |
| `rewind-overview` | Track-level summary for an actor (state machine transitions, montage play ranges, notify fire times) |
| `rewind-snapshot` | Detailed animation state at a specific time or frame -- the time-travel equivalent of `anim instance inspect` |
| `rewind-save` | Save the in-memory recording to a `.utrace` file |

### Build and Live Coding

| Command | Description |
|---------|-------------|
| `build-and-relaunch` | Trigger a full C++ rebuild and optionally relaunch the editor; `--wait` monitors staged progress, and offline fallback can build from `--project` when the bridge is unavailable |
| `trigger-live-coding` | Trigger a Live Coding compile (hot reload); warns on risky reflected header changes and returns full-build guidance when Unreal cancels unsupported changes |
| `reload-bridge-module` | Reload the bridge editor module from disk without a full editor restart |

### Skills (LLM Workflow Prompts)

| Command | Description |
|---------|-------------|
| `skills list` | List all available LLM skill prompts shipped with the CLI |
| `skills get <name>` | Print a skill's full content to stdout for LLM consumption |

Skills are markdown prompts that teach an LLM client how to perform complex multi-step workflows using soft-ue-cli commands. They include step-by-step instructions, type mapping tables, and pre-filled CLI commands.

**Available skills:**

| Skill | Description |
|-------|-------------|
| `blueprint-to-cpp` | Generate C++ `.h`/`.cpp` from a Blueprint asset -- Layer 1 (class scaffolding) + Layer 2 (graph logic translation) |
| `author-umg-designer` | Convert a UI concept image plus text requirements into an editable UMG Designer tree JSON draft for `umg designer apply` |
| `author-umg-workflow` | Turn a UI concept into an editable UMG Designer tree, stable widget-name navigation contract, and PIE interaction verification plan |
| `level-from-image` | Populate a UE level from a reference image -- analyzes the image, maps scene elements to project assets, batch-places actors, then iterates with visual feedback (viewport screenshots) |
| `replay-changes` | Walk the binary-asset conflict recovery flow for Git or Perforce: extract base/local/remote revisions, inspect offline diffs, sync the incoming binary, and replay the wanted local edits manually |
| `test-tools` | Run the exhaustive live integration test script across CLI and MCP modes, including offline `.uasset` smoke checks against a generated Blueprint |

### MCP Server Mode

| Command | Description |
|---------|-------------|
| `mcp-serve` | Run as an MCP (Model Context Protocol) server over stdio |

Exposes 120+ commands as MCP tools and skills as MCP prompts. Compatible with Claude Desktop, Claude Code, Cursor, Windsurf, and other MCP clients. Requires the optional `mcp` extra:

```bash
pip install soft-ue-cli[mcp]
```

---

## Usage Examples

### Spawn an actor at a specific location

```bash
soft-ue-cli spawn-actor BP_Enemy --location 100,200,50 --rotation 0,90,0
```

### Query all actors of a specific class

```bash
soft-ue-cli query-level --class-filter StaticMeshActor --limit 50
soft-ue-cli query-level --world pie --search "BP_Player*"
soft-ue-cli get-property BP_Player_C_0 Health --world pie
```

### Call a BlueprintCallable function

```bash
soft-ue-cli call-function BP_GameMode SetDifficulty --args '{"Level": 3}'
```

### Compose deterministic runtime steps with one batch

```bash
soft-ue-cli batch-call --calls '[
  {"tool":"pie-tick","args":{"frames":1}},
  {"tool":"query-level","args":{"limit":5}},
  {"tool":"get-logs","args":{"lines":5}}
]'
```

### Sweep a pure callable on a transient instance

```bash
soft-ue-cli call-function --class-path /Script/Engine.Actor --function-name K2_GetActorLocation --spawn-transient
```

### Validate a class path before spawning

```bash
soft-ue-cli validate-class-path /Game/Characters/BP_Hero.BP_Hero_C
```

### Tick PIE and inspect animation state

```bash
soft-ue-cli pie-tick --frames 30
soft-ue-cli anim instance inspect --actor-tag TestCharacter --include state_machines,montages
```

### Execute a console command directly in PIE

```bash
soft-ue-cli exec-console-command stat fps
soft-ue-cli exec-console-command --player-index 0 MyGame.MyCommand arg1 arg2
```

### Inspect possession state during PIE

```bash
soft-ue-cli inspect-pawn-possession
soft-ue-cli inspect-pawn-possession --class-filter Character
```

### Inspect a Blueprint's components and variables

```bash
soft-ue-cli blueprint inspect /Game/Blueprints/BP_Player --include components,variables
```

### Build an editable UMG Designer tree

```bash
soft-ue-cli umg designer apply /Game/UI/WBP_MainMenu --spec-file menu_tree.json --compile --save
soft-ue-cli umg navigation wire /Game/UI/WBP_MainMenu --bindings-file navigation.json --compile --save
soft-ue-cli umg workflow run --plan umg_workflow_plan.json
soft-ue-cli umg layout extract --source concept-image --input concept.png --output concept_layout.json
soft-ue-cli umg layout extract --source designer --asset-path /Game/UI/WBP_MainMenu --output umg_expected_layout.json
soft-ue-cli umg layout extract --source runtime --root-widget WBP_MainMenu_C_0 --full-geometry --output umg_runtime_layout.json
soft-ue-cli umg layout compare --mode geometry --subset concept_layout.json umg_runtime_layout.json --output umg_layout_report.json
soft-ue-cli umg layout fit --concept concept_layout.json --actual umg_runtime_layout.json --spec menu_tree.json --output corrected_menu_tree.json
soft-ue-cli umg designer inspect /Game/UI/WBP_MainMenu --include-defaults --depth-limit 8
soft-ue-cli skills get author-umg-workflow
```

`umg navigation wire` fails fast while PIE is active or the editor is saving/garbage collecting because it mutates WidgetBlueprint assets. Stop PIE first, wait until the editor is idle, or pass `--allow-pie` / `--allow-busy` when you intentionally accept that risk.

### Inspect and diff local `.uasset` files offline

```bash
soft-ue-cli asset inspect-file D:/Project/Content/Blueprints/BP_Player.uasset --sections all
soft-ue-cli asset inspect-file D:/Project/Content/Characters/SK_Mannequin_Skeleton.uasset --sections properties
soft-ue-cli asset inspect-file D:/Project/Content/__ExternalActors__/Maps/OpenWorld/5/TQ/ABC123.uasset --sections summary,properties
soft-ue-cli asset diff-file D:/snapshots/BP_Player_before.uasset D:/Project/Content/Blueprints/BP_Player.uasset --sections variables,functions
soft-ue-cli asset diff-file D:/snapshots/SK_before.uasset D:/Project/Content/Characters/SK_Mannequin_Skeleton.uasset --sections properties
soft-ue-cli asset diff-file D:/snapshots/Actor_before.uasset D:/Project/Content/__ExternalActors__/Maps/OpenWorld/5/TQ/ABC123.uasset --sections summary,properties
```

### Inspect UserDefinedEnum and UserDefinedStruct assets

```bash
soft-ue-cli query-enum /Game/Data/E_TraversalActionType
soft-ue-cli query-struct /Game/Data/S_TraversalCheckResult
```

### Inspect Mutable / CustomizableObject assets safely

```bash
soft-ue-cli inspect-customizable-object-graph /Game/Characters/CO_Hero.CO_Hero
soft-ue-cli inspect-mutable-parameters /Game/Characters/CO_Hero.CO_Hero
soft-ue-cli inspect-mutable-diagnostics /Game/Characters/CO_Hero.CO_Hero
```

### Edit Mutable / CustomizableObject assets

```bash
soft-ue-cli mutable graph add-parameter /Game/Characters/CO_Hero.CO_Hero BodyHeight --parameter-type float
soft-ue-cli mutable graph add-mesh-option /Game/Characters/CO_Hero.CO_Hero /Game/Meshes/SKM_Boots.SKM_Boots
soft-ue-cli mutable graph add-group-child /Game/Characters/CO_Hero.CO_Hero <group-node-guid> <child-node-guid>
soft-ue-cli regenerate-co-node-pins /Game/Characters/CO_Hero.CO_Hero <node-guid>
soft-ue-cli connect-co-pins /Game/Characters/CO_Hero.CO_Hero <source-node-guid> Value <target-node-guid> Input
soft-ue-cli mutable compile /Game/Characters/CO_Hero.CO_Hero
soft-ue-cli remove-co-node /Game/Characters/CO_Hero.CO_Hero <node-guid>
```

### Start a PIE session and send input

```bash
soft-ue-cli pie-session start --mode SelectedViewport
soft-ue-cli trigger-input key --key SpaceBar
soft-ue-cli trigger-input action --action-name IA_SwitchCharacter
soft-ue-cli pie-session stop
```

### Capture a screenshot of the editor viewport

```bash
soft-ue-cli capture screenshot --source viewport --output file
soft-ue-cli capture viewport --source editor --scale 50 --color-mode grayscale --output file
soft-ue-cli capture viewport --source editor --width 960 --color-mode monochrome --output file
```

### Edit a Blueprint graph programmatically

```bash
soft-ue-cli blueprint node add /Game/BP_Player K2Node_CallFunction \
  --properties '{"FunctionReference": {"MemberName": "PrintString"}}'
soft-ue-cli blueprint pin connect /Game/BP_Player node1 "exec" node2 "execute"
```

### Manage Blueprint interfaces

```bash
soft-ue-cli modify-interface /Game/ABP_Character add ALI_Locomotion
soft-ue-cli modify-interface /Game/ABP_Character remove ALI_Locomotion
soft-ue-cli blueprint inspect /Game/ABP_Character --include interfaces
```

### Create an anim layer function on an AnimLayerInterface

```bash
soft-ue-cli blueprint node add /Game/ALI_Locomotion AnimLayerFunction --graph-name FullBody
```

### Create an AnimBlueprint state machine

```bash
soft-ue-cli add-anim-state-machine /Game/ABP_Hero Locomotion --default-state Idle
soft-ue-cli add-anim-state /Game/ABP_Hero Locomotion Run --position 500,0
soft-ue-cli add-anim-transition /Game/ABP_Hero Locomotion Idle Run --rule true
soft-ue-cli blueprint node add /Game/ABP_Hero AnimGraphNode_SequencePlayer --graph-name Run
```

### Insert a node between two connected nodes

```bash
soft-ue-cli insert-graph-node /Game/ABP_Hero AnimGraphNode_LinkedAnimLayer \
  {source-guid} OutputPose {target-guid} InputPose --graph-name AnimGraph
```

### Save and compile after edits

```bash
soft-ue-cli compile-blueprint /Game/ABP_Hero
soft-ue-cli save-asset /Game/ABP_Hero
```

### Refresh GameplayTags after editing config

```bash
soft-ue-cli reload-gameplay-tags
soft-ue-cli request-gameplay-tag Status.Effect.Burning
```

### Disconnect a specific wire (preserving others)

```bash
soft-ue-cli disconnect-graph-pin /Game/ABP_Hero {node-guid} OutputPose \
  --target-node {other-guid} --target-pin InputPose
```

### Convert a Blueprint to C++ using the LLM skill

```bash
# List available skills
soft-ue-cli skills list

# Feed the blueprint-to-cpp skill to your LLM client
soft-ue-cli skills get blueprint-to-cpp
# The LLM reads the skill instructions, then runs:
#   soft-ue-cli query-enum /Game/Data/E_Dependency
#   soft-ue-cli query-struct /Game/Data/S_Dependency
#   soft-ue-cli blueprint inspect /Game/BP_Player --include all --include-inherited
#   soft-ue-cli blueprint graph inspect /Game/BP_Player --list-callables
# ...and generates the .h/.cpp files from the JSON responses
```

### Populate a level from a reference image

```bash
# Get the level-from-image skill instructions
soft-ue-cli skills get level-from-image
# The LLM analyzes the image, searches for matching assets, places them,
# then enters a visual feedback loop:
#   soft-ue-cli set-viewport-camera --preset top --ortho-width 8000
#   soft-ue-cli capture viewport --source editor --output file
#   soft-ue-cli capture viewport --source editor --scale 50 --color-mode grayscale
# Compares screenshot to reference, auto-corrects, then asks for human feedback
```

### Profile with UE Insights

```bash
soft-ue-cli insights-capture start --channels CPU,GPU
# ... run your scenario ...
soft-ue-cli insights-capture stop
soft-ue-cli insights-analyze latest --analysis-type cpu
```

### Use as an MCP server (Claude Desktop, Cursor, etc.)

```bash
# Install with MCP support
pip install soft-ue-cli[mcp]

# Run the MCP server (used in MCP client config, not run manually)
soft-ue-cli mcp-serve
```

Add to your MCP client config (e.g. `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "soft-ue-cli": {
      "command": "soft-ue-cli",
      "args": ["mcp-serve"]
    }
  }
}
```

---

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `SOFT_UE_BRIDGE_URL` | *(none)* | Full bridge URL override (e.g. `http://192.168.1.10:8080`) |
| `SOFT_UE_BRIDGE_PORT` | `8080` | Port override when using localhost |
| `SOFT_UE_BRIDGE` | *(none)* | Set to `1` to enable conditional compilation in `Target.cs` |

### Server Discovery Order

The CLI finds the bridge server using this priority:

1. `--server` command-line flag
2. `SOFT_UE_BRIDGE_URL` environment variable
3. `SOFT_UE_BRIDGE_PORT` environment variable (constructs `http://127.0.0.1:<port>`)
4. `.soft-ue-bridge/instance.json` file (searched upward from the current working directory -- written automatically by the plugin at startup)
5. `http://127.0.0.1:8080` (default fallback)

### Conditional Compilation for Teams

If you want only specific developers to compile the bridge plugin (to avoid any overhead for artists or designers), use the `SOFT_UE_BRIDGE` environment variable in your `Target.cs`:

```csharp
// MyGameEditor.Target.cs
if (Environment.GetEnvironmentVariable("SOFT_UE_BRIDGE") == "1")
{
    ExtraModuleNames.Add("SoftUEBridge");
}
```

Developers who need the bridge set `SOFT_UE_BRIDGE=1` in their environment. Everyone else builds without it.

---

## Compatibility

| Requirement | Supported Versions |
|-------------|--------------------|
| **Unreal Engine** | 5.7 |
| **Python** | 3.10+ |
| **Platforms** | Windows, macOS, Linux |
| **Build types** | Editor, DebugGame, Development; Shipping only if explicitly opted in |
| **Dependencies** | `httpx >= 0.27`, `Pillow >= 10`; optional `mcp >= 1.2` for MCP server mode |

---

## Development

```bash
git clone https://github.com/softdaddy-o/soft-ue-cli
cd soft-ue-cli
pip install -e .
pytest -v
```

---

## Feedback

### Report a bug

```bash
soft-ue-cli report-bug \
  --title "Short bug summary" \
  --description "Detailed description"
```

Do not include project-specific information, personal information, or any clue that could identify your project. Replace project names, internal paths, asset names, emails, tokens, and other sensitive details with generic placeholders.

Optional flags: `--steps`, `--expected`, `--actual`, `--severity critical|major|minor`, `--no-system-info`.

### Request a feature

```bash
soft-ue-cli request-feature \
  --title "Short feature summary" \
  --description "What the feature should do"
```

Do not include project-specific information, personal information, or any clue that could identify your project. Replace project names, internal paths, asset names, emails, tokens, and other sensitive details with generic placeholders.

Optional flags: `--use-case`, `--priority enhancement|nice-to-have`.

### Share a testimonial

```bash
soft-ue-cli submit-testimonial \
  --message "Great tool for UE automation!" \
  --agent-name "Claude Code" \
  --rating 5
```

Opens a GitHub Issue (label: `testimonial`) with auto-collected metadata (CLI version, usage streak, top tools). A consent prompt appears before posting unless `--yes` is passed.

All feedback commands require GitHub auth: set `GITHUB_TOKEN` env var or run `gh auth login`.

---

## Frequently Asked Questions

### What is soft-ue-cli?

soft-ue-cli is a Python tool that gives AI agents and developers 120+ commands and tools for Unreal Engine 5 automation. It works as an **MCP server** (for Claude Desktop, Cursor, Windsurf, and other MCP clients) or as a **standard CLI** (for Claude Code, shell scripts, CI/CD). It communicates with a C++ plugin (SoftUEBridge) running inside UE via HTTP/JSON-RPC for live editor/runtime work, and it also includes offline parsers for local Unreal files.

### How do AI agents use soft-ue-cli?

**MCP clients** (Claude Desktop, Cursor, Windsurf): Connect via `soft-ue-cli mcp-serve`. The agent sees 120+ tools with typed schemas and skill prompts -- it can directly call UE operations without going through a terminal.

**Claude Code**: Runs soft-ue-cli commands in the terminal. Add a `CLAUDE.md` file to your UE project describing available commands, and Claude Code autonomously queries your level, spawns actors, edits Blueprints, runs PIE sessions, and iterates on your game.

### Can I use soft-ue-cli without an AI agent?

Yes. soft-ue-cli is a standard Python CLI. You can use it in shell scripts, CI/CD pipelines, custom automation tools, or any workflow that can invoke command-line programs. Every command outputs structured JSON, making it easy to parse and integrate.

### Does it work with packaged/cooked Unreal Engine builds?

Yes, in Development and DebugGame packaged builds by default. The bridge module now uses Unreal's `DeveloperTool` module type, so Shipping builds exclude it unless the target explicitly enables developer tools (for example via `bBuildDeveloperTools = true`).

The plugin descriptor restricts SoftUEBridge's editor-only dependency plugins to Editor targets, so Python/editor scripting dependencies are not enabled for packaged game targets.

### What Unreal Engine versions are supported?

soft-ue-cli is actively developed against Unreal Engine 5.7. That matters if your project cannot move to UE 5.8 yet but still needs an agent-friendly CLI/MCP automation surface.

### Is there any runtime performance impact?

The SoftUEBridge plugin adds a lightweight HTTP server that listens on a single port. When no requests are being made, the overhead is negligible. The server processes requests on the game thread to ensure thread safety with UE APIs. Shipping builds exclude the bridge by default; if you intentionally need it there, enable developer tools for that target.

### How do I change the default port?

Set the `SOFT_UE_BRIDGE_PORT` environment variable before launching UE, or use the `--server` flag when running CLI commands. The default port is 8080.

### Can multiple UE instances run simultaneously?

Yes. Each UE instance writes its port to a `.soft-ue-bridge/instance.json` file in the project directory. Use `SOFT_UE_BRIDGE_URL` or `--server` to target a specific instance when multiple are running.

### How do I edit Blueprints from the command line?

Use `blueprint graph inspect` to inspect existing graph nodes, `blueprint node add` to create new nodes, `blueprint pin connect` to wire them together, and `blueprint node remove` to delete nodes. This enables fully programmatic Blueprint construction -- useful for AI-driven development and automated testing.

### What is the difference between soft-ue-cli and Unreal Engine Remote Control?

Unreal Engine's built-in Remote Control API focuses on property access and preset-based workflows. soft-ue-cli provides a broader command set specifically designed for AI coding agents -- including Blueprint graph editing, StateTree manipulation, PIE session control, UE Insights profiling, widget inspection, and asset creation -- with a simpler setup process (one pip install, one plugin copy).

### What is the difference between soft-ue-cli and UE 5.8's first-party MCP?

UE 5.8's first-party MCP support is the right first stop if your project is already on UE 5.8 and you want Epic's native editor MCP endpoint. soft-ue-cli remains useful when you need UE 5.7 support, cooked Development/DebugGame build support, the same commands from a terminal or CI job, offline `.uasset`/config inspection, packaged LLM workflow prompts, or structured command metadata and missing-plugin recovery across a broader curated tool surface.

They are complementary. Use the official MCP surface where it covers the editor action directly; use soft-ue-cli when the workflow spans UE 5.7 projects, cooked non-Shipping builds, CLI automation, offline files, canonical command scripts, visual capture transforms, UMG verification, or optional plugin diagnostics.

### How do I use soft-ue-cli with Claude Desktop or Cursor?

Run `pip install soft-ue-cli[mcp]` to install MCP support, then add the server to your MCP client config. For Claude Desktop, add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "soft-ue-cli": {
      "command": "soft-ue-cli",
      "args": ["mcp-serve"]
    }
  }
}
```

The MCP server exposes 120+ commands as MCP tools and skills as MCP prompts. The AI editor can then directly call UE operations without going through the terminal.

### What is the difference between soft-ue-cli and other UE MCP servers?

| | soft-ue-cli | unreal-mcp, ue5-mcp, etc. |
|---|---|---|
| **Tools** | 120+ commands/tools | Varies |
| **Coverage** | Blueprints, materials, StateTrees, Mutable, widgets, PIE, profiling, DataTables, CVars, Live Coding, offline file/config inspection | Varies; many focus on live editor operations |
| **LLM skill prompts** | Yes (MCP prompts + CLI) | No |
| **CLI mode** | Yes ??shell scripts, CI/CD, Claude Code | MCP-only |
| **Setup** | `pip install soft-ue-cli[mcp]` + copy one plugin | Varies; often requires custom C++/Python scripting |

---

## Support this project

soft-ue-cli is free, open-source, and maintained by one person. If it saves you hours of manual editor work or helps your AI workflow, consider supporting continued development:

- [Sponsor on GitHub](https://github.com/sponsors/softdaddy-o) ??recurring or one-time
- [Buy me a coffee on Ko-fi](https://ko-fi.com/softdaddy) ??quick one-time donation

Using soft-ue-cli in your project? [Share your experience](https://github.com/softdaddy-o/soft-ue-cli/issues/new?labels=testimonial&title=Testimonial) ??I'd love to hear about it.

---

## Roadmap

- Track UE 5.8 compatibility and first-party MCP overlap, keeping soft-ue-cli focused on CLI/CI, offline inspection, and curated bridge workflows.
- More LLM skills (Material-to-HLSL, Animation Blueprint automation)
- Visual diff for Blueprint changes
- CI/CD integration examples

---

## License

MIT License. See [LICENSE](https://github.com/softdaddy-o/soft-ue-cli/blob/main/LICENSE) for details.

---

## Links

- **PyPI**: [pypi.org/project/soft-ue-cli](https://pypi.org/project/soft-ue-cli/)
- **GitHub**: [github.com/softdaddy-o/soft-ue-cli](https://github.com/softdaddy-o/soft-ue-cli)
- **Claude Code**: [docs.anthropic.com/en/docs/claude-code](https://docs.anthropic.com/en/docs/claude-code)
