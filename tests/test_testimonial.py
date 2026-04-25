"""Tests for submit-testimonial command."""

import json
from argparse import Namespace
from unittest.mock import patch, MagicMock

import pytest

from soft_ue_cli.testimonial import cmd_submit_testimonial, _build_discussion_body


def test_build_discussion_body_full():
    body = _build_discussion_body(
        message="Great tool!",
        agent_name="Claude Code",
        rating=5,
        cli_version="1.9.0",
        streak=7,
        top_tools=["spawn-actor", "get-actors", "set-property"],
    )
    assert "> Great tool!" in body
    assert "Claude Code" in body
    assert "★★★★★" in body
    assert "1.9.0" in body
    assert "7 days" in body
    assert "spawn-actor" in body


def test_build_discussion_body_minimal():
    body = _build_discussion_body(
        message="Nice",
        agent_name=None,
        rating=None,
        cli_version="1.9.0",
        streak=3,
        top_tools=[],
    )
    assert "> Nice" in body
    assert "Anonymous" in body
    assert "Rating" not in body


def test_build_discussion_body_rating_stars():
    body = _build_discussion_body(
        message="ok", agent_name=None, rating=3,
        cli_version="1.0.0", streak=3, top_tools=[],
    )
    assert "★★★☆☆" in body


@patch("soft_ue_cli.testimonial._get_metadata")
@patch("soft_ue_cli.testimonial.create_discussion")
def test_cmd_submit_testimonial_with_yes_flag(mock_create, mock_meta):
    mock_meta.return_value = ("1.9.0", 5, ["spawn-actor"])
    mock_create.return_value = {
        "discussion_number": 1,
        "url": "https://github.com/softdaddy-o/soft-ue-cli/discussions/1",
    }
    args = Namespace(
        message="Great tool!",
        agent_name="Claude",
        rating=5,
        yes=True,
    )
    # Should not raise — --yes skips consent
    cmd_submit_testimonial(args)
    mock_create.assert_called_once()


@patch("soft_ue_cli.testimonial._get_metadata")
@patch("sys.stdin")
def test_cmd_submit_testimonial_non_interactive_no_yes(mock_stdin, mock_meta):
    mock_stdin.isatty.return_value = False
    mock_meta.return_value = ("1.9.0", 3, [])
    args = Namespace(message="Great!", agent_name=None, rating=None, yes=False)
    with pytest.raises(SystemExit):
        cmd_submit_testimonial(args)
