# soft-ue-cli

**soft-ue-cli** lets Claude Code control a running Unreal Engine game or editor — spawn actors, call functions, read logs, and tweak console variables — all from the terminal.

Works in the **UE editor and cooked/packaged builds**. No Python plugin required.

---

## How it works

```
Claude Code
    |
    └── soft-ue-cli  ──HTTP──>  SoftUEBridge plugin  (inside UE process)
                                      └── UGameInstanceSubsystem
                                           (editor + packaged builds)
```

1. The **SoftUEBridge** C++ plugin runs an HTTP server inside UE.
2. **soft-ue-cli** sends commands to it over HTTP/JSON-RPC.
3. Claude Code calls `soft-ue-cli` via its Bash tool.

---

## Quick start

### Step 1 — Install the CLI

```bash
pip install soft-ue-cli
```

### Step 2 — Install the plugin into your UE project

Open your LLM coding client (Claude Code, Cursor, Windsurf, etc.) in your UE project directory and ask it:

> Install the SoftUEBridge plugin using soft-ue-cli setup

The `setup` command is designed for LLM clients. It prints machine-readable instructions that your LLM client will automatically follow — copying the plugin files, editing your `.uproject`, and creating a `CLAUDE.md` with the CLI reference.

If you prefer to do it manually:

```bash
soft-ue-cli setup /path/to/YourProject
```

Then follow the printed steps: copy the plugin, enable it in `.uproject`, rebuild, and launch.

### Step 3 — Rebuild and launch UE

After the plugin is installed, rebuild your project and launch the editor. The bridge server starts automatically when UE loads.

### Step 4 — Verify

```bash
soft-ue-cli check-setup
```

---

## CLI commands

Run `soft-ue-cli --help` or `soft-ue-cli <command> --help` for full details.

| Command | What it does |
|---------|-------------|
| `setup` | Install SoftUEBridge plugin into a UE project |
| `check-setup` | Verify the bridge server is reachable |
| `status` | Quick health check |
| `spawn-actor <class>` | Spawn an actor (native class or Blueprint path) |
| `query-level` | List actors with transforms, tags, components |
| `call-function <actor> <fn>` | Call a BlueprintCallable function |
| `set-property <actor> <prop> <val>` | Set an actor/component property |
| `get-logs` | Read recent output log entries |
| `get-console-var <name>` | Read a console variable |
| `set-console-var <name> <val>` | Set a console variable |

---

## Conditional enable for teams

To avoid including the bridge in every build, use `Target.cs`:

```csharp
if (System.Environment.GetEnvironmentVariable("SOFT_UE_BRIDGE") == "1")
{
    EnablePlugins.Add("SoftUEBridge");
}
```

Set `SOFT_UE_BRIDGE=1` before launching UE to enable it.

---

## Environment variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `SOFT_UE_BRIDGE_URL` | *(auto)* | Full bridge URL override |
| `SOFT_UE_BRIDGE_PORT` | `8080` | Port override |
| `SOFT_UE_BRIDGE` | — | Set to `1` to enable via `Target.cs` |

The CLI discovers the bridge URL in this order:
`SOFT_UE_BRIDGE_URL` > `SOFT_UE_BRIDGE_PORT` > `.soft-ue-bridge/instance.json` (walked up from cwd) > `http://127.0.0.1:8080`

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

MIT — see [LICENSE](LICENSE)
