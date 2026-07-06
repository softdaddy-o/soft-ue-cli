"""Offline diagnostics and report helpers for soft-ue-cli."""

from __future__ import annotations

import csv
import json
import re
from pathlib import Path
from typing import Any, Iterable


def _json_loads_maybe(text: str) -> Any | None:
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _finding(kind: str, line: str, hint: str) -> dict[str, str]:
    return {"kind": kind, "line": line.strip(), "hint": hint}


def summarize_build_log(text: str) -> dict[str, Any]:
    """Classify common Unreal build failures from a log string."""

    findings: list[dict[str, str]] = []
    patterns: list[tuple[str, re.Pattern[str], str]] = [
        (
            "compiler_error",
            re.compile(r"\b(?:fatal )?error C\d{4}\b", re.IGNORECASE),
            "Open the referenced C++ file and fix the compiler diagnostic before retrying the build.",
        ),
        (
            "linker_error",
            re.compile(r"\b(?:error )?LNK\d{4}\b", re.IGNORECASE),
            "Check missing module dependencies, symbol definitions, and Build.cs linkage.",
        ),
        (
            "unreal_header_tool",
            re.compile(r"\b(?:UHT|UnrealHeaderTool)\b|generated\.h", re.IGNORECASE),
            "Check reflected UCLASS/USTRUCT/UFUNCTION declarations and include ordering.",
        ),
        (
            "msbuild_error",
            re.compile(r"\berror MSB\d{4}\b", re.IGNORECASE),
            "Inspect the surrounding MSBuild lines for the failed target and tool invocation.",
        ),
        (
            "toolchain_error",
            re.compile(r"\b(?:SDK|toolchain|Visual Studio|cl\.exe|Windows Kits)\b", re.IGNORECASE),
            "Verify the configured compiler, Windows SDK, and Unreal toolchain installation.",
        ),
        (
            "missing_module",
            re.compile(r"\b(?:missing modules|could not be compiled|Unable to instantiate module)\b", re.IGNORECASE),
            "Regenerate project files or run a full editor-target build to restore missing modules.",
        ),
    ]

    for line in text.splitlines():
        for kind, pattern, hint in patterns:
            if pattern.search(line):
                findings.append(_finding(kind, line, hint))
                break

    seen_kinds = {finding["kind"] for finding in findings}
    next_steps: list[str] = []
    if "unreal_header_tool" in seen_kinds:
        next_steps.append("Run a full editor build after reflected header changes; Live Coding is not enough for UHT failures.")
    if "linker_error" in seen_kinds:
        next_steps.append("Check Build.cs dependencies and any functions declared but not defined.")
    if "compiler_error" in seen_kinds:
        next_steps.append("Start with the first compiler error; later Unreal build errors are often cascading.")
    if not next_steps:
        next_steps.append("Search for the first occurrence of 'error' or 'fatal' and inspect the preceding command block.")

    return {
        "schema": "soft_ue.diagnose.build_log.v1",
        "success": True,
        "finding_count": len(findings),
        "findings": findings,
        "next_steps": next_steps,
    }


_P4_OPENED_RE = re.compile(
    r"^(?P<depot>//.+?)#(?P<rev>\d+)\s+-\s+(?P<action>\w+)"
    r"(?:\s+(?:change\s+(?P<change>[^\s]+)|(?P<default_change>default)\s+change))?\s+\((?P<type>[^)]+)\)"
)


def _p4_risk_for_path(path: str, file_type: str) -> str | None:
    lower = path.lower()
    if lower.endswith((".uasset", ".umap")) or "binary" in file_type.lower():
        return "binary_asset"
    if lower.endswith(".generated.h") or "/intermediate/" in lower or "\\intermediate\\" in lower:
        return "generated_file"
    if any(token in lower for token in ("/saved/", "\\saved\\", "/binaries/", "\\binaries\\")):
        return "generated_or_build_output"
    if lower.endswith((".dll", ".pdb", ".exe", ".obj", ".lib")):
        return "compiled_binary"
    return None


def summarize_p4_opened(text: str) -> dict[str, Any]:
    """Summarize `p4 opened` output and flag risky Unreal changelist entries."""

    opened: list[dict[str, Any]] = []
    risky: list[dict[str, Any]] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        match = _P4_OPENED_RE.match(line)
        if match:
            item = {
                "depot_path": match.group("depot"),
                "rev": int(match.group("rev")),
                "action": match.group("action"),
                "change": match.group("change") or match.group("default_change") or "default",
                "type": match.group("type"),
            }
        else:
            item = {"depot_path": line, "rev": None, "action": "unknown", "change": "unknown", "type": "unknown"}
        opened.append(item)
        risk = _p4_risk_for_path(item["depot_path"], item["type"])
        if risk:
            risky_item = dict(item)
            risky_item["risk"] = risk
            risky.append(risky_item)

    return {
        "schema": "soft_ue.diagnose.p4.v1",
        "success": True,
        "opened_count": len(opened),
        "opened_files": opened,
        "risky_count": len(risky),
        "risky_files": risky,
        "shelve_note": "Review binary/generated entries explicitly before shelving or asking for review.",
    }


