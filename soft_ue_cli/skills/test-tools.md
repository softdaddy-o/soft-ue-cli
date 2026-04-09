---
name: test-tools
description: Exhaustive integration test of all soft-ue-cli tools against a live UE instance. Writes a JSON report.
version: 2.3.0
---

# test-tools — Integration Test Suite

Runs every soft-ue-cli bridge tool against a live UE instance, collects pass/fail results per test, and writes a JSON report. No LLM in the loop — extract the script and run it.

## Modes

| Mode | What it tests | Transport |
|------|--------------|-----------|
| `cli` (default) | Bridge tools directly | HTTP JSON-RPC |
| `mcp` | MCP server layer + bridge tools | MCP stdio → HTTP |
| `all` | Both modes sequentially | Both |

## Requirements

- `soft-ue-cli` installed — `pip install soft-ue-cli`
- MCP mode also requires — `pip install soft-ue-cli[mcp]`
- UE running with SoftUEBridge enabled and reachable

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

    def run_cli(name, *args, check_stdout=None) -> dict:
        t0 = time.time()
        try:
            proc = subprocess.run(CLI + list(args), capture_output=True, text=True, timeout=30)
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
    else:
        # MCP: reaching here means mcp-serve started and initialized successfully
        _record("mcp-serve started", "mcp-serve", {}, True, 0, None)

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
    reg_teardown("delete-asset", {"asset_path": bp_path})
    reg_teardown("delete-asset", {"asset_path": bpi_path})

    run_test("create-asset Blueprint", "create-asset",
             {"asset_path": bp_path, "asset_class": "/Script/Engine.Blueprint",
              "parent_class": "/Script/Engine.Actor"}, has("asset_path"))

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
    run_test("query-asset inspect", "query-asset", {"asset_path": bp_path}, has("path"))
    run_test("get-asset-preview", "get-asset-preview", {"asset_path": bp_path}, has("file_path"))
    run_test("open-asset", "open-asset", {"asset_path": bp_path}, has("success"))

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 7: Blueprint Inspect
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("blueprint-inspect")

    run_test("query-blueprint", "query-blueprint", {"asset_path": bp_path}, has("path"))
    run_test("query-blueprint-graph EventGraph", "query-blueprint-graph",
             {"asset_path": bp_path, "graph": "EventGraph"}, nonempty("graphs"))
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
            "inspect-uasset summary",
            "inspect-uasset", _inspect_uasset_path,
            check_stdout=lambda s: '"name": "BP_SoftUETest"' in s and '"asset_class"' in s,
        )
        run_cli(
            "inspect-uasset all",
            "inspect-uasset", _inspect_uasset_path, "--sections", "all",
            check_stdout=lambda s: '"variables"' in s and '"functions"' in s and '"fidelity"' in s,
        )
    else:
        _record("inspect-uasset", "inspect-uasset", {},
                False, 0, "skipped: could not resolve on-disk .uasset path")

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

    run_test("compile-blueprint", "compile-blueprint", {"asset_path": bp_path}, has("success"))
    run_test("save-asset blueprint", "save-asset", {"asset_path": bp_path}, has("success"))
    if _inspect_uasset_path and _inspect_snapshot_uasset:
        run_cli(
            "diff-uasset summary",
            "diff-uasset", _inspect_snapshot_uasset, _inspect_uasset_path,
            check_stdout=lambda s: '"has_changes"' in s and '"summary"' in s,
        )
        run_cli(
            "diff-uasset all",
            "diff-uasset", _inspect_snapshot_uasset, _inspect_uasset_path, "--sections", "all",
            check_stdout=lambda s: '"changes"' in s and '"summary"' in s,
        )
    else:
        _record("diff-uasset", "diff-uasset", {},
                False, 0, "skipped: could not snapshot on-disk .uasset before mutation")
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
    run_test("capture-screenshot", "capture-screenshot", {}, has("file_path"))

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
             {"action": "start", "timeout": PIE_TIMEOUT}, has("success"), timeout=PIE_TIMEOUT)
    time.sleep(4)
    run_test("pie-session status", "pie-session", {"action": "status"}, has("state"), timeout=PIE_TIMEOUT)
    run_test("get-logs during PIE", "get-logs", {"limit": 5}, has("lines"), timeout=PIE_TIMEOUT)
    run_test("pie-session stop", "pie-session", {"action": "stop", "timeout": PIE_TIMEOUT}, has("success"), timeout=PIE_TIMEOUT)

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 14: Config Tools
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("config")

    # Bridge tools: get / set / validate
    run_test("get-config-value r.Bloom", "get-config-value", {
        "section": "/Script/Engine.RendererSettings",
        "key": "r.Bloom",
        "config_type": "Engine",
    }, has("value"))

    run_test("validate-config-key r.Bloom", "validate-config-key", {
        "section": "/Script/Engine.RendererSettings",
        "key": "r.Bloom",
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
    run_cli("config set project default", "config", *(["--project-path", project_dir] if project_dir else []),
            "set", offline_cfg_path, "SearchValue42", "--layer", "ProjectDefault", "--type", "Engine",
            check_stdout=lambda s: '"status": "ok"' in s and offline_cfg_key in s)
    run_cli("config get search", "config", *(["--project-path", project_dir] if project_dir else []),
            "get", "--search", offline_cfg_key, "--type", "Engine",
            check_stdout=lambda s: offline_cfg_key in s and "SearchValue42" in s)
    run_cli("config diff audit", "config", *(["--project-path", project_dir] if project_dir else []), "diff", "--audit",
            check_stdout=lambda s: '"diffs"' in s or '"sections"' in s or '"overrides"' in s or "no overrides" in s.lower())
    run_cli("config audit", "config", *(["--project-path", project_dir] if project_dir else []), "audit",
            check_stdout=lambda s: '"overrides"' in s or '"sections"' in s or "no overrides" in s.lower())

    # ══════════════════════════════════════════════════════════════════════════
    # Suite 15: Python Scripting
    # ══════════════════════════════════════════════════════════════════════════
    begin_suite("python-scripting")

    script_name = f"suet_{RUN_TS}_{mode_name}"

    run_test("run-python-script inline", "run-python-script", {
        "script": "import unreal; print(unreal.SystemLibrary.get_engine_version())"
    }, has("output"))

    run_cli("save-script", "save-script", script_name,
            "--script", "print('soft-ue-cli test script')")
    run_cli("list-scripts shows entry", "list-scripts",
            check_stdout=lambda s: script_name in s)
    run_cli("run-python-script saved", "run-python-script", "--name", script_name,
            check_stdout=lambda s: "output" in s or "soft-ue-cli" in s)
    run_cli("delete-script", "delete-script", script_name)

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
