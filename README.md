# soft-ue-cli

[![PyPI version](https://img.shields.io/pypi/v/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![Python 3.10+](https://img.shields.io/pypi/pyversions/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

**Control Unreal Engine 5 from the command line.** soft-ue-cli is a Python CLI that lets [Claude Code](https://docs.anthropic.com/en/docs/claude-code) (Anthropic's AI coding agent) -- or any terminal workflow -- spawn actors, edit Blueprints, inspect materials, run Play-In-Editor sessions, capture screenshots, profile performance, and execute 50+ other operations inside a running UE5 editor or packaged build.

One pip install. One plugin copy. Zero manual editor clicks.

```
Claude Code  -->  soft-ue-cli (Python)  -->  HTTP/JSON-RPC  -->  SoftUEBridge plugin (inside UE)
```

---

## Why soft-ue-cli?

- **AI-native UE automation** -- purpose-built so Claude Code can read, modify, and test Unreal Engine projects without a human touching the editor.
- **50+ commands** covering actors, Blueprints, materials, StateTrees, widgets, assets, PIE sessions, profiling, and more.
- **Works everywhere UE runs** -- editor, cooked builds, Windows, macOS, Linux.
- **Single dependency** -- only requires `httpx`. No heavy SDK, no editor scripting setup.
- **Team-friendly** -- conditional compilation via `SOFT_UE_BRIDGE` environment variable means only developers who need the bridge get it compiled in.

---

## Quick Start

### 1. Install the CLI

```bash
pip install soft-ue-cli
```

### 2. Install the plugin into your UE project

```bash
soft-ue-cli setup /path/to/YourProject
```

This copies the bundled **SoftUEBridge** C++ plugin into your project's `Plugins/` directory.

### 3. Enable the plugin

Add the following entry to the `"Plugins"` array in your `.uproject` file:

```json
{"Name": "SoftUEBridge", "Enabled": true}
```

### 4. Rebuild and launch Unreal Engine

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

### 6. Tell Claude Code about the CLI

Create or append to your project's `CLAUDE.md`:

```markdown
## Unreal Engine control

`soft-ue-cli` controls this UE project via the SoftUEBridge plugin.
Run `soft-ue-cli --help` to see all available commands.
The game or editor must be running with SoftUEBridge enabled before using UE commands.
```

Now Claude Code can autonomously control your Unreal Engine project.

---

## How It Works

```
Claude Code
    |
    |  (runs CLI commands in terminal)
    v
soft-ue-cli  (Python process)
    |
    |  HTTP / JSON-RPC requests
    v
SoftUEBridge plugin  (C++ UGameInstanceSubsystem, inside UE process)
    |
    |  Native UE API calls on the game thread
    v
Unreal Engine 5 editor or runtime
```

The **SoftUEBridge** plugin is a lightweight C++ `UGameInstanceSubsystem` that starts an embedded HTTP server on port 8080 when UE launches. The CLI sends JSON-RPC requests to this server, and the plugin executes the corresponding UE operations on the game thread, returning structured JSON responses.

All commands output JSON to stdout (except `get-logs --raw`). Exit code 0 means success, 1 means error.

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
| `call-function` | Call any `BlueprintCallable` `UFUNCTION` on an actor |
| `set-property` | Set a `UPROPERTY` value on an actor by name |
| `add-component` | Add a component to an existing actor |

### Blueprint Inspection and Editing

| Command | Description |
|---------|-------------|
| `query-blueprint` | Inspect a Blueprint asset -- components, variables, functions, event dispatchers |
| `query-blueprint-graph` | Inspect event graphs, function graphs, and node connections |
| `add-graph-node` | Add a node to a Blueprint or Material graph |
| `remove-graph-node` | Remove a node from a graph |
| `connect-graph-pins` | Connect two pins between graph nodes |
| `disconnect-graph-pin` | Disconnect a specific pin |
| `set-node-position` | Batch-set node positions for graph layout |

### Asset Management

| Command | Description |
|---------|-------------|
| `query-asset` | Search the Content Browser by name, class, or path -- also inspect DataTables |
| `create-asset` | Create new Blueprint, Material, DataTable, or other asset types |
| `delete-asset` | Delete an asset |
| `set-asset-property` | Set a property on a Blueprint CDO or component |
| `get-asset-diff` | Get property-level diff of an asset vs. source control |
| `get-asset-preview` | Get a thumbnail/preview image of an asset |
| `open-asset` | Open an asset in the editor |
| `find-references` | Find assets, variables, or functions referencing a given asset |

### Material Inspection

| Command | Description |
|---------|-------------|
| `query-material` | Inspect Material or Material Instance -- parameters, nodes, connections |

### Class and Type Inspection

| Command | Description |
|---------|-------------|
| `class-hierarchy` | Inspect class inheritance chains -- ancestors, descendants, or both |

### Play-In-Editor (PIE) Control

| Command | Description |
|---------|-------------|
| `pie-session` | Start, stop, pause, resume PIE -- also query actor state during play |
| `pie-input` | Send keyboard, mouse, gamepad, or AI-move input to a PIE session |

### Screenshot and Visual Capture

| Command | Description |
|---------|-------------|
| `capture-screenshot` | Capture the editor viewport, PIE window, or a specific editor panel |

### Logging and Console Variables

| Command | Description |
|---------|-------------|
| `get-logs` | Read the UE output log with optional category and text filters |
| `get-console-var` | Read the value of a console variable (CVar) |
| `set-console-var` | Set a console variable value |

### Python Scripting in UE

| Command | Description |
|---------|-------------|
| `run-python-script` | Execute a Python script inside UE's embedded Python interpreter |
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
| `inspect-widget-blueprint` | Inspect UMG Widget Blueprint hierarchy, bindings, and properties |
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

### Build and Live Coding

| Command | Description |
|---------|-------------|
| `build-and-relaunch` | Trigger a full C++ rebuild and optionally relaunch the editor |
| `trigger-live-coding` | Trigger a Live Coding compile (hot reload) |

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

### Inspect a Blueprint's components and variables

```bash
soft-ue-cli query-blueprint /Game/Blueprints/BP_Player --include components,variables
```

### Start a PIE session and send input

```bash
soft-ue-cli pie-session start --mode SelectedViewport
soft-ue-cli pie-input press --key SpaceBar
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

### Profile with UE Insights

```bash
soft-ue-cli insights-capture start --channels CPU,GPU
# ... run your scenario ...
soft-ue-cli insights-capture stop
soft-ue-cli insights-analyze latest --analysis-type cpu
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
| **Unreal Engine** | 5.3, 5.4, 5.5 |
| **Python** | 3.10+ |
| **Platforms** | Windows, macOS, Linux |
| **Build types** | Editor, Development, Shipping (cooked/packaged) |
| **Dependencies** | `httpx >= 0.27` (sole runtime dependency) |

---

## Development

```bash
git clone https://github.com/softdaddy-o/soft-ue-cli
cd soft-ue-cli
pip install -e .
pytest -v
```

---

## Frequently Asked Questions

### What is soft-ue-cli?

soft-ue-cli is a Python command-line tool that controls Unreal Engine 5 from the terminal. It communicates with a C++ plugin (SoftUEBridge) running inside UE via HTTP/JSON-RPC, enabling automation of actor spawning, Blueprint editing, material inspection, Play-In-Editor sessions, screenshot capture, performance profiling, and 50+ other operations.

### How does Claude Code use soft-ue-cli to control Unreal Engine?

Claude Code runs soft-ue-cli commands in the terminal just like a developer would. By adding a `CLAUDE.md` file to your UE project that describes the available commands, Claude Code can autonomously query your level, spawn actors, edit Blueprints, run PIE sessions, and iterate on your game -- all without manual editor interaction.

### Can I use soft-ue-cli without Claude Code?

Yes. soft-ue-cli is a standard Python CLI. You can use it in shell scripts, CI/CD pipelines, custom automation tools, or any workflow that can invoke command-line programs. Every command outputs structured JSON, making it easy to parse and integrate.

### Does it work with packaged/cooked Unreal Engine builds?

Yes. The SoftUEBridge plugin works in both the UE editor and in cooked/packaged builds (Development and Shipping configurations). This makes it useful for automated testing of packaged games.

### What Unreal Engine versions are supported?

soft-ue-cli supports Unreal Engine 5.3, 5.4, and 5.5. The C++ plugin uses stable engine APIs and is compatible across these versions without modification.

### Is there any runtime performance impact?

The SoftUEBridge plugin adds a lightweight HTTP server that listens on a single port. When no requests are being made, the overhead is negligible. The server processes requests on the game thread to ensure thread safety with UE APIs. For production builds where you do not want the bridge, use conditional compilation via the `SOFT_UE_BRIDGE` environment variable.

### How do I change the default port?

Set the `SOFT_UE_BRIDGE_PORT` environment variable before launching UE, or use the `--server` flag when running CLI commands. The default port is 8080.

### Can multiple UE instances run simultaneously?

Yes. Each UE instance writes its port to a `.soft-ue-bridge/instance.json` file in the project directory. Use `SOFT_UE_BRIDGE_URL` or `--server` to target a specific instance when multiple are running.

### How do I edit Blueprints from the command line?

Use `query-blueprint-graph` to inspect existing graph nodes, `add-graph-node` to create new nodes, `connect-graph-pins` to wire them together, and `remove-graph-node` to delete nodes. This enables fully programmatic Blueprint construction -- useful for AI-driven development and automated testing.

### What is the difference between soft-ue-cli and Unreal Engine Remote Control?

Unreal Engine's built-in Remote Control API focuses on property access and preset-based workflows. soft-ue-cli provides a broader command set specifically designed for AI coding agents -- including Blueprint graph editing, StateTree manipulation, PIE session control, UE Insights profiling, widget inspection, and asset creation -- with a simpler setup process (one pip install, one plugin copy).

---

## License

MIT License. See [LICENSE](https://github.com/softdaddy-o/soft-ue-cli/blob/main/LICENSE) for details.

---

## Links

- **PyPI**: [pypi.org/project/soft-ue-cli](https://pypi.org/project/soft-ue-cli/)
- **GitHub**: [github.com/softdaddy-o/soft-ue-cli](https://github.com/softdaddy-o/soft-ue-cli)
- **Claude Code**: [docs.anthropic.com/en/docs/claude-code](https://docs.anthropic.com/en/docs/claude-code)
