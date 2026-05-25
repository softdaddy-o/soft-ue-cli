"""Public command taxonomy and availability metadata."""

from __future__ import annotations

from copy import deepcopy
from typing import Any

from .command_aliases import CANONICAL_COMMAND_FOR_LEGACY


SCHEMA = "soft-ue.commands.v1"

LAYERS = {"offline", "bridge", "compatibility", "workflow", "skill", "support"}
CATEGORIES = {
    "animation",
    "asset",
    "blueprint",
    "build",
    "capture",
    "compare",
    "config",
    "inspect",
    "mutable",
    "preview",
    "statetree",
    "support",
    "umg",
    "verify",
    "workflow",
}
STATUSES = {"canonical", "compatibility", "deprecated"}


def _plugin(name: str, module: str = "", note: str = "") -> dict[str, Any]:
    return {
        "name": name,
        "module": module,
        "required": True,
        "note": note,
    }


def _entry(
    name: str,
    summary: str,
    *,
    layer: str,
    category: str,
    requires_bridge: bool,
    requires_editor: bool,
    requires_pie: bool = False,
    required_plugins: list[dict[str, Any]] | None = None,
    status: str = "canonical",
    canonical_command: str = "",
    examples: list[str] | None = None,
) -> dict[str, Any]:
    if layer not in LAYERS:
        raise ValueError(f"unknown command layer: {layer}")
    if category not in CATEGORIES:
        raise ValueError(f"unknown command category: {category}")
    if status not in STATUSES:
        raise ValueError(f"unknown command status: {status}")
    return {
        "name": name,
        "summary": summary,
        "layer": layer,
        "category": category,
        "requires_bridge": requires_bridge,
        "requires_editor": requires_editor,
        "requires_pie": requires_pie,
        "required_plugins": required_plugins or [],
        "status": status,
        "canonical_command": canonical_command,
        "examples": examples or [],
    }