def make_issue_investigation_plan(source: str, payload_text: str) -> dict[str, Any]:
    """Turn Jira, Sentry, or plain issue text into local Unreal investigation steps."""

    source = source.lower()
    payload = _json_loads_maybe(payload_text)
    title = ""
    issue_key = ""
    description = ""

    if source == "jira" and isinstance(payload, dict):
        fields = payload.get("fields") if isinstance(payload.get("fields"), dict) else {}
        issue_key = str(payload.get("key") or "")
        title = str(fields.get("summary") or payload.get("summary") or issue_key or "Jira issue")
        description = str(fields.get("description") or payload.get("description") or "")
    elif source == "sentry" and isinstance(payload, dict):
        title = str(payload.get("title") or payload.get("culprit") or "Sentry event")
        issue_key = str(payload.get("id") or payload.get("eventID") or "")
        description = " ".join(str(payload.get(key) or "") for key in ("culprit", "platform", "environment")).strip()
    else:
        lines = [line.strip() for line in payload_text.splitlines() if line.strip()]
        title = lines[0] if lines else "Issue"
        description = "\n".join(lines[1:])

    suggested_commands = [
        "soft-ue-cli status --json",
        "soft-ue-cli get-logs --lines 200 --filter error",
        "soft-ue-cli commands --json --category inspect",
    ]
    investigation_plan = [
        "Extract the asset, map, actor, and repro terms from the issue text.",
        "Check bridge health and recent logs before editing assets.",
        "Pick the narrowest inspector command for the affected Blueprint, AnimBP, widget, or level.",
        "Capture a before/after probe result that can be attached back to the issue.",
    ]

    return {
        "schema": "soft_ue.diagnose.issue.v1",
        "success": True,
        "source": source,
        "issue_key": issue_key,
        "title": title,
        "description_excerpt": description[:500],
        "suggested_commands": suggested_commands,
        "investigation_plan": investigation_plan,
    }


def _validate_csv(path: Path, problems: list[dict[str, Any]]) -> None:
    with path.open("r", encoding="utf-8", errors="replace", newline="") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            problems.append({"path": str(path), "kind": "empty_datatable", "message": "CSV has no header row"})
            return
        row_name_column = "Name" if "Name" in reader.fieldnames else reader.fieldnames[0]
        seen: set[str] = set()
        reference_columns = [
            column
            for column in reader.fieldnames
            if any(token in column.lower() for token in ("asset", "path", "tag", "class", "reference"))
        ]
        for row_index, row in enumerate(reader, start=2):
            row_name = (row.get(row_name_column) or "").strip()
            if row_name:
                if row_name in seen:
                    problems.append({
                        "path": str(path),
                        "kind": "duplicate_row_name",
                        "row": row_index,
                        "column": row_name_column,
                        "value": row_name,
                    })
                seen.add(row_name)
            for column in reference_columns:
                if not (row.get(column) or "").strip():
                    problems.append({
                        "path": str(path),
                        "kind": "empty_reference",
                        "row": row_index,
                        "column": column,
                    })


def _validate_json(path: Path, problems: list[dict[str, Any]]) -> None:
    text = _read_text(path)
    try:
        json.loads(text)
    except json.JSONDecodeError as exc:
        problems.append({"path": str(path), "kind": "malformed_json", "line": exc.lineno, "message": exc.msg})


def _validate_ini(path: Path, problems: list[dict[str, Any]]) -> None:
    section = ""
    keys_by_section: dict[str, set[str]] = {}
    for line_number, raw_line in enumerate(_read_text(path).splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            keys_by_section.setdefault(section, set())
            continue
        if "=" not in line:
            continue
        key = line.split("=", 1)[0].strip()
        seen = keys_by_section.setdefault(section, set())
        if key in seen:
            problems.append({"path": str(path), "kind": "duplicate_config_key", "line": line_number, "section": section, "key": key})
        seen.add(key)


def validate_data_files(paths: Iterable[Path | str]) -> dict[str, Any]:
    problems: list[dict[str, Any]] = []
    checked: list[str] = []
    for raw_path in paths:
        path = Path(raw_path)
        checked.append(str(path))
        if not path.exists():
            problems.append({"path": str(path), "kind": "missing_file", "message": "File does not exist"})
            continue
        suffix = path.suffix.lower()
        if suffix == ".csv":
            _validate_csv(path, problems)
        elif suffix == ".json":
            _validate_json(path, problems)
        elif suffix == ".ini":
            _validate_ini(path, problems)
        else:
            problems.append({"path": str(path), "kind": "unsupported_file", "message": "No validator registered for this file type"})

    return {
        "schema": "soft_ue.diagnose.data.v1",
        "success": True,
        "checked": checked,
        "problem_count": len(problems),
        "problems": problems,
    }


def build_handoff_report(
    *,
    title: str,
    summary: str = "",
    evidence: Iterable[str] = (),
    next_steps: Iterable[str] = (),
) -> dict[str, Any]:
    evidence_list = [item for item in evidence if item]
    next_step_list = [item for item in next_steps if item]

    lines = [f"# {title}", ""]
    if summary:
        lines.extend(["## Summary", summary, ""])
    if evidence_list:
        lines.append("## Evidence")
        lines.extend(f"- {item}" for item in evidence_list)
        lines.append("")
    if next_step_list:
        lines.append("## Next Steps")
        lines.extend(f"- {item}" for item in next_step_list)
        lines.append("")

    markdown = "\n".join(lines).rstrip() + "\n"
    return {
        "schema": "soft_ue.diagnose.handoff.v1",
        "success": True,
        "title": title,
        "markdown": markdown,
    }
