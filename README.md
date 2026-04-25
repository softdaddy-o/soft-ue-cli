# soft-ue-cli

[![PyPI version](https://img.shields.io/pypi/v/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![Python 3.10+](https://img.shields.io/pypi/pyversions/soft-ue-cli.svg)](https://pypi.org/project/soft-ue-cli/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

**Control Unreal Engine 5 from the command line.** soft-ue-cli is a Python CLI that lets [Claude Code](https://docs.anthropic.com/en/docs/claude-code) (Anthropic's AI coding agent) -- or any terminal workflow -- spawn actors, edit Blueprints, inspect materials, run Play-In-Editor sessions, capture screenshots, profile performance, and execute 50+ other operations inside a running UE5 editor or packaged build.

---

## Installation

```bash
pip install soft-ue-cli
```

## Setup

```bash
soft-ue-cli setup /path/to/MyProject
```

This copies the SoftUEBridge C++ plugin into your UE project and prints instructions for enabling it. After rebuilding and launching UE, verify the connection:

```bash
soft-ue-cli check-setup
```

---

## Architecture

```
Claude Code / Terminal
    |
    |  (runs CLI commands)
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

## Command Reference

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
| `query-blueprint` | Inspect a Blueprint asset -- components, variables, functions, interfaces, event dispatchers |
| `query-blueprint-graph` | Inspect event graphs, function graphs, and node connections |
| `add-graph-node` | Add a node to a Blueprint or Material graph (supports `AnimLayerFunction` for ALIs) |
| `modify-interface` | Add or remove an implemented interface on a Blueprint or AnimBlueprint |
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
| `trigger-input` | Send input events to a running game (PIE or packaged build) |

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

### Build and Live Coding

| Command | Description |
|---------|-------------|
| `build-and-relaunch` | Trigger a full C++ rebuild and optionally relaunch the editor |
| `trigger-live-coding` | Trigger a Live Coding compile (hot reload) |

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `SOFT_UE_BRIDGE_URL` | *(none)* | Full bridge URL override (e.g. `http://192.168.1.10:8080`) |
| `SOFT_UE_BRIDGE_PORT` | `8080` | Port override when using localhost |

---

## Development

```bash
git clone https://github.com/softdaddy-o/soft-ue-cli
cd soft-ue-cli
pip install -e .
pytest -v
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