_EXPLICIT_ENTRIES: dict[str, dict[str, Any]] = {
    "commands": _entry(
        "commands",
        "List CLI command taxonomy and availability metadata.",
        layer="offline",
        category="inspect",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli commands --json", "soft-ue-cli commands --category umg"],
    ),
    "umg": _entry(
        "umg",
        "Canonical UMG command family for designer, preview, verification, layout, and workflow operations.",
        layer="workflow",
        category="umg",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli umg layout compare --mode geometry expected.json actual.json"],
    ),
    "umg layout": _entry(
        "umg layout",
        "Canonical UMG layout extraction, comparison, and spec fitting family.",
        layer="offline",
        category="compare",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli umg layout compare --mode geometry expected.json actual.json"],
    ),
    "umg designer": _entry(
        "umg designer",
        "Canonical UMG designer inspection and authoring family.",
        layer="bridge",
        category="umg",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli umg designer apply /Game/UI/WBP_Menu --spec-file menu.json"],
    ),
    "umg navigation": _entry(
        "umg navigation",
        "Canonical UMG navigation contract validation and wiring family.",
        layer="bridge",
        category="umg",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli umg navigation wire /Game/UI/WBP_Menu --bindings-file navigation.json"],
    ),
    "umg preview": _entry(
        "umg preview",
        "Canonical tool-owned UMG preview widget lifecycle family.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg preview replace --widget-class /Game/UI/WBP_Menu.WBP_Menu_C"],
    ),
    "umg preview create": _entry(
        "umg preview create",
        "Create a tool-owned UMG preview widget and return a preview handle.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg preview create --widget-class /Game/UI/WBP_Menu.WBP_Menu_C"],
    ),
    "umg preview replace": _entry(
        "umg preview replace",
        "Replace existing tool-owned UMG previews with a new preview widget.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg preview replace --widget-class /Game/UI/WBP_Menu.WBP_Menu_C"],
    ),
    "umg preview remove": _entry(
        "umg preview remove",
        "Remove tool-owned UMG preview widgets by handle or PIE world.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg preview remove --preview-handle softue-preview:..."],
    ),
    "umg preview list": _entry(
        "umg preview list",
        "List tool-owned UMG preview widgets.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg preview list"],
    ),
    "umg verify": _entry(
        "umg verify",
        "Canonical UMG runtime and contract verification family.",
        layer="bridge",
        category="verify",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        examples=["soft-ue-cli umg verify widgets --expected-widgets '[\"StartButton\"]'"],
    ),
    "umg workflow": _entry(
        "umg workflow",
        "Canonical UMG workflow orchestration family.",
        layer="workflow",
        category="workflow",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli umg workflow run --plan workflow.json"],
    ),
    "apply-widget-tree": _entry(
        "apply-widget-tree",
        "Compatibility wrapper for applying a UMG designer tree spec.",
        layer="compatibility",
        category="umg",
        requires_bridge=True,
        requires_editor=True,
        status="compatibility",
        canonical_command="umg designer apply",
    ),
    "inspect-widget-blueprint": _entry(
        "inspect-widget-blueprint",
        "Compatibility wrapper for UMG designer inspection.",
        layer="compatibility",
        category="inspect",
        requires_bridge=True,
        requires_editor=True,
        status="compatibility",
        canonical_command="umg designer inspect",
    ),
    "wire-widget-navigation": _entry(
        "wire-widget-navigation",
        "Compatibility wrapper for UMG navigation wiring.",
        layer="compatibility",
        category="umg",
        requires_bridge=True,
        requires_editor=True,
        status="compatibility",
        canonical_command="umg navigation wire",
    ),
    "verify-umg-workflow": _entry(
        "verify-umg-workflow",
        "Compatibility wrapper for bundled UMG preview and workflow verification.",
        layer="compatibility",
        category="workflow",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        status="compatibility",
        canonical_command="umg workflow run",
    ),
    "extract-umg-layout": _entry(
        "extract-umg-layout",
        "Compatibility wrapper for UMG layout extraction.",
        layer="compatibility",
        category="compare",
        requires_bridge=False,
        requires_editor=False,
        status="compatibility",
        canonical_command="umg layout extract",
    ),
    "compare-umg-layout": _entry(
        "compare-umg-layout",
        "Compatibility wrapper for UMG geometry layout comparison.",
        layer="compatibility",
        category="compare",
        requires_bridge=False,
        requires_editor=False,
        status="compatibility",
        canonical_command="umg layout compare --mode geometry",
    ),
    "compare-umg-screenshot": _entry(
        "compare-umg-screenshot",
        "Compatibility wrapper for UMG screenshot comparison.",
        layer="compatibility",
        category="compare",
        requires_bridge=False,
        requires_editor=False,
        status="compatibility",
        canonical_command="umg layout compare --mode pixel",
    ),
    "capture": _entry(
        "capture",
        "Canonical capture command family for viewport and screenshot capture.",
        layer="bridge",
        category="capture",
        requires_bridge=True,
        requires_editor=False,
        examples=["soft-ue-cli capture viewport --scale 50"],
    ),
    "capture viewport": _entry(
        "capture viewport",
        "Canonical viewport capture command.",
        layer="bridge",
        category="capture",
        requires_bridge=True,
        requires_editor=False,
        examples=["soft-ue-cli capture viewport --source editor --width 640"],
    ),
    "capture screenshot": _entry(
        "capture screenshot",
        "Canonical screenshot capture command for editor windows, tabs, regions, viewport, and PIE window.",
        layer="bridge",
        category="capture",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli capture screenshot --source pie-window --output base64"],
    ),
    "capture-viewport": _entry(
        "capture-viewport",
        "Compatibility wrapper for viewport capture.",
        layer="compatibility",
        category="capture",
        requires_bridge=True,
        requires_editor=False,
        status="compatibility",
        canonical_command="capture viewport",
    ),
    "capture-screenshot": _entry(
        "capture-screenshot",
        "Compatibility wrapper for screenshot capture.",
        layer="compatibility",
        category="capture",
        requires_bridge=True,
        requires_editor=True,
        status="compatibility",
        canonical_command="capture screenshot --source <mode>",
    ),
    "capture-pie-screenshot": _entry(
        "capture-pie-screenshot",
        "Compatibility wrapper for PIE screenshot capture.",
        layer="compatibility",
        category="capture",
        requires_bridge=True,
        requires_editor=True,
        requires_pie=True,
        status="compatibility",
        canonical_command="capture screenshot --source pie-window",
    ),
    "mutable": _entry(
        "mutable",
        "Canonical Mutable/CustomizableObject command family.",
        layer="workflow",
        category="mutable",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")],
        examples=["soft-ue-cli mutable inspect graph /Game/Characters/CO_Hero.CO_Hero"],
    ),
    "mutable inspect": _entry(
        "mutable inspect",
        "Canonical Mutable inspection and diagnostics family.",
        layer="bridge",
        category="mutable",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")],
        examples=["soft-ue-cli mutable inspect parameters /Game/Characters/CO_Hero.CO_Hero"],
    ),
    "mutable graph": _entry(
        "mutable graph",
        "Canonical Mutable graph authoring family.",
        layer="bridge",
        category="mutable",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")],
        examples=["soft-ue-cli mutable graph add-node /Game/Characters/CO_Hero.CO_Hero CustomizableObjectNodeFloatParameter"],
    ),
    "mutable compile": _entry(
        "mutable compile",
        "Canonical Mutable compile command.",
        layer="bridge",
        category="mutable",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")],
        examples=["soft-ue-cli mutable compile /Game/Characters/CO_Hero.CO_Hero"],
    ),
    "statetree": _entry(
        "statetree",
        "Canonical StateTree inspection and authoring command family.",
        layer="workflow",
        category="statetree",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")],
        examples=["soft-ue-cli statetree inspect /Game/AI/ST_Enemy"],
    ),
    "statetree inspect": _entry(
        "statetree inspect",
        "Canonical StateTree inspection command.",
        layer="bridge",
        category="statetree",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")],
        examples=["soft-ue-cli statetree inspect /Game/AI/ST_Enemy"],
    ),
    "statetree state": _entry(
        "statetree state",
        "Canonical StateTree state authoring family.",
        layer="bridge",
        category="statetree",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")],
        examples=["soft-ue-cli statetree state add /Game/AI/ST_Enemy Patrol"],
    ),
    "statetree task": _entry(
        "statetree task",
        "Canonical StateTree task authoring family.",
        layer="bridge",
        category="statetree",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")],
        examples=["soft-ue-cli statetree task add /Game/AI/ST_Enemy Patrol UStateTreeTask_MoveTo"],
    ),
    "statetree transition": _entry(
        "statetree transition",
        "Canonical StateTree transition authoring family.",
        layer="bridge",
        category="statetree",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")],
        examples=["soft-ue-cli statetree transition add /Game/AI/ST_Enemy Patrol Attack"],
    ),
    "anim": _entry(
        "anim",
        "Canonical animation inspection, graph authoring, sync marker, and Rewind Debugger family.",
        layer="workflow",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim rewind status"],
    ),
    "anim instance": _entry(
        "anim instance",
        "Canonical animation instance inspection family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim instance inspect --actor-name BP_Hero"],
    ),
    "anim sync-marker": _entry(
        "anim sync-marker",
        "Canonical animation sync marker inspection and authoring family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim sync-marker inspect /Game/Animation/Run"],
    ),
    "anim state-machine": _entry(
        "anim state-machine",
        "Canonical AnimBlueprint state-machine authoring family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim state-machine add /Game/Animation/ABP_Hero Locomotion"],
    ),
    "anim state": _entry(
        "anim state",
        "Canonical AnimBlueprint state authoring family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim state add /Game/Animation/ABP_Hero Locomotion Run"],
    ),
    "anim transition": _entry(
        "anim transition",
        "Canonical AnimBlueprint transition authoring family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli anim transition add /Game/Animation/ABP_Hero Locomotion Idle Run"],
    ),
    "anim rewind": _entry(
        "anim rewind",
        "Canonical Rewind Debugger recording and inspection family.",
        layer="bridge",
        category="animation",
        requires_bridge=True,
        requires_editor=True,
        required_plugins=[_plugin("Animation Insights", "GameplayInsights", "Required for Rewind Debugger recording and inspection.")],
        examples=["soft-ue-cli anim rewind snapshot --actor-tag Player --time 1.25"],
    ),
    "asset": _entry(
        "asset",
        "Canonical asset inspection, preview, source-control, file, and authoring command family.",
        layer="workflow",
        category="asset",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli asset query --asset-path /Game/Data/DT_Items"],
    ),
    "asset query": _entry(
        "asset query",
        "Canonical asset query and inspection command.",
        layer="bridge",
        category="asset",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli asset query --asset-path /Game/Data/DT_Items"],
    ),
    "asset preview": _entry(
        "asset preview",
        "Canonical asset thumbnail preview command.",
        layer="bridge",
        category="preview",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli asset preview /Game/Textures/T_Player --resolution 512"],
    ),
    "asset file": _entry(
        "asset file",
        "Canonical offline .uasset inspection family.",
        layer="offline",
        category="asset",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli asset inspect-file D:/Project/Content/BP_Player.uasset"],
    ),
    "asset inspect-file": _entry(
        "asset inspect-file",
        "Canonical offline .uasset inspection command.",
        layer="offline",
        category="asset",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli asset inspect-file D:/Project/Content/BP_Player.uasset"],
    ),
    "asset diff-file": _entry(
        "asset diff-file",
        "Canonical offline .uasset diff command.",
        layer="offline",
        category="asset",
        requires_bridge=False,
        requires_editor=False,
        examples=["soft-ue-cli asset diff-file BP_Old.uasset BP_New.uasset"],
    ),
    "asset create": _entry(
        "asset create",
        "Canonical asset creation command.",
        layer="bridge",
        category="asset",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli asset create /Game/Blueprints/BP_NewActor Blueprint --parent-class Actor"],
    ),
    "asset save": _entry(
        "asset save",
        "Canonical asset save command.",
        layer="bridge",
        category="asset",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli asset save /Game/Blueprints/BP_Player"],
    ),
    "blueprint": _entry(
        "blueprint",
        "Canonical Blueprint inspection, graph authoring, pin, interface, and compile command family.",
        layer="workflow",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint graph inspect /Game/Blueprints/BP_Player"],
    ),
    "blueprint inspect": _entry(
        "blueprint inspect",
        "Canonical Blueprint asset inspection command.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint inspect /Game/Blueprints/BP_Player"],
    ),
    "blueprint graph": _entry(
        "blueprint graph",
        "Canonical Blueprint graph inspection family.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint graph inspect /Game/Blueprints/BP_Player --graph-name EventGraph"],
    ),
    "blueprint node": _entry(
        "blueprint node",
        "Canonical Blueprint graph node authoring family.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint node add /Game/Blueprints/BP_Player K2Node_CallFunction"],
    ),
    "blueprint pin": _entry(
        "blueprint pin",
        "Canonical Blueprint graph pin wiring family.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint pin connect /Game/Blueprints/BP_Player Source then Target execute"],
    ),
    "blueprint interface": _entry(
        "blueprint interface",
        "Canonical Blueprint interface authoring family.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint interface modify /Game/Blueprints/BP_Player add BPI_Usable"],
    ),
    "blueprint compile": _entry(
        "blueprint compile",
        "Canonical Blueprint compile command.",
        layer="bridge",
        category="blueprint",
        requires_bridge=True,
        requires_editor=True,
        examples=["soft-ue-cli blueprint compile /Game/Blueprints/BP_Player"],
    ),
}


