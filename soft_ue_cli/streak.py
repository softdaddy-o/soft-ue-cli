"""Daily usage streak tracking for testimonial nudges."""

from __future__ import annotations

import json
from datetime import date, timedelta
from pathlib import Path

STREAK_FILE = Path.home() / ".soft-ue-cli" / "streak.json"

_EMPTY_DATA = {"dates": [], "testimonial_nudged": False, "tool_usage": {}}


def _load_data() -> dict:
    """Load streak data from disk, returning empty defaults if missing/corrupt."""
    try:
        return json.loads(STREAK_FILE.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return dict(_EMPTY_DATA)


def _save_data(data: dict) -> None:
    """Write streak data to disk, creating parent dirs if needed."""
    STREAK_FILE.parent.mkdir(parents=True, exist_ok=True)
    STREAK_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")


def record_success(tool_name: str) -> None:
    """Record a successful tool call: update date set, tool counts, trim old dates."""
    data = _load_data()
    today_str = str(date.today())
    cutoff = str(date.today() - timedelta(days=30))

    # Check if streak was broken before this call
    old_streak = _compute_streak(data.get("dates", []))

    # Add today
    dates = sorted(set(data.get("dates", [])) | {today_str})
    # Trim dates older than 30 days
    dates = [d for d in dates if d >= cutoff]
    data["dates"] = dates

    # Update tool usage
    usage = data.get("tool_usage", {})
    usage[tool_name] = usage.get(tool_name, 0) + 1
    data["tool_usage"] = usage

    # If streak was broken (was 0 before adding today), reset nudge flag and tool usage
    if old_streak == 0:
        data["testimonial_nudged"] = False
        data["tool_usage"] = {tool_name: 1}

    _save_data(data)


def get_streak() -> int:
    """Count consecutive days ending at today."""
    data = _load_data()
    return _compute_streak(data.get("dates", []))


def _compute_streak(dates: list[str]) -> int:
    """Count consecutive days ending at today from a list of date strings."""
    if not dates:
        return 0
    today = date.today()
    date_set = set(dates)
    if str(today) not in date_set:
        return 0
    streak = 0
    for i in range(len(date_set) + 1):
        if str(today - timedelta(days=i)) in date_set:
            streak += 1
        else:
            break
    return streak


def should_nudge_testimonial() -> bool:
    """True when streak >= 3 and user hasn't been nudged this cycle."""
    data = _load_data()
    if data.get("testimonial_nudged", False):
        return False
    return _compute_streak(data.get("dates", [])) >= 3


def mark_nudged() -> None:
    """Mark that the testimonial nudge has been shown this streak cycle."""
    data = _load_data()
    data["testimonial_nudged"] = True
    _save_data(data)
