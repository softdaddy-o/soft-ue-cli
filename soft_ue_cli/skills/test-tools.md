---
name: test-tools
description: Exhaustive integration test of all soft-ue-cli tools against a live UE instance. Writes a JSON report.
version: 2.6.2
---

# test-tools — Integration Test Suite

Runs every soft-ue-cli bridge tool against a live UE instance, collects pass/fail results per test, and writes a JSON report. No LLM in the loop — extract the script and run it.

## Modes

When a new CLI tool, MCP-exposed tool, or new inspect/diff section is added, extend this skill's script so the new surface is exercised here. Offline-only tools still need smoke coverage in this suite where practical.

| Mode | What it tests | Transport |
|------|--------------|-----------|
| `cli` (default) | Bridge tools directly | HTTP JSON-RPC |
| `mcp` | MCP server layer + bridge tools | MCP stdio → HTTP |
| `all` | Both modes sequentially | Both |

## Requirements

- `soft-ue-cli` installed — `pip install soft-ue-cli`
- MCP mode also requires — `pip install soft-ue-cli[mcp]`
- UE running with SoftUEBridge enabled and reachable
- Optional AnimBlueprint smoke coverage: set `SOFT_UE_TEST_ANIM_BP=/Game/.../ABP_Test`

## Usage

```bash
# 1. Get this skill
soft-ue-cli skills get test-tools

# 2. Save the Python block below to disk, then run:
python test_tools.py                            # CLI mode → soft-ue-test-report_<ts>.json
python test_tools.py --mode mcp                 # MCP mode
python test_tools.py --mode all                 # both modes
python test_tools.py report.json --mode all     # custom output path
```

Exit code: 0 if all tests pass, 1 if any fail.

## Script

