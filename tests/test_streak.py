"""Tests for daily streak tracking."""

import json
from datetime import date, timedelta
from pathlib import Path
from unittest.mock import patch

from soft_ue_cli.streak import (
    _load_data,
    _save_data,
    get_streak,
    record_success,
    should_nudge_testimonial,
    mark_nudged,
    STREAK_FILE,
)


def _write_streak(tmp_path: Path, data: dict) -> Path:
    """Write streak data to a temp file and return its path."""
    f = tmp_path / "streak.json"
    f.write_text(json.dumps(data))
    return f


def test_load_data_missing_file(tmp_path):
    f = tmp_path / "streak.json"
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        data = _load_data()
    assert data == {"dates": [], "testimonial_nudged": False, "tool_usage": {}}


def test_record_success_adds_today(tmp_path):
    f = _write_streak(tmp_path, {"dates": [], "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        record_success("spawn-actor")
    data = json.loads(f.read_text())
    assert str(date.today()) in data["dates"]
    assert data["tool_usage"]["spawn-actor"] == 1


def test_record_success_increments_tool_count(tmp_path):
    today = str(date.today())
    f = _write_streak(tmp_path, {
        "dates": [today],
        "testimonial_nudged": False,
        "tool_usage": {"spawn-actor": 3},
    })
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        record_success("spawn-actor")
    data = json.loads(f.read_text())
    assert data["tool_usage"]["spawn-actor"] == 4


def test_record_success_no_duplicate_dates(tmp_path):
    today = str(date.today())
    f = _write_streak(tmp_path, {"dates": [today], "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        record_success("spawn-actor")
    data = json.loads(f.read_text())
    assert data["dates"].count(today) == 1


def test_get_streak_consecutive_days(tmp_path):
    today = date.today()
    dates = [str(today - timedelta(days=i)) for i in range(4)]
    f = _write_streak(tmp_path, {"dates": dates, "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert get_streak() == 4


def test_get_streak_gap_resets(tmp_path):
    today = date.today()
    dates = [
        str(today),
        str(today - timedelta(days=1)),
        str(today - timedelta(days=3)),
    ]
    f = _write_streak(tmp_path, {"dates": dates, "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert get_streak() == 2


def test_get_streak_no_today(tmp_path):
    yesterday = str(date.today() - timedelta(days=1))
    f = _write_streak(tmp_path, {"dates": [yesterday], "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert get_streak() == 0


def test_should_nudge_true_at_3_days(tmp_path):
    today = date.today()
    dates = [str(today - timedelta(days=i)) for i in range(3)]
    f = _write_streak(tmp_path, {"dates": dates, "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert should_nudge_testimonial() is True


def test_should_nudge_false_if_already_nudged(tmp_path):
    today = date.today()
    dates = [str(today - timedelta(days=i)) for i in range(3)]
    f = _write_streak(tmp_path, {"dates": dates, "testimonial_nudged": True, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert should_nudge_testimonial() is False


def test_should_nudge_false_under_3_days(tmp_path):
    today = date.today()
    dates = [str(today), str(today - timedelta(days=1))]
    f = _write_streak(tmp_path, {"dates": dates, "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        assert should_nudge_testimonial() is False


def test_mark_nudged(tmp_path):
    today = str(date.today())
    f = _write_streak(tmp_path, {"dates": [today], "testimonial_nudged": False, "tool_usage": {}})
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        mark_nudged()
    data = json.loads(f.read_text())
    assert data["testimonial_nudged"] is True


def test_trims_old_dates(tmp_path):
    today = date.today()
    old_date = str(today - timedelta(days=45))
    recent_date = str(today)
    f = _write_streak(tmp_path, {
        "dates": [old_date, recent_date],
        "testimonial_nudged": False,
        "tool_usage": {},
    })
    with patch("soft_ue_cli.streak.STREAK_FILE", f):
        record_success("get-actors")
    data = json.loads(f.read_text())
    assert old_date not in data["dates"]
    assert recent_date in data["dates"]