_CLIENT_SIDE_COMMANDS = {
    "check-setup",
    "commands",
    "config",
    "diff-uasset",
    "inspect-uasset",
    "request-feature",
    "report-bug",
    "setup",
    "skills",
    "status",
    "submit-testimonial",
    "wait-for-ready",
}


_SUPPORT_COMMANDS = {"report-bug", "request-feature", "submit-testimonial"}
_PIE_COMMANDS = {
    "capture-pie-screenshot",
    "exec-console-command",
    "inspect-pawn-possession",
    "inspect-runtime-widgets",
    "pie-session",
    "pie-tick",
    "trigger-input",
    "verify-umg-workflow",
}
_EDITOR_OPTIONAL_COMMANDS = {
    "build-and-relaunch",
    "reload-bridge-module",
    "trigger-live-coding",
}


def _plugin_requirements_for_command(name: str) -> list[dict[str, Any]]:
    if name.endswith("-co") or "-co-" in name or name.startswith("add-co-") or name.startswith("inspect-mutable"):
        return [_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")]
    if "customizable-object" in name:
        return [_plugin("Mutable", "CustomizableObjectEditor", "Required for Mutable/CustomizableObject graph operations.")]
    if "statetree" in name:
        return [_plugin("StateTree", "StateTreeEditorModule", "Required for StateTree asset editing and inspection.")]
    if name.startswith("rewind-"):
        return [_plugin("Animation Insights", "GameplayInsights", "Required for Rewind Debugger recording and inspection.")]
    if name == "trigger-input":
        return [_plugin("Enhanced Input", "EnhancedInput", "Required only for Enhanced Input action injection.")]
    return []


def _infer_category(name: str) -> str:
    if name.startswith("capture"):
        return "capture"
    if name in _SUPPORT_COMMANDS:
        return "support"
    if name.startswith("config"):
        return "config"
    if "blueprint" in name or "graph" in name:
        return "blueprint"
    if name.startswith("anim") or "-anim-" in name or "sync-marker" in name or name.startswith("rewind-"):
        return "animation"
    if "asset" in name or "uasset" in name:
        return "asset"
    if "umg" in name or "widget" in name:
        return "umg"
    if name.startswith("build") or "live-coding" in name:
        return "build"
    if name.startswith("inspect") or name.startswith("query") or name.startswith("get-"):
        return "inspect"
    return "workflow"


def _default_entry(name: str, summary: str = "") -> dict[str, Any]:
    required_plugins = _plugin_requirements_for_command(name)
    is_client_side = name in _CLIENT_SIDE_COMMANDS
    requires_bridge = not is_client_side
    editor_plugin_names = {"Mutable", "StateTree", "Animation Insights"}
    requires_editor_plugin = any(req["name"] in editor_plugin_names for req in required_plugins)
    requires_editor = requires_bridge and (
        name in _EDITOR_OPTIONAL_COMMANDS
        or _infer_category(name) in {"asset", "blueprint", "umg", "animation", "build"}
        or requires_editor_plugin
    )
    requires_pie = name in _PIE_COMMANDS
    layer = "support" if name in _SUPPORT_COMMANDS else "offline" if is_client_side else "bridge"
    return _entry(
        name,
        summary or name,
        layer=layer,
        category=_infer_category(name),
        requires_bridge=requires_bridge,
        requires_editor=requires_editor,
        requires_pie=requires_pie,
        required_plugins=required_plugins,
    )


def _argparse_command_summaries() -> dict[str, str]:
    try:
        from .__main__ import build_parser
    except Exception:
        return {}

    parser = build_parser()
    for action in parser._actions:
        if isinstance(action, getattr(__import__("argparse"), "_SubParsersAction")):
            return {
                choice.dest: choice.help or choice.dest
                for choice in action._choices_actions
            }
    return {}


def _build_catalog() -> dict[str, dict[str, Any]]:
    catalog: dict[str, dict[str, Any]] = {}
    for name, summary in _argparse_command_summaries().items():
        catalog[name] = _default_entry(name, summary)
    for legacy_name, canonical_command in CANONICAL_COMMAND_FOR_LEGACY.items():
        if legacy_name not in catalog:
            continue
        canonical_root = canonical_command.split()[0]
        if canonical_root == "mutable":
            catalog[legacy_name]["category"] = "mutable"
        elif canonical_root == "statetree":
            catalog[legacy_name]["category"] = "statetree"
        elif canonical_root == "anim":
            catalog[legacy_name]["category"] = "animation"
        elif canonical_root in {"asset", "blueprint"}:
            catalog[legacy_name]["category"] = canonical_root
            if not canonical_command.startswith(("asset inspect-file", "asset diff-file")):
                catalog[legacy_name]["requires_editor"] = True
        catalog[legacy_name]["layer"] = "compatibility"
        catalog[legacy_name]["status"] = "compatibility"
        catalog[legacy_name]["canonical_command"] = canonical_command
    for name, entry in _EXPLICIT_ENTRIES.items():
        catalog[name] = deepcopy(entry)
    return catalog


def iter_command_metadata() -> list[dict[str, Any]]:
    """Return all command metadata entries sorted by command name."""
    return [deepcopy(entry) for _, entry in sorted(_build_catalog().items())]


def get_command_metadata(name: str) -> dict[str, Any]:
    """Return metadata for one command or command family."""
    catalog = _build_catalog()
    if name not in catalog:
        raise KeyError(f"unknown command metadata: {name}")
    return deepcopy(catalog[name])


def command_metadata_as_json(*, probe: bool = False) -> dict[str, Any]:
    """Return command metadata payload for JSON discovery."""
    commands = iter_command_metadata()
    if probe:
        for entry in commands:
            entry["available"] = "unknown"
            entry["availability_note"] = "Bridge probing is not enabled for this command metadata entry yet."
    return {
        "schema": SCHEMA,
        "commands": commands,
    }


def filter_command_metadata(
    *,
    category: str | None = None,
    requires_bridge: bool | None = None,
    compatibility: bool = False,
    plugin: str | None = None,
) -> list[dict[str, Any]]:
    """Filter command metadata entries for human-readable listings."""
    entries = iter_command_metadata()
    if category:
        entries = [entry for entry in entries if entry["category"] == category or category in entry["name"].split()]
    if requires_bridge is not None:
        entries = [entry for entry in entries if entry["requires_bridge"] is requires_bridge]
    if compatibility:
        entries = [entry for entry in entries if entry["status"] in {"compatibility", "deprecated"}]
    if plugin:
        plugin_lower = plugin.lower()
        entries = [
            entry
            for entry in entries
            if any(req["name"].lower() == plugin_lower for req in entry["required_plugins"])
        ]
    return entries
