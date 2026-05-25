"""Canonical command-family aliases for legacy flat CLI commands."""

from __future__ import annotations


COMMAND_ALIAS_PREFIXES: dict[tuple[str, ...], str] = {
    ("mutable", "inspect", "graph"): "inspect-customizable-object-graph",
    ("mutable", "inspect", "parameters"): "inspect-mutable-parameters",
    ("mutable", "inspect", "diagnostics"): "inspect-mutable-diagnostics",
    ("mutable", "graph", "add-node"): "add-co-node",
    ("mutable", "graph", "add-parameter"): "add-co-parameter",
    ("mutable", "graph", "add-mesh-option"): "add-co-mesh-option",
    ("mutable", "graph", "set-base-mesh"): "set-co-base-mesh",
    ("mutable", "graph", "add-group-child"): "add-co-group-child",
    ("mutable", "graph", "set-node-property"): "set-co-node-property",
    ("mutable", "graph", "connect-pins"): "connect-co-pins",
    ("mutable", "graph", "regenerate-node-pins"): "regenerate-co-node-pins",
    ("mutable", "graph", "remove-node"): "remove-co-node",
    ("mutable", "graph", "create-from-spec"): "create-co-from-spec",
    ("mutable", "graph", "wire-slot-from-table"): "wire-customizable-object-slot-from-table",
    ("mutable", "compile"): "compile-co",
    ("statetree", "inspect"): "query-statetree",
    ("statetree", "state", "add"): "add-statetree-state",
    ("statetree", "state", "remove"): "remove-statetree-state",
    ("statetree", "task", "add"): "add-statetree-task",
    ("statetree", "transition", "add"): "add-statetree-transition",
    ("anim", "instance", "inspect"): "inspect-anim-instance",
    ("anim", "sync-marker", "inspect"): "inspect-sync-markers",
    ("anim", "sync-marker", "compare"): "compare-sync-markers",
    ("anim", "sync-marker", "add"): "add-sync-marker",
    ("anim", "sync-marker", "remove"): "remove-sync-marker",
    ("anim", "state-machine", "add"): "add-anim-state-machine",
    ("anim", "state", "add"): "add-anim-state",
    ("anim", "transition", "add"): "add-anim-transition",
    ("anim", "rewind", "start"): "rewind-start",
    ("anim", "rewind", "stop"): "rewind-stop",
    ("anim", "rewind", "status"): "rewind-status",
    ("anim", "rewind", "list-tracks"): "rewind-list-tracks",
    ("anim", "rewind", "tracks"): "rewind-list-tracks",
    ("anim", "rewind", "overview"): "rewind-overview",
    ("anim", "rewind", "snapshot"): "rewind-snapshot",
    ("anim", "rewind", "save"): "rewind-save",
    ("asset", "query"): "query-asset",
    ("asset", "delete"): "delete-asset",
    ("asset", "release-lock"): "release-asset-lock",
    ("asset", "diff"): "get-asset-diff",
    ("asset", "preview"): "get-asset-preview",
    ("asset", "open"): "open-asset",
    ("asset", "set-property"): "set-asset-property",
    ("asset", "inspect-file"): "inspect-uasset",
    ("asset", "diff-file"): "diff-uasset",
    ("asset", "save"): "save-asset",
    ("asset", "create"): "create-asset",
    ("blueprint", "inspect"): "query-blueprint",
    ("blueprint", "graph", "inspect"): "query-blueprint-graph",
    ("blueprint", "compile"): "compile-blueprint",
    ("blueprint", "node", "add"): "add-graph-node",
    ("blueprint", "node", "remove"): "remove-graph-node",
    ("blueprint", "node", "position"): "set-node-position",
    ("blueprint", "node", "property"): "set-node-property",
    ("blueprint", "pin", "connect"): "connect-graph-pins",
    ("blueprint", "pin", "disconnect"): "disconnect-graph-pin",
    ("blueprint", "interface", "modify"): "modify-interface",
}


CANONICAL_COMMAND_FOR_LEGACY: dict[str, str] = {}
for _prefix, _legacy_command in COMMAND_ALIAS_PREFIXES.items():
    CANONICAL_COMMAND_FOR_LEGACY.setdefault(_legacy_command, " ".join(_prefix))


_ALIASES_BY_LENGTH = sorted(COMMAND_ALIAS_PREFIXES.items(), key=lambda item: len(item[0]), reverse=True)
_ROOT_OPTIONS_WITH_VALUES = {"--server", "--timeout"}


def _command_start_index(args: list[str]) -> int | None:
    index = 0
    while index < len(args):
        current = args[index]
        if current in {"-h", "--help"}:
            return None
        if current in _ROOT_OPTIONS_WITH_VALUES:
            index += 2
            continue
        if any(current.startswith(f"{option}=") for option in _ROOT_OPTIONS_WITH_VALUES):
            index += 1
            continue
        return index
    return None


def normalize_command_aliases(args: list[str] | None) -> list[str] | None:
    """Rewrite canonical nested command prefixes to their legacy flat parser command."""
    if args is None:
        return None

    normalized = list(args)
    command_index = _command_start_index(normalized)
    if command_index is None:
        return normalized

    command_tail = normalized[command_index:]
    for prefix, legacy_command in _ALIASES_BY_LENGTH:
        if tuple(command_tail[: len(prefix)]) == prefix:
            return normalized[:command_index] + [legacy_command] + command_tail[len(prefix):]
    return normalized
