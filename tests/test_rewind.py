"""Tests for rewind-* CLI commands."""

from unittest.mock import patch, MagicMock

import pytest

from soft_ue_cli.__main__ import (
    cmd_rewind_start,
    cmd_rewind_stop,
    cmd_rewind_status,
    cmd_rewind_list_tracks,
    cmd_rewind_overview,
    cmd_rewind_snapshot,
    cmd_rewind_save,
)


def _make_args(**kwargs):
    """Create a mock argparse.Namespace with given attributes."""
    args = MagicMock()
    # Set defaults for all rewind args
    defaults = {
        "channels": None,
        "actors": None,
        "file": None,
        "load": None,
        "actor_tag": None,
        "tracks": None,
        "time": None,
        "frame": None,
        "include": None,
    }
    defaults.update(kwargs)
    for k, v in defaults.items():
        setattr(args, k, v)
    return args


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindStart:
    def test_start_no_args(self, mock_run, mock_print):
        mock_run.return_value = {"status": "recording"}
        cmd_rewind_start(_make_args())
        mock_run.assert_called_once_with("rewind-start", {})

    def test_start_with_channels(self, mock_run, mock_print):
        mock_run.return_value = {"status": "recording"}
        cmd_rewind_start(_make_args(channels="skeletal-mesh,montage"))
        args = mock_run.call_args[0][1]
        assert args["channels"] == ["skeletal-mesh", "montage"]

    def test_start_with_actors(self, mock_run, mock_print):
        mock_run.return_value = {"status": "recording"}
        cmd_rewind_start(_make_args(actors="Player,Enemy01"))
        args = mock_run.call_args[0][1]
        assert args["actors"] == ["Player", "Enemy01"]

    def test_start_with_file(self, mock_run, mock_print):
        mock_run.return_value = {"status": "recording"}
        cmd_rewind_start(_make_args(file="/tmp/test.utrace"))
        args = mock_run.call_args[0][1]
        assert args["file"] == "/tmp/test.utrace"

    def test_start_with_load(self, mock_run, mock_print):
        mock_run.return_value = {"status": "loaded"}
        cmd_rewind_start(_make_args(load="/tmp/existing.utrace"))
        args = mock_run.call_args[0][1]
        assert args["load"] == "/tmp/existing.utrace"
        assert "channels" not in args


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindStop:
    def test_stop(self, mock_run, mock_print):
        mock_run.return_value = {"status": "stopped", "duration": 5.0}
        cmd_rewind_stop(_make_args())
        mock_run.assert_called_once_with("rewind-stop", {})


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindStatus:
    def test_status(self, mock_run, mock_print):
        mock_run.return_value = {"recording": True}
        cmd_rewind_status(_make_args())
        mock_run.assert_called_once_with("rewind-status", {})


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindListTracks:
    def test_list_all(self, mock_run, mock_print):
        mock_run.return_value = {"actors": []}
        cmd_rewind_list_tracks(_make_args())
        mock_run.assert_called_once_with("rewind-list-tracks", {})

    def test_list_filtered(self, mock_run, mock_print):
        mock_run.return_value = {"actors": []}
        cmd_rewind_list_tracks(_make_args(actor_tag="Player"))
        args = mock_run.call_args[0][1]
        assert args["actor_tag"] == "Player"


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindOverview:
    def test_overview_required_actor(self, mock_run, mock_print):
        mock_run.return_value = {"actor_tag": "Player", "tracks": {}}
        cmd_rewind_overview(_make_args(actor_tag="Player"))
        args = mock_run.call_args[0][1]
        assert args["actor_tag"] == "Player"

    def test_overview_with_track_filter(self, mock_run, mock_print):
        mock_run.return_value = {"actor_tag": "Player", "tracks": {}}
        cmd_rewind_overview(_make_args(actor_tag="Player", tracks="state_machines,montages"))
        args = mock_run.call_args[0][1]
        assert args["tracks"] == ["state_machines", "montages"]


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindSnapshot:
    def test_snapshot_by_time(self, mock_run, mock_print):
        mock_run.return_value = {"time": 3.45}
        cmd_rewind_snapshot(_make_args(actor_tag="Player", time=3.45))
        args = mock_run.call_args[0][1]
        assert args["time"] == 3.45
        assert "frame" not in args

    def test_snapshot_by_frame(self, mock_run, mock_print):
        mock_run.return_value = {"frame": 207}
        cmd_rewind_snapshot(_make_args(actor_tag="Player", frame=207))
        args = mock_run.call_args[0][1]
        assert args["frame"] == 207
        assert "time" not in args

    def test_snapshot_with_include(self, mock_run, mock_print):
        mock_run.return_value = {"time": 1.0}
        cmd_rewind_snapshot(
            _make_args(actor_tag="Player", time=1.0, include="state-machines,montages")
        )
        args = mock_run.call_args[0][1]
        assert args["include"] == ["state-machines", "montages"]


@patch("soft_ue_cli.__main__._print_json")
@patch("soft_ue_cli.__main__._run_tool")
class TestRewindSave:
    def test_save_default(self, mock_run, mock_print):
        mock_run.return_value = {"file": "/tmp/auto.utrace"}
        cmd_rewind_save(_make_args())
        mock_run.assert_called_once_with("rewind-save", {})

    def test_save_custom_path(self, mock_run, mock_print):
        mock_run.return_value = {"file": "/custom/path.utrace"}
        cmd_rewind_save(_make_args(file="/custom/path.utrace"))
        args = mock_run.call_args[0][1]
        assert args["file"] == "/custom/path.utrace"