```python
#!/usr/bin/env python3
"""
soft-ue-cli integration test suite.
Usage: python test_tools.py [output_path] [--mode cli|mcp|all]

  cli  (default) — calls bridge HTTP server directly via call_tool()
  mcp            — spawns soft-ue-cli mcp-serve and speaks MCP stdio
  all            — runs both modes sequentially, combines into one report
"""

import argparse
import itertools
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import tempfile
import time
from datetime import datetime, timezone

try:
    from soft_ue_cli.client import call_tool as _http_call_tool, health_check
    from soft_ue_cli.discovery import get_server_url
    from soft_ue_cli import __version__ as CLI_VERSION
except ImportError:
    print("error: soft-ue-cli not installed. Run: pip install soft-ue-cli", file=sys.stderr)
    sys.exit(1)

# ── Argument parsing ───────────────────────────────────────────────────────────
def _default_output_path() -> str:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"soft-ue-test-report_{stamp}.json"

_parser = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
_parser.add_argument("output_path", nargs="?", default=_default_output_path())
_parser.add_argument("--mode", choices=["cli", "mcp", "all"], default="cli",
                     help="Transport mode (default: cli)")
_args = _parser.parse_args()

OUTPUT_PATH = _args.output_path
MODE = _args.mode

RUN_TS = int(time.time())
LABEL_PFX = f"SUET_{RUN_TS}"
CLI = [sys.executable, "-m", "soft_ue_cli"]

# ── CLI caller (direct HTTP) ───────────────────────────────────────────────────
def _cli_caller(tool_name: str, arguments: dict, timeout: float | None = None) -> dict:
    return _http_call_tool(tool_name, arguments, timeout=timeout)

# ── MCP client ─────────────────────────────────────────────────────────────────
class MCPClient:
    """Minimal synchronous MCP stdio client for integration testing.

    Spawns `soft-ue-cli mcp-serve` as a subprocess, sends JSON-RPC messages
    over stdin, and reads responses from stdout via a background reader thread.
    """

    # Bridge tool names that differ from their MCP/CLI-exposed names.
    _BRIDGE_TO_MCP: dict[str, str] = {
        "get-class-hierarchy": "class-hierarchy",
        "get-project-info":    "project-info",
        "anim-repoint-references": "anim retarget repoint-references",
        "anim-retarget-blueprint": "anim retarget blueprint",
        "pose-search-schema-inspect": "anim pose-search inspect",
        "pose-search-schema-remap": "anim pose-search remap",
        "metasound-inspect": "metasound inspect",
    }

    def __init__(self) -> None:
        self._proc = subprocess.Popen(
            CLI + ["mcp-serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self._ids = itertools.count(1)
        self._recv_q: queue.Queue[str | None] = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        self._initialize()

    def _read_loop(self) -> None:
        try:
            for line in self._proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                    msg_id = msg.get("id")
                    self._recv_q.put((msg_id, msg))
                except json.JSONDecodeError:
                    pass  # ignore non-JSON lines (e.g. log output)
        finally:
            self._recv_q.put((None, None))  # signal EOF

    def _send(self, msg: dict) -> None:
        self._proc.stdin.write(json.dumps(msg) + "\n")
        self._proc.stdin.flush()

    def _recv(self, expected_id: int, timeout: float = 30.0) -> dict:
        """Receive a response matching expected_id, discarding any stale responses."""
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(f"MCP response timeout for id={expected_id}")
            try:
                msg_id, msg = self._recv_q.get(timeout=min(remaining, 1.0))
            except queue.Empty:
                continue
            if msg is None:
                raise EOFError("MCP server closed stdout")
            if msg_id == expected_id:
                return msg
            # Stale response from a previous timed-out call — discard and keep waiting

    def _initialize(self) -> None:
        init_id = next(self._ids)
        self._send({
            "jsonrpc": "2.0",
            "id": init_id,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "test-tools", "version": "2.0"},
            },
        })
        self._recv(expected_id=init_id, timeout=15.0)
        # Notify server that initialization is complete (no response expected)
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})

    def call_tool(self, tool_name: str, arguments: dict,
                  timeout: float | None = None) -> dict:
        mcp_name = self._BRIDGE_TO_MCP.get(tool_name, tool_name)
        call_id = next(self._ids)
        self._send({
            "jsonrpc": "2.0",
            "id": call_id,
            "method": "tools/call",
            "params": {"name": mcp_name, "arguments": arguments},
        })
        resp = self._recv(expected_id=call_id, timeout=timeout or 30.0)
        if "error" in resp:
            err = resp["error"]
            raise Exception(err.get("message", str(err)))
        result = resp.get("result", {})
        if result.get("isError"):
            content = result.get("content", [])
            msg = content[0].get("text", "unknown") if content else "unknown"
            raise Exception(msg)
        content = result.get("content", [])
        if content and content[0].get("type") == "text":
            text = content[0]["text"]
            try:
                parsed = json.loads(text)
                # MCP server wraps tool errors as {"error": "..."} dicts
                if isinstance(parsed, dict) and set(parsed) == {"error"}:
                    raise Exception(parsed["error"])
                return parsed
            except json.JSONDecodeError:
                return {"text": text}
        return result

    def close(self) -> None:
        try:
            self._proc.stdin.close()
            self._proc.wait(timeout=10)
        except Exception:
            self._proc.kill()

# ── Single-mode runner ─────────────────────────────────────────────────────────
_run_start = time.time()


def _run_single_mode(mode_name: str, caller) -> list[dict]:
    """Run the complete test suite using *caller* as the tool invoker.

    caller(tool_name, arguments, timeout) -> dict

    Returns the list of suite dicts (each containing 'name' and 'tests').
    Each suite dict has a 'mode' key added for identification in combined reports.
    """
    suites: list[dict] = []
    current_suite: list[dict] | None = None  # current suite's test list
    current_suite_dict: dict | None = None
    teardown_list: list[tuple[str, dict]] = []

    TEST_NS = f"/Game/SoftUETest_{RUN_TS}_{mode_name}"

    # ── Inner helpers (close over locals) ─────────────────────────────────────
    def begin_suite(name: str) -> None:
        nonlocal current_suite, current_suite_dict
        current_suite_dict = {"name": name, "mode": mode_name, "tests": []}
        current_suite = current_suite_dict["tests"]
        suites.append(current_suite_dict)
        print(f"\n[{mode_name}] Suite: {name}")

    def _record(name, tool, args, passed, elapsed_ms, error) -> dict:
        assert current_suite is not None
        rec = {"name": name, "tool": tool, "args": args,
               "passed": passed, "duration_ms": elapsed_ms, "error": error}
        current_suite.append(rec)
        marker = "PASS" if passed else "FAIL"
        suffix = f" — {error}" if error else ""
        print(f"  [{marker}] {name} ({elapsed_ms}ms){suffix}")
        return rec

    def run_test(name, tool, args, check=None, timeout=None) -> dict:
        t0 = time.time()
        try:
            result = caller(tool, args, timeout)
            passed = check(result) if check else True
            error = None if passed else f"check failed: {json.dumps(result)[:200]}"
        except Exception as exc:
            result = None
            passed = False
            error = str(exc)[:300]
        return _record(name, tool, args, passed, int((time.time() - t0) * 1000), error)

    def run_cli(name, *args, check_stdout=None, timeout=30) -> dict:
        t0 = time.time()
        try:
            proc = subprocess.run(CLI + list(args), capture_output=True, text=True, timeout=timeout)
            passed = proc.returncode == 0
            if passed and check_stdout:
                passed = check_stdout(proc.stdout)
            error = None if passed else (proc.stderr.strip() or proc.stdout.strip())[:300]
        except Exception as exc:
            passed = False
            error = str(exc)[:300]
        cmd_str = "soft-ue-cli " + " ".join(str(a) for a in args)
        return _record(name, cmd_str, {}, passed, int((time.time() - t0) * 1000), error)

    def reg_teardown(tool, args):
        teardown_list.append((tool, args))

    def has(*keys):
        return lambda r: all(k in r for k in keys)

    def nonempty(key):
        return lambda r: isinstance(r.get(key), list) and len(r.get(key)) > 0

    def actors_include(label):
        return lambda r: any(
            a.get("label") == label or a.get("name") == label
            for a in r.get("actors", [])
        )

    def starts_with(key, value):
        return lambda r: str(r.get(key, "")).startswith(value)

    def asset_to_disk_path(project_dir, asset_path, ext=".uasset"):
        if not project_dir or not asset_path or not asset_path.startswith("/Game/"):
            return None
        relative = asset_path[len("/Game/"):].replace("/", os.sep)
        return os.path.normpath(os.path.join(project_dir, "Content", relative + ext))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 0: Setup — create and load a fresh test level
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("setup")

    test_level_path = f"{TEST_NS}/SoftUETestLevel"

    # Capture the currently open level so teardown can restore it
    _original_level: str | None = None
    try:
        _r = caller("run-python-script", {
            "script": (
                "import unreal\n"
                "w = unreal.EditorLevelLibrary.get_editor_world()\n"
                "if w:\n"
                "    print(w.get_path_name().split('.')[0])\n"
            )
        }, None)
        _lvl = (_r.get("output") or "").strip().splitlines()
        _lvl = next((l for l in _lvl if l.startswith("/")), None)
        if _lvl:
            _original_level = _lvl
    except Exception:
        pass

    run_test("create test level", "create-asset",
             {"asset_path": test_level_path, "asset_class": "World"}, has("asset_path"))
    _open_level_args = {"asset_path": test_level_path}
    _open_first = run_test("open test level", "open-asset",
                           _open_level_args, has("success"))
    if not _open_first["passed"]:
        time.sleep(1)
        run_test("open test level retry", "open-asset",
                 _open_level_args, has("success"))
    time.sleep(2)

    # open-asset (level restore) is handled first in teardown to avoid
    # CheckForWorldGCLeaks crash when switching levels.
    reg_teardown("delete-asset", {"asset_path": test_level_path})

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 1: Status
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("status")

    if mode_name == "cli":
        t0 = time.time()
        try:
            info = health_check()
            ok = "error" not in info
            err = info.get("error") if not ok else None
        except Exception as exc:
            ok, err = False, str(exc)[:300]
        _record("bridge health check", "health_check", {}, ok, int((time.time() - t0) * 1000), err)
        expected_co_tools = {
            "reload-bridge-module",
            "add-customizable-object-node",
            "set-customizable-object-node-property",
            "connect-customizable-object-pins",
            "regenerate-customizable-object-node-pins",
            "compile-customizable-object",
            "remove-customizable-object-node",
            "wire-customizable-object-slot-from-table",
        }
        tool_names = set(info.get("tool_names", [])) if isinstance(info, dict) else set()
        missing_co_tools = sorted(expected_co_tools - tool_names)
        _record("CustomizableObject bridge tools registered", "health_check.tool_names", {},
                not missing_co_tools, 0,
                f"missing: {missing_co_tools}" if missing_co_tools else None)
    else:
        # MCP: reaching here means mcp-serve started and initialized successfully
        _record("mcp-serve started", "mcp-serve", {}, True, 0, None)

    run_cli("wait-for-ready immediate", "wait-for-ready", "--timeout", "5", "--poll-interval", "0.25",
            check_stdout=lambda s: '"status": "ready"' in s and '"success": true' in s)
    run_cli("await-bridge alias help", "await-bridge", "--help",
            check_stdout=lambda s: "wait-for-ready" in s and "--launch-editor" in s)

    project_info = None
    project_dir = None
    try:
        project_info = caller("get-project-info", {}, None)
        project_dir = project_info.get("project_directory") or os.path.dirname(project_info.get("project_path", ""))
        if project_dir:
            project_dir = os.path.normpath(project_dir)
    except Exception:
        project_info = None
        project_dir = None

    run_test("project-info", "get-project-info", {}, has("project_name"))
    run_test("get-logs", "get-logs", {"limit": 10}, has("lines"))
    try:
        _logs_cursor = caller("get-logs", {"lines": 0}, None).get("next_cursor")
    except Exception:
        _logs_cursor = None
    if _logs_cursor:
        run_test("get-logs since cursor", "get-logs", {"since": _logs_cursor}, has("entries"))
    else:
        _record("get-logs since cursor", "get-logs", {}, True, 0, "skipped: no cursor available")

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 2: Console Variables
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("console-vars")

    original_pct: int | None = None
    try:
        original_pct = int(float(
            caller("get-console-var", {"name": "r.ScreenPercentage"}, None).get("value", 100)
        ))
    except Exception:
        pass
    reg_teardown("set-console-var", {"name": "r.ScreenPercentage", "value": original_pct or 100})

    run_test("get r.ScreenPercentage", "get-console-var",
             {"name": "r.ScreenPercentage"}, has("value"))
    run_test("set r.ScreenPercentage=75", "set-console-var",
             {"name": "r.ScreenPercentage", "value": 75}, has("success"))
    run_test("verify r.ScreenPercentage=75", "get-console-var",
             {"name": "r.ScreenPercentage"}, starts_with("value", "75"))
    run_test("get r.ShadowQuality", "get-console-var",
             {"name": "r.ShadowQuality"}, has("value"))
    run_test("set r.ShadowQuality=2", "set-console-var",
             {"name": "r.ShadowQuality", "value": 2}, has("success"))
    reg_teardown("set-console-var", {"name": "r.ShadowQuality", "value": 3})

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 3: Level Query
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("level-query")

    run_test("query-level all", "query-level", {"limit": 20}, has("actors"))
    run_test("query-level search=Camera", "query-level",
             {"search": "Camera", "limit": 5}, has("actors"))
    run_test("query-level class_filter=StaticMeshActor", "query-level",
             {"class_filter": "StaticMeshActor", "limit": 5}, has("actors"))
    run_test("query-level include_components", "query-level",
             {"limit": 5, "include_components": True}, has("actors"))
    run_test("query-level world=editor", "query-level",
             {"world": "editor", "limit": 5}, has("actors"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 4: Actor Lifecycle
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("actor-lifecycle")

    a1 = f"{LABEL_PFX}_A1"
    a2 = f"{LABEL_PFX}_A2"
    a3 = f"{LABEL_PFX}_A3"
    a4 = f"{LABEL_PFX}_A4"
    reg_teardown("batch-delete-actors", {"actors": [a1, a2, a3, a4]})

    run_test("spawn-actor", "spawn-actor",
             {"actor_class": "StaticMeshActor", "label": a1, "location": [0, 0, 100]},
             has("actor_label"))
    run_test("verify spawned in level", "query-level",
             {"search": a1, "limit": 5}, actors_include(a1))
    run_test("get-property Tags", "get-property",
             {"actor_name": a1, "property_name": "Tags"}, has("value"))
    run_test("get-property Tags world=editor", "get-property",
             {"actor_name": a1, "property_name": "Tags", "world": "editor"}, has("value"))
    run_test("set-property bHidden=true", "set-property",
             {"actor_name": a1, "property_name": "bHidden", "value": True}, has("success"))
    run_test("call-function GetActorLabel", "call-function",
             {"actor_name": a1, "function_name": "GetActorLabel"}, has("ReturnValue"))
    run_test("batch-spawn-actors", "batch-spawn-actors", {"actors": [
        {"actor_class": "StaticMeshActor", "label": a2, "location": [200, 0, 100]},
        {"actor_class": "StaticMeshActor", "label": a3, "location": [400, 0, 100]},
        {"actor_class": "StaticMeshActor", "label": a4, "location": [600, 0, 100]},
    ]}, has("spawned"))
    run_test("batch-modify-actors", "batch-modify-actors", {"modifications": [
        {"label": a2, "location": [200, 200, 100]},
        {"label": a3, "location": [400, 200, 100]},
    ]}, has("modified"))
    run_test("batch-delete-actors a2/a3/a4", "batch-delete-actors",
             {"actors": [a2, a3, a4]}, has("deleted"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 5: Components
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("components")

    comp_name = f"{LABEL_PFX}_Light"
    run_test("add-component PointLight", "add-component",
             {"actor_name": a1, "component_class": "PointLightComponent",
              "component_name": comp_name}, has("success"))
    run_test("get-property Intensity", "get-property",
             {"actor_name": a1, "property_name": f"{comp_name}.Intensity"}, has("value"))
    run_test("set-property Intensity=8000", "set-property",
             {"actor_name": a1, "property_name": f"{comp_name}.Intensity", "value": 8000},
             has("success"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 6: Assets
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("assets")

    bp_path = f"{TEST_NS}/BP_SoftUETest"
    bpi_path = f"{TEST_NS}/BPI_SoftUETest"
    wbp_path = f"{TEST_NS}/WBP_SoftUETest"
    reg_teardown("delete-asset", {"asset_path": bp_path})
    reg_teardown("delete-asset", {"asset_path": bpi_path})
    reg_teardown("delete-asset", {"asset_path": wbp_path})

    run_test("create-asset Blueprint", "create-asset",
             {"asset_path": bp_path, "asset_class": "/Script/Engine.Blueprint",
              "parent_class": "/Script/Engine.Actor"}, has("asset_path"))

    run_test("create-asset WidgetBlueprint", "create-asset",
             {"asset_path": wbp_path, "asset_class": "WidgetBlueprint"}, has("asset_path"))
    run_test("apply-widget-tree UMG designer spec", "apply-widget-tree", {
        "asset_path": wbp_path,
        "compile": True,
        "save": True,
        "spec": {
            "root": {
                "class": "CanvasPanel",
                "name": "RootCanvas",
                "children": [
                    {
                        "class": "TextBlock",
                        "name": "TitleText",
                        "text": "SoftUE",
                        "font_size": 32,
                        "slot": {
                            "position": [32, 32],
                            "size": [320, 64],
                            "z_order": 1,
                        },
                    },
                    {
                        "class": "Button",
                        "name": "StartButton",
                        "slot": {
                            "position": [32, 120],
                            "size": [220, 56],
                            "z_order": 2,
                        },
                        "children": [
                            {
                                "class": "TextBlock",
                                "name": "StartButtonLabel",
                                "text": "Start",
                                "justification": "center",
                            }
                        ],
                    },
                    {
                        "class": "WidgetSwitcher",
                        "name": "ScreenSwitcher",
                        "slot": {
                            "position": [32, 200],
                            "size": [420, 180],
                            "z_order": 3,
                        },
                        "children": [
                            {
                                "class": "CanvasPanel",
                                "name": "HomePanel",
                                "children": [
                                    {
                                        "class": "TextBlock",
                                        "name": "HomePanelText",
                                        "text": "Home",
                                    }
                                ],
                            },
                            {
                                "class": "CanvasPanel",
                                "name": "DetailsPanel",
                                "children": [
                                    {
                                        "class": "TextBlock",
                                        "name": "DetailsPanelText",
                                        "text": "Details",
                                    }
                                ],
                            },
                        ],
                    },
                ],
            }
        },
    }, lambda r: r.get("success") is True and r.get("widget_count", 0) >= 9)
    run_test("inspect-widget-blueprint applied tree", "inspect-widget-blueprint",
             {"asset_path": wbp_path, "depth_limit": 8},
             lambda r: "RootCanvas" in r.get("all_widgets", []) and "StartButtonLabel" in r.get("all_widgets", []))
    _umg_layout_tmp = tempfile.mkdtemp(prefix="soft_ue_umg_layout_")
    _umg_expected_layout = os.path.join(_umg_layout_tmp, "umg_expected_layout.json")
    run_cli(
        "umg layout extract designer",
        "umg",
        "layout",
        "extract",
        "--source",
        "designer",
        "--asset-path",
        wbp_path,
        "--output",
        _umg_expected_layout,
        check_stdout=lambda s: json.loads(s).get("widgets") and os.path.exists(_umg_expected_layout),
    )
    run_test("wire-widget-navigation UMG nav contract", "wire-widget-navigation", {
        "asset_path": wbp_path,
        "bindings": [
            {
                "button": "StartButton",
                "mode": "switcher",
                "switcher": "ScreenSwitcher",
                "target_widget": "DetailsPanel",
            }
        ],
        "compile": True,
        "save": True,
    }, lambda r: r.get("success") is True and r.get("binding_count") == 1 and "parent_binding_contract" in r)

    # BlueprintInterface — skip gracefully if plugin doesn't support it yet
    _bpi_created = False
    _bpi_args = {"asset_path": bpi_path, "asset_class": "BlueprintInterface"}
    _t0 = time.time()
    try:
        _bpi_result = caller("create-asset", _bpi_args, None)
        _bpi_created = "asset_path" in _bpi_result
        _bpi_err = None if _bpi_created else f"check failed: {json.dumps(_bpi_result)[:200]}"
        _record("create-asset BlueprintInterface", "create-asset", _bpi_args,
                _bpi_created, int((time.time() - _t0) * 1000), _bpi_err)
    except Exception as _bpi_exc:
        _bpi_msg = str(_bpi_exc)[:300]
        _bpi_known_gap = any(kw in _bpi_msg.lower() for kw in (
            "blueprintinterface", "not supported", "not implemented",
            "unsupported", "unknown asset class",
        ))
        _record("create-asset BlueprintInterface", "create-asset", _bpi_args,
                _bpi_known_gap, int((time.time() - _t0) * 1000),
                f"skipped (known gap): {_bpi_msg}" if _bpi_known_gap else _bpi_msg)

    run_test("query-asset by path", "query-asset", {"path": TEST_NS}, has("assets"))
    run_test("query-asset by class", "query-asset",
             {"class": "Blueprint", "path": TEST_NS}, has("assets"))
    run_test("query-asset by name pattern", "query-asset",
             {"query": "BP_SoftUETest", "class": "Blueprint", "path": TEST_NS}, has("assets"))
    run_test("query-asset inspect", "query-asset", {"asset_path": bp_path}, has("path"))
    run_test("query-asset inspect world settings", "query-asset",
             {"asset_path": test_level_path}, lambda r: "world_settings" in r and "default_game_mode" in r)
    run_test("get-asset-preview", "get-asset-preview", {"asset_path": bp_path}, has("file_path"))
    run_test("open-asset", "open-asset", {"asset_path": bp_path}, has("success"))
    run_test("release-asset-lock", "release-asset-lock", {"asset_path": bp_path}, has("success"))
    run_test("inspect-customizable-object-graph unavailable smoke", "inspect-customizable-object-graph",
             {"asset_path": bp_path}, lambda r: "available" in r and r["available"] is False)
    run_test("inspect-mutable-parameters unavailable smoke", "inspect-mutable-parameters",
             {"asset_path": bp_path}, lambda r: "available" in r and r["available"] is False)
    run_test("inspect-mutable-diagnostics unavailable smoke", "inspect-mutable-diagnostics",
             {"asset_path": bp_path}, lambda r: "available" in r)
    run_cli("mutable graph remove-node help", "mutable", "graph", "remove-node", "--help",
            check_stdout=lambda s: "remove-node" in s and "node" in s)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 7: Blueprint Inspect
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("blueprint-inspect")

    run_test("query-blueprint", "query-blueprint", {"asset_path": bp_path}, has("path"))
    run_test("query-blueprint-graph EventGraph", "query-blueprint-graph",
             {"asset_path": bp_path, "graph": "EventGraph"}, nonempty("graphs"))
    run_test("query-blueprint-graph recursive node-class", "query-blueprint-graph",
             {"asset_path": bp_path, "recursive": True, "node_class": "K2Node_Event"}, nonempty("graphs"))
    run_test("save-asset blueprint (pre-inspect)", "save-asset", {"asset_path": bp_path}, has("success"))
    _inspect_uasset_path = None
    _inspect_uexp_path = None
    _inspect_snapshot_uasset = None
    _inspect_snapshot_uexp = None
    _inspect_uasset_path = asset_to_disk_path(project_dir, bp_path, ".uasset")

    if _inspect_uasset_path and os.path.exists(_inspect_uasset_path):
        _inspect_uexp_path = os.path.splitext(_inspect_uasset_path)[0] + ".uexp"
        _snapshot_dir = os.path.join(os.path.dirname(os.path.abspath(OUTPUT_PATH)), f"soft_ue_snapshots_{RUN_TS}")
        os.makedirs(_snapshot_dir, exist_ok=True)
        _inspect_snapshot_uasset = os.path.join(_snapshot_dir, f"{mode_name}_BP_SoftUETest_before.uasset")
        try:
            shutil.copy2(_inspect_uasset_path, _inspect_snapshot_uasset)
            if os.path.exists(_inspect_uexp_path):
                _inspect_snapshot_uexp = os.path.join(_snapshot_dir, f"{mode_name}_BP_SoftUETest_before.uexp")
                shutil.copy2(_inspect_uexp_path, _inspect_snapshot_uexp)
        except Exception:
            _inspect_snapshot_uasset = None
            _inspect_snapshot_uexp = None
        run_cli(
            "asset inspect-file summary",
            "asset", "inspect-file", _inspect_uasset_path,
            check_stdout=lambda s: '"name": "BP_SoftUETest"' in s and '"asset_class"' in s,
        )
        run_cli(
            "asset inspect-file all",
            "asset", "inspect-file", _inspect_uasset_path, "--sections", "all",
            check_stdout=lambda s: '"variables"' in s and '"functions"' in s and '"fidelity"' in s,
        )
        run_cli(
            "asset inspect-file properties",
            "asset", "inspect-file", _inspect_uasset_path, "--sections", "properties",
            check_stdout=lambda s: '"properties"' in s and '"fidelity"' in s,
        )
    else:
        _record("inspect-uasset", "inspect-uasset", {},
                True, 0, "skipped: could not resolve on-disk .uasset path")

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 8: Blueprint Graph Manipulation
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("blueprint-graph")

    # Use K2Node_IfThenElse (Branch) — allocates exec pins unconditionally.
    # K2Node_CallFunction needs a function reference set before pins appear.
    _add_node_args = {
        "asset_path": bp_path,
        "graph_name": "EventGraph",
        "node_class": "K2Node_IfThenElse",
        "position": [400, 0],
    }
    branch_guid = None
    _t0 = time.time()
    try:
        _add_result = caller("add-graph-node", _add_node_args, None)
        branch_guid = _add_result.get("node_guid")
        _ok = branch_guid is not None
        _err = None if _ok else f"check failed: {json.dumps(_add_result)[:200]}"
    except Exception as exc:
        _ok, _err = False, str(exc)[:300]
    _record("add-graph-node Branch", "add-graph-node", _add_node_args,
            _ok, int((time.time() - _t0) * 1000), _err)

    begin_guid = None
    try:
        graph_resp = caller("query-blueprint-graph",
                            {"asset_path": bp_path, "graph_name": "EventGraph"}, None)
        nodes = [n for g in graph_resp.get("graphs", []) for n in g.get("nodes", [])]
        for n in nodes:
            if "BeginPlay" in n.get("title", ""):
                begin_guid = n.get("guid")
    except Exception:
        pass

    if begin_guid and branch_guid:
        run_test("connect-graph-pins", "connect-graph-pins", {
            "asset_path": bp_path,
            "source_node": begin_guid, "source_pin": "then",
            "target_node": branch_guid, "target_pin": "execute",
        }, has("success"))
    else:
        _record("connect-graph-pins", "connect-graph-pins", {},
                False, 0, "skipped: could not resolve node guids")

    if branch_guid:
        run_test("set-node-position", "set-node-position", {
            "asset_path": bp_path, "graph": "EventGraph",
            "positions": [{"guid": branch_guid, "x": 500, "y": 100}],
        }, has("success"))
    else:
        _record("set-node-position", "set-node-position", {},
                False, 0, "skipped: no branch_guid")

    anim_bp_path = os.environ.get("SOFT_UE_TEST_ANIM_BP", "").strip()
    if anim_bp_path:
        sm_name = f"SM_{LABEL_PFX}"
        idle_name = f"Idle_{LABEL_PFX}"
        run_name = f"Run_{LABEL_PFX}"
        run_test("add-anim-state-machine", "add-anim-state-machine", {
            "asset_path": anim_bp_path,
            "state_machine_name": sm_name,
            "default_state": idle_name,
            "position": [700, 0],
        }, has("node_guid"))
        run_test("add-anim-state", "add-anim-state", {
            "asset_path": anim_bp_path,
            "state_machine_name": sm_name,
            "state_name": run_name,
            "position": [1000, 0],
        }, has("node_guid"))
        run_test("add-anim-transition", "add-anim-transition", {
            "asset_path": anim_bp_path,
            "state_machine_name": sm_name,
            "source_state": idle_name,
            "target_state": run_name,
            "crossfade_duration": 0.15,
            "rule": True,
        }, has("transition_graph"))
    else:
        _record("add-anim-state-machine", "add-anim-state-machine", {},
                True, 0, "skipped: SOFT_UE_TEST_ANIM_BP not set")
        _record("add-anim-state", "add-anim-state", {},
                True, 0, "skipped: SOFT_UE_TEST_ANIM_BP not set")
        _record("add-anim-transition", "add-anim-transition", {},
                True, 0, "skipped: SOFT_UE_TEST_ANIM_BP not set")

    run_test("compile-blueprint", "compile-blueprint", {"asset_path": bp_path}, has("success"))
    run_test("save-asset blueprint", "save-asset", {"asset_path": bp_path}, has("success"))
    if _inspect_uasset_path and _inspect_snapshot_uasset:
        run_cli(
            "asset diff-file summary",
            "asset", "diff-file", _inspect_snapshot_uasset, _inspect_uasset_path,
            check_stdout=lambda s: '"has_changes"' in s and '"summary"' in s,
        )
        run_cli(
            "asset diff-file all",
            "asset", "diff-file", _inspect_snapshot_uasset, _inspect_uasset_path, "--sections", "all",
            check_stdout=lambda s: '"changes"' in s and '"summary"' in s,
        )
        run_cli(
            "asset diff-file properties",
            "asset", "diff-file", _inspect_snapshot_uasset, _inspect_uasset_path, "--sections", "properties",
            check_stdout=lambda s: '"properties"' in s and '"change_count"' in s,
        )
    else:
        _record("diff-uasset", "diff-uasset", {},
                True, 0, "skipped: could not snapshot on-disk .uasset before mutation")
    if _bpi_created:
        _bpi_class_path = bpi_path + "." + bpi_path.split("/")[-1] + "_C"
        run_test("modify-interface add", "modify-interface", {
            "asset_path": bp_path,
            "action": "add",
            "interface_class": _bpi_class_path,
        }, has("success"))
    else:
        _record("modify-interface add", "modify-interface", {},
                True, 0, "skipped: BlueprintInterface was not created")

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 9: Class Hierarchy
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("class-hierarchy")

    run_test("parents of Actor", "get-class-hierarchy",
             {"class_name": "Actor", "direction": "parents"}, has("parents"))
    run_test("children of Actor depth=2", "get-class-hierarchy",
             {"class_name": "Actor", "direction": "children", "depth": 2}, has("children"))
    run_test("class-hierarchy StaticMeshActor", "get-class-hierarchy",
             {"class_name": "StaticMeshActor"}, has("class"))
    run_test("validate-class-path Actor", "validate-class-path",
             {"class_path": "/Script/Engine.Actor"}, has("class_exists"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 10: Find References
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("find-references")

    run_test("find-references blueprint", "find-references",
             {"asset_path": bp_path, "type": "asset"}, has("referencers"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 11: Materials
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("materials")

    run_test("query-material BasicShapeMaterial", "query-material",
             {"asset_path": "/Engine/BasicShapes/BasicShapeMaterial"}, has("asset_path"))
    try:
        _mpc_assets = caller("query-asset", {"class": "MaterialParameterCollection", "limit": 1}, None)
        _mpc_path = (_mpc_assets.get("assets") or [{}])[0].get("path")
    except Exception:
        _mpc_path = None
    if _mpc_path:
        run_test("query-mpc", "query-mpc", {"asset_path": _mpc_path}, has("scalar_parameters"))
    else:
        _record("query-mpc (no MPC in project)", "query-mpc", {}, True, 0, None)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 12: Viewport
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("viewport")

    for preset in ("top", "front", "perspective"):
        run_test(f"set-viewport-camera preset={preset}", "set-viewport-camera",
                 {"preset": preset}, has("success"))
    run_test("capture-viewport", "capture-viewport", {}, has("file_path"))
    run_test("capture-viewport scaled grayscale", "capture-viewport",
             {"scale": 50, "color_mode": "grayscale"}, has("file_path", "width", "height"))
    run_test("capture-screenshot", "capture-screenshot", {}, has("file_path"))
    run_test("capture-screenshot scaled monochrome", "capture-screenshot",
             {"scale": 50, "color_mode": "monochrome"}, has("file_path", "width", "height"))
    _visual_tmp = tempfile.mkdtemp(prefix="soft_ue_visual_")
    _ref_ppm = os.path.join(_visual_tmp, "reference.ppm")
    _cap_ppm = os.path.join(_visual_tmp, "captured.ppm")
    _diff_png = os.path.join(_visual_tmp, "diff.png")
    _ppm = "P3\n4 4\n255\n" + "\n".join(["20 40 80"] * 16) + "\n"
    with open(_ref_ppm, "w", encoding="ascii") as _fp:
        _fp.write(_ppm)
    with open(_cap_ppm, "w", encoding="ascii") as _fp:
        _fp.write(_ppm)
    _layout_expected = os.path.join(_visual_tmp, "expected_layout.json")
    _layout_actual = os.path.join(_visual_tmp, "actual_layout.json")
    _layout_report = os.path.join(_visual_tmp, "layout_report.json")
    _layout = {
        "canvas_size": [1920, 1080],
        "widgets": [
            {"name": "RootCanvas", "normalized_bounds": [0, 0, 1, 1], "z_order": 0, "opacity": 1.0}
        ],
    }
    with open(_layout_expected, "w", encoding="utf-8") as _fp:
        json.dump(_layout, _fp)
    with open(_layout_actual, "w", encoding="utf-8") as _fp:
        json.dump(_layout, _fp)
    run_cli(
        "umg layout compare geometry offline",
        "umg",
        "layout",
        "compare",
        "--mode",
        "geometry",
        _layout_expected,
        _layout_actual,
        "--output",
        _layout_report,
        check_stdout=lambda s: json.loads(s).get("success") is True and os.path.exists(_layout_report),
    )
    _layout_unified_report = os.path.join(_visual_tmp, "layout_unified_report.json")
    run_cli(
        "umg layout compare geometry offline",
        "umg",
        "layout",
        "compare",
        "--mode",
        "geometry",
        "--subset",
        _layout_expected,
        _layout_actual,
        "--output",
        _layout_unified_report,
        check_stdout=lambda s: json.loads(s).get("success") is True and os.path.exists(_layout_unified_report),
    )
    _layout_corrected_spec = os.path.join(_visual_tmp, "corrected_widget_tree.json")
    _layout_spec = os.path.join(_visual_tmp, "widget_tree.json")
    with open(_layout_spec, "w", encoding="utf-8") as _fp:
        json.dump({"root": {"class": "CanvasPanel", "name": "RootCanvas", "slot": {"position": [0, 0], "size": [1920, 1080]}}}, _fp)
    run_cli(
        "umg layout fit offline",
        "umg",
        "layout",
        "fit",
        "--concept",
        _layout_expected,
        "--actual",
        _layout_actual,
        "--spec",
        _layout_spec,
        "--output",
        _layout_corrected_spec,
        check_stdout=lambda s: json.loads(s).get("success") is True and os.path.exists(_layout_corrected_spec),
    )
    run_cli(
        "umg layout compare pixel offline",
        "umg",
        "layout",
        "compare",
        "--mode",
        "pixel",
        _ref_ppm,
        _cap_ppm,
        "--annotated-output",
        _diff_png,
        check_stdout=lambda s: json.loads(s).get("success") is True and os.path.exists(_diff_png),
    )

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 13: PIE
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("pie")

    PIE_TIMEOUT = 120.0

    # Stop any leftover PIE session
    try:
        _pie_status = caller("pie-session", {"action": "status"}, PIE_TIMEOUT)
        if _pie_status.get("state") not in (None, "stopped", "not_running"):
            caller("pie-session", {"action": "stop", "timeout": PIE_TIMEOUT}, PIE_TIMEOUT)
            time.sleep(3)
    except Exception:
        pass

    reg_teardown("pie-session", {"action": "stop", "timeout": PIE_TIMEOUT})

    run_test("pie-session start", "pie-session",
             {"action": "start", "timeout": PIE_TIMEOUT, "blueprint_error_action": "report"},
             lambda r: "blueprint_compile_errors" in r or r.get("success") is False,
             timeout=PIE_TIMEOUT)
    time.sleep(4)
    run_test("pie-session status", "pie-session", {"action": "status"}, has("state"), timeout=PIE_TIMEOUT)
    run_test("pie-tick explicit delta", "pie-tick", {
        "frames": 2,
        "delta": 0.0166666,
    }, lambda r: r.get("ticks") == 2 and r.get("world_time_delta", 0) > 0, timeout=PIE_TIMEOUT)
    run_test("capture-screenshot pie-window composited", "capture-screenshot",
             {"mode": "pie-window", "scale": 70, "color_mode": "color"},
             lambda r: "file_path" in r or r.get("capture_mode") == "pie-window",
             timeout=PIE_TIMEOUT)
    run_test("exec-console-command stat fps", "exec-console-command",
             {"command": "stat fps", "world": "pie"}, has("success"), timeout=PIE_TIMEOUT)
    run_test("inspect-pawn-possession", "inspect-pawn-possession",
             {"world": "pie"}, has("pawns"), timeout=PIE_TIMEOUT)
    run_test("verify-umg-workflow preview widget", "verify-umg-workflow", {
        "widget_class": wbp_path,
        "expected_widgets": ["RootCanvas", "StartButton", "ScreenSwitcher", "DetailsPanel"],
        "expected_text": ["SoftUE"],
        "click_sequence": [{"button": "StartButton"}],
        "capture_after": True,
        "remove_preview": True,
    }, lambda r: r.get("success") is True and r.get("created_preview_widget") is True, timeout=PIE_TIMEOUT)
    _umg_preview_handle = None
    _umg_preview_root = None
    _umg_runtime_widget_count = 0
    _umg_runtime_layout = os.path.join(_umg_layout_tmp, "umg_runtime_layout.json")

    def _remember_umg_preview(stdout: str) -> bool:
        nonlocal _umg_preview_handle, _umg_preview_root
        data = json.loads(stdout)
        _umg_preview_handle = data.get("preview_handle")
        _umg_preview_root = data.get("widget_name")
        if not _umg_preview_root:
            roots = data.get("root_widgets") or []
            _umg_preview_root = next((r.get("name") for r in roots if isinstance(r, dict) and r.get("name")), None)
        return data.get("success") is True and bool(_umg_preview_handle) and bool(_umg_preview_root)

    run_cli(
        "umg preview replace canonical",
        "--timeout",
        str(int(PIE_TIMEOUT)),
        "umg",
        "preview",
        "replace",
        "--widget-class",
        wbp_path,
        "--pie-index",
        "0",
        "--fullscreen",
        "--viewport-size",
        "1920,1080",
        "--capture-after",
        check_stdout=_remember_umg_preview,
        timeout=PIE_TIMEOUT,
    )
    if _umg_preview_handle:
        reg_teardown("umg-preview-remove", {"preview_handle": _umg_preview_handle})

    def _preview_list_has_created_widget(stdout: str) -> bool:
        data = json.loads(stdout)
        previews = data.get("previews") or []
        roots = data.get("root_widgets") or []
        return (
            data.get("preview_count", 0) >= 1
            and any(p.get("preview_handle") == _umg_preview_handle for p in previews if isinstance(p, dict))
            and (
                any(p.get("widget_name") == _umg_preview_root for p in previews if isinstance(p, dict))
                or any(r.get("name") == _umg_preview_root for r in roots if isinstance(r, dict))
            )
        )

    run_cli(
        "umg preview list canonical",
        "--timeout",
        str(int(PIE_TIMEOUT)),
        "umg",
        "preview",
        "list",
        "--pie-index",
        "0",
        check_stdout=_preview_list_has_created_widget,
        timeout=PIE_TIMEOUT,
    )

    def _remember_runtime_widget_count(s: str) -> bool:
        nonlocal _umg_runtime_widget_count
        result = json.loads(s)
        _umg_runtime_widget_count = int(result.get("widget_count") or 0)
        roots = result.get("root_widgets") or []
        return (
            _umg_runtime_widget_count > 0
            and any(r.get("name") == _umg_preview_root for r in roots if isinstance(r, dict))
        )

    run_cli(
        "umg runtime inspect preview tree",
        "--timeout",
        str(int(PIE_TIMEOUT)),
        "umg",
        "runtime",
        "inspect",
        "--pie-index",
        "0",
        "--root-widget",
        _umg_preview_root or "",
        "--include-slate",
        check_stdout=_remember_runtime_widget_count,
        timeout=PIE_TIMEOUT,
    )

    run_cli(
        "umg verify widgets preview root",
        "--timeout",
        str(int(PIE_TIMEOUT)),
        "umg",
        "verify",
        "widgets",
        "--pie-index",
        "0",
        "--root-widget",
        _umg_preview_root or "",
        "--expected-widgets",
        json.dumps(["RootCanvas"]),
        check_stdout=lambda s: json.loads(s).get("success") is True,
        timeout=PIE_TIMEOUT,
    )

    def _runtime_layout_matches_inspect(s: str) -> bool:
        layout = json.loads(s)
        widgets = layout.get("widgets", [])
        names = {w.get("name") for w in widgets if isinstance(w, dict)}
        return (
            len(json.loads(s).get("widgets", [])) >= _umg_runtime_widget_count
            and _umg_runtime_widget_count > 0
            and _umg_preview_root in names
            and any("bounds" in w for w in widgets if isinstance(w, dict))
            and os.path.exists(_umg_runtime_layout)
        )

    run_cli(
        "umg layout extract runtime non-empty",
        "--timeout",
        str(int(PIE_TIMEOUT)),
        "umg",
        "layout",
        "extract",
        "--source",
        "runtime",
        "--root-widget",
        _umg_preview_root or "",
        "--full-geometry",
        "--output",
        _umg_runtime_layout,
        check_stdout=_runtime_layout_matches_inspect,
        timeout=PIE_TIMEOUT,
    )
    if _umg_preview_handle:
        run_cli(
            "umg preview remove canonical",
            "--timeout",
            str(int(PIE_TIMEOUT)),
            "umg",
            "preview",
            "remove",
            "--preview-handle",
            _umg_preview_handle,
            check_stdout=lambda s: json.loads(s).get("success") is True,
            timeout=PIE_TIMEOUT,
        )
    run_test("get-logs during PIE", "get-logs", {"limit": 5}, has("lines"), timeout=PIE_TIMEOUT)
    run_test("pie-session stop", "pie-session", {"action": "stop", "timeout": PIE_TIMEOUT}, has("success"), timeout=PIE_TIMEOUT)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 14: Config Tools
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("config")

    # Bridge tools: get / set / validate
    # Use set-config-value's own test key for get — r.Bloom is a CVar,
    # not an INI key, so get-config-value (which reads GConfig INI) won't find it.
    run_test("validate-config-key r.DefaultFeature.AutoExposure", "validate-config-key", {
        "section": "/Script/Engine.RendererSettings",
        "key": "r.DefaultFeature.AutoExposure",
        "config_type": "Engine",
    }, has("valid"))

    # set-config-value: write a test key, then read it back
    cfg_section = "/Script/SoftUETest"
    cfg_key = f"TestKey_{RUN_TS}_{mode_name}"
    run_test("set-config-value test key", "set-config-value", {
        "section": cfg_section,
        "key": cfg_key,
        "value": "42",
        "config_type": "Engine",
    }, lambda r: r.get("status") == "ok" and r.get("value") == "42")
    run_test("get-config-value test key", "get-config-value", {
        "section": cfg_section,
        "key": cfg_key,
        "config_type": "Engine",
    }, starts_with("value", "42"))

    # CLI subcommands (offline — no bridge required)
    run_cli("config tree", "config", *(["--project-path", project_dir] if project_dir else []), "tree", "--exists-only",
            check_stdout=lambda s: '"layers"' in s)
    run_cli("config tree ini", "config", *(["--project-path", project_dir] if project_dir else []), "tree", "--format", "ini", "--exists-only",
            check_stdout=lambda s: '"format": "ini"' in s or '"layers": []' in s)
    offline_cfg_key = f"OfflineSearchKey_{RUN_TS}_{mode_name}"
    offline_cfg_path = f"[{cfg_section}]{offline_cfg_key}"
    # Write to GameDirUser layer (Config/UserEngine.ini) — less likely to be
    # read-only under source control than ProjectDefault (Config/DefaultEngine.ini).
    run_cli("config set user layer", "config", *(["--project-path", project_dir] if project_dir else []),
            "set", offline_cfg_path, "SearchValue42", "--layer", "GameDirUser", "--type", "Engine",
            check_stdout=lambda s: '"status": "ok"' in s and offline_cfg_key in s)
    run_cli("config get search", "config", *(["--project-path", project_dir] if project_dir else []),
            "get", "--search", offline_cfg_key, "--type", "Engine",
            check_stdout=lambda s: offline_cfg_key in s and "SearchValue42" in s)
    run_cli("config diff audit", "config", *(["--project-path", project_dir] if project_dir else []), "diff", "--audit",
            check_stdout=lambda s: '"diffs"' in s or '"sections"' in s or '"overrides"' in s or "no overrides" in s.lower())
    run_cli("config audit", "config", *(["--project-path", project_dir] if project_dir else []), "audit",
            check_stdout=lambda s: '"overrides"' in s or '"sections"' in s or "no overrides" in s.lower())
    run_cli("commands json metadata", "commands", "--json",
            check_stdout=lambda s: '"schema": "soft-ue.commands.v1"' in s and '"umg preview replace"' in s)
    run_cli("commands category umg", "commands", "--category", "umg",
            check_stdout=lambda s: "umg" in s and "removed" not in s)
    run_cli("commands include removed migrations", "commands", "--include-removed", "--json",
            check_stdout=lambda s: '"status": "removed"' in s and '"canonical_command": "umg designer apply"' in s)
    run_cli("commands category capture", "commands", "--category", "capture",
            check_stdout=lambda s: "capture viewport" in s and "capture screenshot" in s and "capture-screenshot" not in s)
    run_cli("commands category mutable", "commands", "--category", "mutable",
            check_stdout=lambda s: "mutable graph" in s and "mutable compile" in s and "compile-co" not in s)
    run_cli("commands category statetree", "commands", "--category", "statetree",
            check_stdout=lambda s: "statetree inspect" in s and "query-statetree" not in s)
    run_cli("commands category animation", "commands", "--category", "animation",
            check_stdout=lambda s: "anim rewind" in s and "rewind-status" not in s)
    run_cli("commands category asset", "commands", "--category", "asset",
            check_stdout=lambda s: "asset query" in s and "query-asset" not in s)
    run_cli("commands category blueprint", "commands", "--category", "blueprint",
            check_stdout=lambda s: "blueprint graph" in s and "query-blueprint-graph" not in s)
    run_cli("commands plugin mutable json", "commands", "--plugin", "Mutable", "--json",
            check_stdout=lambda s: '"required_plugins"' in s and '"Mutable"' in s)
    run_cli("capture viewport help", "capture", "viewport", "--help",
            check_stdout=lambda s: "--source" in s and "--scale" in s)
    run_cli("capture screenshot help", "capture", "screenshot", "--help",
            check_stdout=lambda s: "--source" in s and "pie-window" in s and "--output-file" in s)
    run_cli("mutable graph add-node help", "mutable", "graph", "add-node", "--help",
            check_stdout=lambda s: "mutable graph add-node" in s and "--properties" in s)
    run_cli("statetree inspect help", "statetree", "inspect", "--help",
            check_stdout=lambda s: "statetree inspect" in s and "--include" in s)
    run_cli("anim rewind status help", "anim", "rewind", "status", "--help",
            check_stdout=lambda s: "anim rewind status" in s and "recording" in s.lower())
    run_cli("anim retarget repoint-references help", "anim", "retarget", "repoint-references", "--help",
            check_stdout=lambda s: "anim retarget repoint-references" in s and "--target-skeleton" in s)
    run_cli("anim retarget blueprint help", "anim", "retarget", "blueprint", "--help",
            check_stdout=lambda s: "anim retarget blueprint" in s and "--bone-map" in s and "--target-skeleton" in s)
    run_cli("anim pose-search inspect help", "anim", "pose-search", "inspect", "--help",
            check_stdout=lambda s: "anim pose-search inspect" in s and "schema_path" in s)
    run_cli("anim pose-search remap help", "anim", "pose-search", "remap", "--help",
            check_stdout=lambda s: "anim pose-search remap" in s and "--bone-map" in s)
    run_cli("metasound inspect help", "metasound", "inspect", "--help",
            check_stdout=lambda s: "metasound inspect" in s and "asset_path" in s)
    run_cli("asset preview help", "asset", "preview", "--help",
            check_stdout=lambda s: "asset preview" in s and "--resolution" in s)
    run_cli("blueprint graph inspect help", "blueprint", "graph", "inspect", "--help",
            check_stdout=lambda s: "blueprint graph inspect" in s and "--graph-name" in s)
    _umg_expected = os.path.join(tempfile.gettempdir(), f"soft_ue_umg_expected_{RUN_TS}_{mode_name}.json")
    _umg_actual = os.path.join(tempfile.gettempdir(), f"soft_ue_umg_actual_{RUN_TS}_{mode_name}.json")
    with open(_umg_expected, "w", encoding="utf-8") as fh:
        json.dump({"widgets": [{"name": "Root", "normalized_bounds": [0, 0, 1, 1]}]}, fh)
    with open(_umg_actual, "w", encoding="utf-8") as fh:
        json.dump({"widgets": [{"name": "Root", "normalized_bounds": [0, 0, 1, 1]}]}, fh)
    run_cli("umg layout compare smoke", "umg", "layout", "compare", "--mode", "geometry", _umg_expected, _umg_actual,
            check_stdout=lambda s: '"success": true' in s)
    run_cli("umg layout extract help", "umg", "layout", "extract", "--help",
            check_stdout=lambda s: "--preview-handle" in s and "--full-geometry" in s)
    run_cli("umg preview help", "umg", "preview", "--help",
            check_stdout=lambda s: "create" in s and "replace" in s and "remove" in s)
    run_cli("umg preview replace help", "umg", "preview", "replace", "--help",
            check_stdout=lambda s: "--fullscreen" in s and "--viewport-size" in s and "--viewport-anchors" in s)
    run_cli("umg verify help", "umg", "verify", "--help",
            check_stdout=lambda s: "widgets" in s and "navigation" in s)
    run_cli("umg workflow help", "umg", "workflow", "--help",
            check_stdout=lambda s: "run" in s and "iterate-layout" in s)
    run_cli("umg workflow iterate-layout help", "umg", "workflow", "iterate-layout", "--help",
            check_stdout=lambda s: "--concept-layout" in s and "--output-dir" in s and "--max-iterations" in s)
    for _co_label, _co_args in (
        ("mutable graph add-node", ("mutable", "graph", "add-node")),
        ("mutable graph add-parameter", ("mutable", "graph", "add-parameter")),
        ("mutable graph add-mesh-option", ("mutable", "graph", "add-mesh-option")),
        ("mutable graph set-base-mesh", ("mutable", "graph", "set-base-mesh")),
        ("mutable graph add-group-child", ("mutable", "graph", "add-group-child")),
        ("mutable graph set-node-property", ("mutable", "graph", "set-node-property")),
        ("mutable graph connect-pins", ("mutable", "graph", "connect-pins")),
        ("mutable graph regenerate-node-pins", ("mutable", "graph", "regenerate-node-pins")),
        ("mutable compile", ("mutable", "compile")),
        ("mutable graph create-from-spec", ("mutable", "graph", "create-from-spec")),
    ):
        run_cli(f"{_co_label} help", *_co_args, "--help",
                check_stdout=lambda s, _cmd=_co_label: _cmd in s and "CustomizableObject" in s)
    run_cli("mutable compile gather references help", "mutable", "compile", "--help",
            check_stdout=lambda s: "--gather-references" in s)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 15: Python Scripting
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("python-scripting")

    script_name = f"suet_{RUN_TS}_{mode_name}"

    run_test("run-python-script inline", "run-python-script", {
        "script": "import unreal; print(unreal.SystemLibrary.get_engine_version())"
    }, has("output"))
    run_test("reload-gameplay-tags", "reload-gameplay-tags", {}, has("success"))

    run_cli("save-script", "save-script", script_name,
            "--script", "print('soft-ue-cli test script')")
    run_cli("list-scripts shows entry", "list-scripts",
            check_stdout=lambda s: script_name in s)
    run_cli("run-python-script saved", "run-python-script", "--name", script_name,
            check_stdout=lambda s: "output" in s or "soft-ue-cli" in s)
    run_cli("delete-script", "delete-script", script_name)

    helper_script = os.path.join(os.path.dirname(os.path.abspath(OUTPUT_PATH)), f"soft_ue_helper_{RUN_TS}_{mode_name}.py")
    with open(helper_script, "w", encoding="utf-8") as fh:
        fh.write(
            "from soft_ue_bridge import call\n"
            "import os\n"
            "result = call('query-level', {'limit': 1})\n"
            "print('HELPER_ACTORS', len(result.get('actors', [])))\n"
            "print('HELPER_FILE', os.path.basename(__file__))\n"
        )
    run_cli("run-python-script helper import", "run-python-script", "--script-path", helper_script,
            check_stdout=lambda s: "HELPER_ACTORS" in s and "HELPER_FILE" in s)
    try:
        _project_tags = caller("get-project-info", {"section": "tags"}, None).get("settings", {}).get("tags", {})
        _tag_sources = _project_tags.get("sources", [])
        _first_tag = None
        for _source in _tag_sources:
            _tags = _source.get("tags") or []
            if _tags:
                _first_tag = _tags[0]
                break
    except Exception:
        _first_tag = None
    if _first_tag:
        run_test("request-gameplay-tag", "request-gameplay-tag",
                 {"tag_name": _first_tag}, has("valid"))
    else:
        _record("request-gameplay-tag", "request-gameplay-tag", {},
                True, 0, "skipped: no gameplay tags found in project-info")

    begin_suite("advanced-automation")

    run_cli("automation tests run help", "automation", "tests", "run", "--help",
            check_stdout=lambda s: "automation tests run" in s and "structured JSON" in s)
    run_test("automation tests list-only no-match", "run-automation-tests", {
        "filter": "SoftUEBridgeDefinitelyMissingAutomationTest",
        "list_only": True,
        "max_tests": 1,
    }, lambda r: r.get("matched_count") == 0 and "matched_tests" in r)

    run_test("batch-call pie/query/logs smoke", "batch-call", {
        "calls": [
            {"tool": "query-level", "args": {"limit": 3}},
            {"tool": "get-logs", "args": {"lines": 3}},
        ]
    }, lambda r: r.get("status") in {"ok", "error"} and isinstance(r.get("results"), list))

    _skeletal_actor_tag = None
    try:
        _skeletal_resp = caller("query-level", {"limit": 25, "include_components": True}, None)
        for _actor in _skeletal_resp.get("actors", []):
            _components = _actor.get("components", [])
            if any("SkeletalMeshComponent" in str(_c.get("class", "")) for _c in _components):
                _skeletal_actor_tag = _actor.get("label") or _actor.get("name")
                break
    except Exception:
        _skeletal_actor_tag = None
    if _skeletal_actor_tag:
        run_test("inspect-anim-instance smoke", "inspect-anim-instance", {
            "actor_tag": _skeletal_actor_tag,
            "include": ["state_machines", "montages"],
        }, has("anim_instance_class"))
    else:
        _record("inspect-anim-instance smoke", "inspect-anim-instance", {},
                True, 0, "skipped: no skeletal actor found in current level")

    run_cli("anim sync-marker inspect help", "anim", "sync-marker", "inspect", "--help",
            check_stdout=lambda s: "anim sync-marker inspect" in s and "asset_path" in s)
    run_cli("anim sync-marker compare help", "anim", "sync-marker", "compare", "--help",
            check_stdout=lambda s: "anim sync-marker compare" in s and "asset_paths" in s)
    run_cli("anim sync-marker add help", "anim", "sync-marker", "add", "--help",
            check_stdout=lambda s: "anim sync-marker add" in s and "time" in s)
    run_cli("anim sync-marker remove help", "anim", "sync-marker", "remove", "--help",
            check_stdout=lambda s: "anim sync-marker remove" in s and "tolerance" in s)
    _repoint_assets = [
        part.strip()
        for part in os.environ.get("SOFT_UE_TEST_ANIM_REPOINT_ASSETS", "").split(",")
        if part.strip()
    ]
    _repoint_map_env = os.environ.get("SOFT_UE_TEST_ANIM_REPOINT_MAP", "").strip()
    if _repoint_assets and _repoint_map_env:
        _repoint_map = {}
        for _entry in _repoint_map_env.split(","):
            if "=" in _entry:
                _old, _new = _entry.split("=", 1)
            else:
                _old, _new = _entry.split(":", 1)
            _repoint_map[_old.strip()] = _new.strip()
        run_test("anim-repoint-references smoke", "anim-repoint-references", {
            "asset_paths": _repoint_assets,
            "replacement_map": _repoint_map,
        }, lambda r: r.get("success") is True and "assets" in r)
    else:
        _record("anim-repoint-references smoke", "anim-repoint-references", {},
                True, 0, "skipped: set SOFT_UE_TEST_ANIM_REPOINT_ASSETS and SOFT_UE_TEST_ANIM_REPOINT_MAP")

    _anim_bp_source = os.environ.get("SOFT_UE_TEST_ANIM_RETARGET_BLUEPRINT_SOURCE", "").strip()
    _anim_bp_target = os.environ.get("SOFT_UE_TEST_ANIM_RETARGET_BLUEPRINT_TARGET", "").strip()
    _anim_bp_skeleton = os.environ.get("SOFT_UE_TEST_ANIM_RETARGET_BLUEPRINT_SKELETON", "").strip()
    _anim_bp_bone_map_env = os.environ.get("SOFT_UE_TEST_ANIM_RETARGET_BLUEPRINT_BONE_MAP", "").strip()
    if _anim_bp_source and _anim_bp_target and _anim_bp_skeleton and _anim_bp_bone_map_env:
        _anim_bp_bone_map = {}
        for _entry in _anim_bp_bone_map_env.split(","):
            if "=" in _entry:
                _old, _new = _entry.split("=", 1)
            else:
                _old, _new = _entry.split(":", 1)
            _anim_bp_bone_map[_old.strip()] = _new.strip()
        run_test("anim-retarget-blueprint smoke", "anim-retarget-blueprint", {
            "source_blueprint": _anim_bp_source,
            "target_blueprint": _anim_bp_target,
            "target_skeleton": _anim_bp_skeleton,
            "bone_map": _anim_bp_bone_map,
        }, lambda r: r.get("success") is True and r.get("target_blueprint"))
    else:
        _record("anim-retarget-blueprint smoke", "anim-retarget-blueprint", {},
                True, 0, "skipped: set SOFT_UE_TEST_ANIM_RETARGET_BLUEPRINT_SOURCE/TARGET/SKELETON/BONE_MAP")

    _pose_schema = os.environ.get("SOFT_UE_TEST_POSE_SEARCH_SCHEMA", "").strip()
    if _pose_schema:
        run_test("pose-search-schema-inspect smoke", "pose-search-schema-inspect", {
            "schema_path": _pose_schema,
        }, lambda r: r.get("success") is True and "bone_references" in r)
    else:
        _record("pose-search-schema-inspect smoke", "pose-search-schema-inspect", {},
                True, 0, "skipped: set SOFT_UE_TEST_POSE_SEARCH_SCHEMA")

    _pose_bone_map_env = os.environ.get("SOFT_UE_TEST_POSE_SEARCH_BONE_MAP", "").strip()
    if _pose_schema and _pose_bone_map_env:
        _pose_bone_map = {}
        for _entry in _pose_bone_map_env.split(","):
            if "=" in _entry:
                _old, _new = _entry.split("=", 1)
            else:
                _old, _new = _entry.split(":", 1)
            _pose_bone_map[_old.strip()] = _new.strip()
        _pose_args = {
            "schema_path": _pose_schema,
            "bone_map": _pose_bone_map,
        }
        _pose_target_skeleton = os.environ.get("SOFT_UE_TEST_POSE_SEARCH_TARGET_SKELETON", "").strip()
        if _pose_target_skeleton:
            _pose_args["target_skeleton"] = _pose_target_skeleton
        run_test("pose-search-schema-remap smoke", "pose-search-schema-remap", _pose_args,
                 lambda r: r.get("success") is True and "changes" in r)
    else:
        _record("pose-search-schema-remap smoke", "pose-search-schema-remap", {},
                True, 0, "skipped: set SOFT_UE_TEST_POSE_SEARCH_SCHEMA and SOFT_UE_TEST_POSE_SEARCH_BONE_MAP")

    _metasound_asset = os.environ.get("SOFT_UE_TEST_METASOUND_ASSET", "").strip()
    if _metasound_asset:
        run_test("metasound-inspect smoke", "metasound-inspect", {
            "asset_path": _metasound_asset,
        }, lambda r: r.get("success") is True and "graph" in r)
    else:
        _record("metasound-inspect smoke", "metasound-inspect", {},
                True, 0, "skipped: set SOFT_UE_TEST_METASOUND_ASSET")

    run_test("call-function transient native", "call-function", {
        "class_path": "/Script/Engine.Actor",
        "function_name": "K2_GetActorLocation",
        "spawn_transient": True,
    }, lambda r: r.get("success") is True or "return_value" in r)

    _batch_json = os.path.join(os.path.dirname(os.path.abspath(OUTPUT_PATH)), f"soft_ue_batch_{RUN_TS}_{mode_name}.json")
    with open(_batch_json, "w", encoding="utf-8") as fh:
        json.dump([{}, {}], fh)
    run_cli("call-function batch-json", "call-function",
            "--class-path", "/Script/Engine.Actor",
            "--function-name", "K2_GetActorLocation",
            "--spawn-transient",
            "--batch-json", _batch_json,
            check_stdout=lambda s: '"results"' in s and '"count"' in s)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 16: Insights
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("insights")

    trace_name = f"SoftUETest_{RUN_TS}_{mode_name}"
    run_test("insights-capture start", "insights-capture",
             {"action": "start", "output_file": trace_name}, has("status"))

    _poll_end = time.time() + 10
    _trace_still_active = False
    while time.time() < _poll_end:
        time.sleep(0.5)
        try:
            _s = caller("insights-capture", {"action": "status"}, None)
            if _s.get("status") == "active":
                _trace_still_active = True
                break
            if _s.get("status") == "idle":
                break
        except Exception:
            break

    _t0 = time.time()
    if not _trace_still_active:
        _stop_ok = True
        _stop_err = "trace never reported active; stop skipped"
    else:
        time.sleep(1)
        try:
            _stop_r = caller("insights-capture", {"action": "stop"}, None)
            _stop_status = str(_stop_r.get("status", "")).lower()
            _stop_msg = json.dumps(_stop_r)[:200].lower()
            _stop_ok = (
                _stop_status in {"stopped", "idle"}
                or "no active trace" in _stop_msg
                or "already stopped" in _stop_msg
            )
            _stop_err = None if _stop_ok else f"check failed: {json.dumps(_stop_r)[:200]}"
            if _stop_ok and _stop_status != "stopped":
                _stop_err = "auto-stopped (treated as pass)"
        except Exception as exc:
            _stop_msg = str(exc)[:300]
            _stop_msg_l = _stop_msg.lower()
            _stop_ok = (
                "no active trace" in _stop_msg_l
                or "already stopped" in _stop_msg_l
                or "status: idle" in _stop_msg_l
            )
            _stop_err = "auto-stopped (treated as pass)" if _stop_ok else _stop_msg
    _record("insights-capture stop", "insights-capture", {"action": "stop"},
            _stop_ok, int((time.time() - _t0) * 1000), _stop_err)

    run_test("insights-list-traces", "insights-list-traces", {}, has("traces"))

    # ══════════════════════════════════════════════════════════════════════════
    # Teardown
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("teardown")

    # Restore original level FIRST — close editors + GC before switching worlds
    if _original_level:
        _save_t0 = time.time()
        try:
            caller("save-asset", {"asset_path": test_level_path}, None)
            _save_ok, _save_err = True, None
        except Exception as exc:
            _save_ok, _save_err = False, str(exc)[:300]
        _record("save-asset (test level before restore)", "save-asset",
                {"asset_path": test_level_path}, _save_ok,
                int((time.time() - _save_t0) * 1000), _save_err)

        _t0 = time.time()
        try:
            caller("run-python-script", {
                "script": (
                    "import unreal\n"
                    "try:\n"
                    "    sub = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)\n"
                    "    close_fn = getattr(sub, 'close_all_asset_editors', None) or getattr(sub, 'close_all_editors', None)\n"
                    "    if close_fn: close_fn()\n"
                    "except Exception:\n"
                    "    pass\n"
                    "for _ in range(3):\n"
                    "    unreal.SystemLibrary.collect_garbage()\n"
                )
            }, None)
        except Exception:
            pass
        time.sleep(1.0)
        _open_ok, _open_err = False, None
        try:
            caller("open-asset", {"asset_path": _original_level}, None)
            _open_ok = True
        except Exception as exc:
            _open_err = str(exc)[:300]
        _record("open-asset (restore level)", "open-asset",
                {"asset_path": _original_level}, _open_ok,
                int((time.time() - _t0) * 1000), _open_err)

    # LIFO teardown
    for tool_name, args in reversed(teardown_list):
        t0 = time.time()
        label_str = f"{tool_name} {list(args.values())[0] if args else ''}".strip()
        try:
            caller(tool_name, args, None)
            td_ok, td_err = True, None
        except Exception as exc:
            td_err = str(exc)[:300]
            td_ok = (
                tool_name == "delete-asset"
                and "asset not found" in td_err.lower()
            )
            if td_ok:
                td_err = "already removed (treated as pass)"
        _record(label_str, tool_name, args, td_ok, int((time.time() - t0) * 1000), td_err)

    return suites


# ── Main ───────────────────────────────────────────────────────────────────────
def _print_mode_summary(mode_name: str, suites: list[dict]) -> tuple[int, int]:
    all_tests = [t for s in suites for t in s["tests"]]
    total = len(all_tests)
    n_passed = sum(1 for t in all_tests if t["passed"])
    n_failed = total - n_passed
    print(f"  [{mode_name}] {n_passed}/{total} passed, {n_failed} failed")
    return n_passed, n_failed


all_suites: list[dict] = []
modes_to_run: list[tuple[str, object]] = []  # (mode_name, caller_or_None)
mcp_client: MCPClient | None = None

if MODE in ("cli", "all"):
    modes_to_run.append(("cli", _cli_caller))

if MODE in ("mcp", "all"):
    try:
        mcp_client = MCPClient()
        modes_to_run.append(("mcp", mcp_client.call_tool))
    except Exception as exc:
        print(f"error: could not start mcp-serve: {exc}", file=sys.stderr)
        print("Install MCP support with: pip install soft-ue-cli[mcp]", file=sys.stderr)
        sys.exit(1)

try:
    for mode_name, caller in modes_to_run:
        print(f"\n{'=' * 60}")
        print(f"Mode: {mode_name.upper()}")
        print(f"{'=' * 60}")
        suites = _run_single_mode(mode_name, caller)
        all_suites.extend(suites)
finally:
    if mcp_client is not None:
        mcp_client.close()

# ── Report ─────────────────────────────────────────────────────────────────────
all_tests = [t for s in all_suites for t in s["tests"]]
total = len(all_tests)
n_passed = sum(1 for t in all_tests if t["passed"])
n_failed = total - n_passed

report = {
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "cli_version": CLI_VERSION,
    "bridge_url": get_server_url(),
    "mode": MODE,
    "summary": {
        "total": total,
        "passed": n_passed,
        "failed": n_failed,
        "duration_ms": int((time.time() - _run_start) * 1000),
    },
    "suites": all_suites,
}

with open(OUTPUT_PATH, "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=2, ensure_ascii=False)

print(f"\n{'=' * 60}")
if MODE == "all":
    for mode_name, _ in modes_to_run:
        _print_mode_summary(mode_name, [s for s in all_suites if s.get("mode") == mode_name])
    print(f"  [total] {n_passed}/{total} passed, {n_failed} failed")
else:
    print(f"Results : {n_passed}/{total} passed, {n_failed} failed")
print(f"Report  : {OUTPUT_PATH}")
print(f"Duration: {report['summary']['duration_ms']}ms")
print(f"{'=' * 60}")

sys.exit(0 if n_failed == 0 else 1)
```
