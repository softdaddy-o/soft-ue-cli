# soft-ue-cli (+mcp)

[![PyPI version](https://img.shields.io/pypi/v/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![Python 3.10+](https://img.shields.io/pypi/pyversions/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![AI agents](https://img.shields.io/badge/AI_agents-ready-7c3aed)](#why-soft-ue-cli)
[![skills](https://img.shields.io/badge/skills-11-84cc16)](#skills-llm-workflow-prompts)
[![tools](https://img.shields.io/badge/tools-60%2B-f97316)](#complete-command-reference)
[![MCP](https://img.shields.io/badge/MCP-server-0ea5e9)](#mcp-server-mode)
[![AI built for coding agents](https://img.shields.io/badge/AI_built_for-coding_agents-6b7280)](#why-soft-ue-cli)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub_Sponsors-Support_this_project-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/softdaddy-o)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Buy_me_a_coffee-ff5e5b?logo=ko-fi&logoColor=white)](https://ko-fi.com/softdaddy)

Built and maintained by a solo developer. [Support this project](#support-this-project) if it saves you time.

This project was originally created to help friends work with Unreal Engine more effectively.

**Control Unreal Engine 5 from your AI agent or terminal.** soft-ue-cli gives any LLM — via **MCP server** or **CLI** — 60+ tools to spawn actors, edit Blueprints, inspect materials, read and patch UE config files, run Play-In-Editor sessions, capture screenshots, profile performance, and more inside a running UE5 editor or packaged build.

Two connection paths. Same package. Bridge tools when Unreal is running, offline tools when it is not.

## Project Status

Public development of this repository is winding down.

Future work may continue in a private environment because ongoing development can involve proprietary employer-owned code that cannot be published here.

The code already published in this repository remains available as a prototype and reference implementation. You are welcome to use it, fork it, and extend it in accordance with the repository license.

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
           -> inspect-uasset, diff-uasset, config tree/get/diff/audit, skills
```

---

## Why soft-ue-cli?

- **MCP server + CLI in one package** -- use as an MCP server (`mcp-serve`) for Claude Desktop, Cursor, Windsurf, and other MCP clients, **or** as a standard CLI for Claude Code, shell scripts, and CI/CD. Same 60+ tools either way.
- **AI-native UE automation** -- purpose-built so LLM agents can read, modify, and test Unreal Engine projects without a human touching the editor.
- **60+ tools** covering actors, Blueprints, materials, StateTrees, widgets, assets, config files, PIE sessions, profiling, and more.
- **Online + offline workflows** -- bridge-backed UE mutation and runtime inspection when Unreal is open, plus direct local inspection, diff, and config tooling when it is not.
- **Config-aware workflows** — inspect hierarchy, trace overrides, diff layers, and patch `.ini`, `BuildConfiguration.xml`, and `.uproject` data from one `config` command group.
- **LLM skill prompts** -- ships with markdown workflows (e.g. Blueprint-to-C++ conversion) exposed as MCP prompts or CLI commands.
- **Works everywhere UE runs** -- editor, cooked builds, Windows, macOS, Linux.
- **Single dependency** -- only requires `httpx`. Add `[mcp]` extra for MCP server mode.
- **Team-friendly** -- conditional compilation via `SOFT_UE_BRIDGE` environment variable means only developers who need the bridge get it compiled in.

---

## Quick Start

### 1. Install

```bash
pip install soft-ue-cli          # CLI only
pip install soft-ue-cli[mcp]     # CLI + MCP server
```

### 2. Install the plugin into your UE project

Run the setup command **inside your LLM client** (Claude Code, Cursor, etc.) — it outputs step-by-step instructions that the AI agent will follow to copy the plugin, edit your `.uproject`, and configure itself:

```bash
soft-ue-cli setup /path/to/YourProject
```

If you're running manually (not via an LLM), follow the printed instructions yourself: copy the plugin directory, add the `"Plugins"` entry to your `.uproject`, and create the `CLAUDE.md` snippet.

### 3. Rebuild and launch Unreal Engine

After regenerating project files and rebuilding, launch the editor. Look for this log line to confirm the bridge is running:

```
LogSoftUEBridge: Bridge server started on port 8080
```

### 5. Verify the connection

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

The AI editor now has direct access to all 60+ UE tools and skill prompts — no terminal needed.

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
           -> inspect-uasset, diff-uasset, config *, skills get/list
```

The **SoftUEBridge** plugin is a lightweight C++ `UGameInstanceSubsystem` that starts an embedded HTTP server on port 8080 when UE launches. Bridge-backed commands send JSON-RPC requests to this server, and the plugin executes the corresponding UE operations on the game thread, returning structured JSON responses. Offline commands bypass the bridge entirely and operate directly on local files.

All commands output JSON to stdout (except `get-logs --raw`). Exit code 0 means success, 1 means error.

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
LLM executes soft-ue-cli commands (query-blueprint, query-blueprint-graph, ...)
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
    |  Reuses call_tool() — HTTP/JSON-RPC
    v
SoftUEBridge plugin (inside UE)
```

Running `soft-ue-cli mcp-serve` starts an MCP server over stdio. It auto-generates MCP tool schemas from the CLI's argparse parser and forwards tool calls to the UE bridge. Skills are exposed as MCP prompts. Install the optional extra: `pip install soft-ue-cli[mcp]`.

---

## Complete Command Reference

Every command is available via `soft-ue-cli <command>`. Run `soft-ue-cli <command> --help` for detailed options.

### Setup and Diagnostics

| Command | Description |
|---------|-------------|
| `setup` | Copy SoftUEBridge plugin into a UE project |
| `check-setup` | Verify plugin files, .uproject settings, and bridge server reachability |
| `status` | Health check -- returns server status |
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
| `query-blueprint` | Inspect a Blueprint asset -- components, variables, functions, interfaces, event dispatchers |
| `query-blueprint-graph` | Inspect event graphs, function graphs, and node connections |
| `inspect-uasset` | Inspect a local `.uasset` file offline by parsed metadata, with best support for Blueprint and External Actor assets |
| `diff-uasset` | Diff two local `.uasset` files offline by parsed metadata, with best support for Blueprint and External Actor assets |
| `add-graph-node` | Add a node to a Blueprint or Material graph (supports `AnimLayerFunction` for ALIs) |
| `modify-interface` | Add or remove an implemented interface on a Blueprint or AnimBlueprint |
| `remove-graph-node` | Remove a node from a graph |
| `connect-graph-pins` | Connect two pins between graph nodes |
| `disconnect-graph-pin` | Disconnect pin connections (all or specific with `--target-node`/`--target-pin`) |
| `insert-graph-node` | Atomically insert a node between two connected nodes |
| `set-node-position` | Batch-set node positions for graph layout |
| `compile-blueprint` | Compile a Blueprint or AnimBlueprint and return the result |
| `compile-material` | Compile a Material, MaterialInstance, or MaterialFunction |
| `save-asset` | Save a modified asset to disk (with optional `--checkout` for source control) |
| `set-node-property` | Set properties on a graph node by GUID (UPROPERTY, inner structs, pin defaults) |

### Asset Management

| Command | Description |
|---------|-------------|
| `query-asset` | Search the Content Browser by name, class, or path -- also inspect DataTables and map `WorldSettings` such as `DefaultGameMode` |
| `query-enum` | Inspect a UserDefinedEnum asset -- authored names, display names, tooltips, numeric values |
| `query-struct` | Inspect a UserDefinedStruct asset -- authored member names, defaults, and metadata |
| `create-asset` | Create new Blueprint, Material, DataTable, World (Level), or other asset types |
| `delete-asset` | Delete an asset |
| `release-asset-lock` | Best-effort close editors and release UE file handles for a specific asset |
| `set-asset-property` | Set a property on a Blueprint CDO or component, including nested `InstancedStruct` members |
| `get-asset-diff` | Get property-level diff of an asset vs. source control |
| `get-asset-preview` | Get a thumbnail/preview image of an asset |
| `open-asset` | Open an asset in the editor |
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
| `inspect-anim-instance` | Snapshot a target actor's live `UAnimInstance` state, montages, slot activity, and blend weights |
| `inspect-pawn-possession` | Inspect controller/pawn links, AI auto-possession, and hidden state in a running world |
| `trigger-input` | Send input events to a running game (PIE or packaged build) |

### Screenshot and Visual Capture

| Command | Description |
|---------|-------------|
| `capture-screenshot` | Capture the editor viewport, PIE window, or a specific editor panel |
| `capture-viewport` | Capture the current viewport (auto-detects PIE, standalone, or editor) |
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
| `query-statetree` | Inspect a StateTree asset -- states, tasks, transitions |
| `add-statetree-state` | Add a state to a StateTree |
| `add-statetree-task` | Add a task to a StateTree state |
| `add-statetree-transition` | Add a transition between StateTree states |
| `remove-statetree-state` | Remove a state from a StateTree |

### Widget Blueprint Inspection

| Command | Description |
|---------|-------------|
| `inspect-widget-blueprint` | Inspect UMG Widget Blueprint hierarchy, bindings, properties, and input mapping key bindings |
| `inspect-runtime-widgets` | Inspect live UMG widget geometry during PIE sessions |
| `add-widget` | Add a widget to a Widget Blueprint |

### DataTable Editing

| Command | Description |
|---------|-------------|
| `add-datatable-row` | Add or update a row in a DataTable asset |

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
| `rewind-start` | Start a Rewind Debugger recording with channel and actor filtering, or load an existing `.utrace` file with `--load` |
| `rewind-stop` | Stop the current recording |
| `rewind-status` | Query current recording state (detects recordings from CLI or editor UI) |
| `rewind-list-tracks` | List all recorded actors and their available track types |
| `rewind-overview` | Track-level summary for an actor (state machine transitions, montage play ranges, notify fire times) |
| `rewind-snapshot` | Detailed animation state at a specific time or frame — the time-travel equivalent of `inspect-anim-instance` |
| `rewind-save` | Save the in-memory recording to a `.utrace` file |

### Build and Live Coding

| Command | Description |
|---------|-------------|
| `build-and-relaunch` | Trigger a full C++ rebuild and optionally relaunch the editor (`--wait` to monitor progress) |
| `trigger-live-coding` | Trigger a Live Coding compile (hot reload); warns on risky reflected header changes |

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
| `level-from-image` | Populate a UE level from a reference image -- analyzes the image, maps scene elements to project assets, batch-places actors, then iterates with visual feedback (viewport screenshots) |
| `replay-changes` | Walk the binary-asset conflict recovery flow for Git or Perforce: extract base/local/remote revisions, inspect offline diffs, sync the incoming binary, and replay the wanted local edits manually |
| `test-tools` | Run the exhaustive live integration test script across CLI and MCP modes, including offline `.uasset` smoke checks against a generated Blueprint |

### MCP Server Mode

| Command | Description |
|---------|-------------|
| `mcp-serve` | Run as an MCP (Model Context Protocol) server over stdio |

Exposes all 60+ commands as MCP tools and skills as MCP prompts. Compatible with Claude Desktop, Claude Code, Cursor, Windsurf, and other MCP clients. Requires the optional `mcp` extra:

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
soft-ue-cli inspect-anim-instance --actor-tag TestCharacter --include state_machines,montages
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
soft-ue-cli query-blueprint /Game/Blueprints/BP_Player --include components,variables
```

### Inspect and diff local `.uasset` files offline

```bash
soft-ue-cli inspect-uasset D:/Project/Content/Blueprints/BP_Player.uasset --sections all
soft-ue-cli inspect-uasset D:/Project/Content/__ExternalActors__/Maps/OpenWorld/5/TQ/ABC123.uasset --sections summary,properties
soft-ue-cli diff-uasset D:/snapshots/BP_Player_before.uasset D:/Project/Content/Blueprints/BP_Player.uasset --sections variables,functions
soft-ue-cli diff-uasset D:/snapshots/Actor_before.uasset D:/Project/Content/__ExternalActors__/Maps/OpenWorld/5/TQ/ABC123.uasset --sections summary,properties
```

### Inspect UserDefinedEnum and UserDefinedStruct assets

```bash
soft-ue-cli query-enum /Game/Data/E_TraversalActionType
soft-ue-cli query-struct /Game/Data/S_TraversalCheckResult
```

### Start a PIE session and send input

```bash
soft-ue-cli pie-session start --mode SelectedViewport
soft-ue-cli trigger-input key --key SpaceBar
soft-ue-cli pie-session stop
```

### Capture a screenshot of the editor viewport

```bash
soft-ue-cli capture-screenshot viewport --output screenshot.png
```

### Edit a Blueprint graph programmatically

```bash
soft-ue-cli add-graph-node /Game/BP_Player K2Node_CallFunction \
  --properties '{"FunctionReference": {"MemberName": "PrintString"}}'
soft-ue-cli connect-graph-pins /Game/BP_Player node1 "exec" node2 "execute"
```

### Manage Blueprint interfaces

```bash
soft-ue-cli modify-interface /Game/ABP_Character add ALI_Locomotion
soft-ue-cli modify-interface /Game/ABP_Character remove ALI_Locomotion
soft-ue-cli query-blueprint /Game/ABP_Character --include interfaces
```

### Create an anim layer function on an AnimLayerInterface

```bash
soft-ue-cli add-graph-node /Game/ALI_Locomotion AnimLayerFunction --graph-name FullBody
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
#   soft-ue-cli query-blueprint /Game/BP_Player --include all --include-inherited
#   soft-ue-cli query-blueprint-graph /Game/BP_Player --list-callables
# ...and generates the .h/.cpp files from the JSON responses
```

### Populate a level from a reference image

```bash
# Get the level-from-image skill instructions
soft-ue-cli skills get level-from-image
# The LLM analyzes the image, searches for matching assets, places them,
# then enters a visual feedback loop:
#   soft-ue-cli set-viewport-camera --preset top --ortho-width 8000
#   soft-ue-cli capture-viewport --source editor --output file
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
| **Dependencies** | `httpx >= 0.27` (sole runtime dependency); optional `mcp >= 1.2` for MCP server mode |

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

Do not include project-specific information or personal information. Replace project names, internal paths, asset names, emails, tokens, and other sensitive details with generic placeholders.

Optional flags: `--steps`, `--expected`, `--actual`, `--severity critical|major|minor`, `--no-system-info`.

### Request a feature

```bash
soft-ue-cli request-feature \
  --title "Short feature summary" \
  --description "What the feature should do"
```

Do not include project-specific information or personal information. Replace project names, internal paths, asset names, emails, tokens, and other sensitive details with generic placeholders.

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

soft-ue-cli is a Python tool that gives AI agents and developers 60+ operations to control Unreal Engine 5. It works as an **MCP server** (for Claude Desktop, Cursor, Windsurf, and other MCP clients) or as a **standard CLI** (for Claude Code, shell scripts, CI/CD). It communicates with a C++ plugin (SoftUEBridge) running inside UE via HTTP/JSON-RPC, enabling actor spawning, Blueprint editing, material inspection, Play-In-Editor sessions, screenshot capture, performance profiling, and more.

### How do AI agents use soft-ue-cli?

**MCP clients** (Claude Desktop, Cursor, Windsurf): Connect via `soft-ue-cli mcp-serve`. The agent sees all 60+ tools with typed schemas and skill prompts — it can directly call UE operations without going through a terminal.

**Claude Code**: Runs soft-ue-cli commands in the terminal. Add a `CLAUDE.md` file to your UE project describing available commands, and Claude Code autonomously queries your level, spawns actors, edits Blueprints, runs PIE sessions, and iterates on your game.

### Can I use soft-ue-cli without an AI agent?

Yes. soft-ue-cli is a standard Python CLI. You can use it in shell scripts, CI/CD pipelines, custom automation tools, or any workflow that can invoke command-line programs. Every command outputs structured JSON, making it easy to parse and integrate.

### Does it work with packaged/cooked Unreal Engine builds?

Yes, in Development and DebugGame packaged builds by default. The bridge module now uses Unreal's `DeveloperTool` module type, so Shipping builds exclude it unless the target explicitly enables developer tools (for example via `bBuildDeveloperTools = true`).

The plugin descriptor restricts SoftUEBridge's editor-only dependency plugins to Editor targets, so Python/editor scripting dependencies are not enabled for packaged game targets.

### What Unreal Engine versions are supported?

soft-ue-cli is actively developed against Unreal Engine 5.7.

### Is there any runtime performance impact?

The SoftUEBridge plugin adds a lightweight HTTP server that listens on a single port. When no requests are being made, the overhead is negligible. The server processes requests on the game thread to ensure thread safety with UE APIs. Shipping builds exclude the bridge by default; if you intentionally need it there, enable developer tools for that target.

### How do I change the default port?

Set the `SOFT_UE_BRIDGE_PORT` environment variable before launching UE, or use the `--server` flag when running CLI commands. The default port is 8080.

### Can multiple UE instances run simultaneously?

Yes. Each UE instance writes its port to a `.soft-ue-bridge/instance.json` file in the project directory. Use `SOFT_UE_BRIDGE_URL` or `--server` to target a specific instance when multiple are running.

### How do I edit Blueprints from the command line?

Use `query-blueprint-graph` to inspect existing graph nodes, `add-graph-node` to create new nodes, `connect-graph-pins` to wire them together, and `remove-graph-node` to delete nodes. This enables fully programmatic Blueprint construction -- useful for AI-driven development and automated testing.

### What is the difference between soft-ue-cli and Unreal Engine Remote Control?

Unreal Engine's built-in Remote Control API focuses on property access and preset-based workflows. soft-ue-cli provides a broader command set specifically designed for AI coding agents -- including Blueprint graph editing, StateTree manipulation, PIE session control, UE Insights profiling, widget inspection, and asset creation -- with a simpler setup process (one pip install, one plugin copy).

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

The MCP server exposes all 60+ commands as MCP tools and skills as MCP prompts. The AI editor can then directly call UE operations without going through the terminal.

### What is the difference between soft-ue-cli and other UE MCP servers?

| | soft-ue-cli | unreal-mcp, ue5-mcp, etc. |
|---|---|---|
| **Tools** | 60+ | 10–49 |
| **Coverage** | Blueprints, materials, StateTrees, widgets, PIE, profiling, DataTables, CVars, Live Coding | Varies; most cover actors + basic assets |
| **LLM skill prompts** | Yes (MCP prompts + CLI) | No |
| **CLI mode** | Yes — shell scripts, CI/CD, Claude Code | MCP-only |
| **Setup** | `pip install soft-ue-cli[mcp]` + copy one plugin | Varies; often requires custom C++/Python scripting |

---

## Support this project

soft-ue-cli is free, open-source, and maintained by one person. If it saves you hours of manual editor work or helps your AI workflow, consider supporting continued development:

- [Sponsor on GitHub](https://github.com/sponsors/softdaddy-o) — recurring or one-time
- [Buy me a coffee on Ko-fi](https://ko-fi.com/softdaddy) — quick one-time donation

Using soft-ue-cli in your project? [Share your experience](https://github.com/softdaddy-o/soft-ue-cli/issues/new?labels=testimonial&title=Testimonial) — I'd love to hear about it.

---

## Roadmap

- UE 5.8 support
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
