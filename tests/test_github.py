"""Tests for cli/soft_ue_cli/github.py ??GitHub auth and issue creation."""

from __future__ import annotations

import subprocess
from unittest.mock import patch

import httpx
import pytest

from soft_ue_cli.__main__ import build_parser
from soft_ue_cli.github import _resolve_token, create_issue

_PATCH_SUBPROCESS = "soft_ue_cli.github.subprocess.run"
_PATCH_HTTPX_POST = "soft_ue_cli.github.httpx.post"


def _mock_issue_response(number: int) -> httpx.Response:
    """Build a mock 201 response for a created GitHub issue."""
    return httpx.Response(
        201,
        json={
            "number": number,
            "html_url": f"https://github.com/softdaddy-o/soft-ue-cli/issues/{number}",
        },
        request=httpx.Request("POST", "https://api.github.com"),
    )


def _mock_error_response(status: int, message: str) -> httpx.Response:
    """Build a mock error response from the GitHub API."""
    return httpx.Response(
        status,
        json={"message": message},
        request=httpx.Request("POST", "https://api.github.com"),
    )


# -- _resolve_token ------------------------------------------------------------


def test_resolve_token_env_var(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_envtoken123")
    assert _resolve_token() == "ghp_envtoken123"


def test_resolve_token_gh_cli(monkeypatch):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS) as mock_run:
        mock_run.return_value = subprocess.CompletedProcess(
            args=["gh", "auth", "token"], returncode=0, stdout="ghp_ghcli456\n"
        )
        assert _resolve_token() == "ghp_ghcli456"


def test_resolve_token_strips_whitespace(monkeypatch):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS) as mock_run:
        mock_run.return_value = subprocess.CompletedProcess(
            args=["gh", "auth", "token"], returncode=0, stdout="  ghp_spaces  \n"
        )
        assert _resolve_token() == "ghp_spaces"


def test_resolve_token_gh_not_installed(monkeypatch):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS, side_effect=FileNotFoundError):
        with pytest.raises(SystemExit) as exc:
            _resolve_token()
        assert exc.value.code == 1


def test_resolve_token_gh_cli_fails(monkeypatch):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS, side_effect=subprocess.CalledProcessError(1, "gh")):
        with pytest.raises(SystemExit) as exc:
            _resolve_token()
        assert exc.value.code == 1


def test_resolve_token_gh_cli_timeout(monkeypatch):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS, side_effect=subprocess.TimeoutExpired(["gh", "auth", "token"], 10)):
        with pytest.raises(SystemExit) as exc:
            _resolve_token()
        assert exc.value.code == 1


def test_resolve_token_gh_returns_empty(monkeypatch):
    """gh auth token succeeds but returns empty output."""
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    with patch(_PATCH_SUBPROCESS) as mock_run:
        mock_run.return_value = subprocess.CompletedProcess(
            args=["gh", "auth", "token"], returncode=0, stdout="\n"
        )
        with pytest.raises(SystemExit) as exc:
            _resolve_token()
        assert exc.value.code == 1


# -- create_issue --------------------------------------------------------------


def test_create_issue_success(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(42)):
        result = create_issue("Bug title", "Bug body", ["bug"])
    assert result == {"issue_number": 42, "url": "https://github.com/softdaddy-o/soft-ue-cli/issues/42"}


def test_create_issue_401(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_expired")
    with patch(_PATCH_HTTPX_POST, return_value=_mock_error_response(401, "Bad credentials")):
        with pytest.raises(SystemExit) as exc:
            create_issue("title", "body", [])
        assert exc.value.code == 1


def test_create_issue_403_rate_limit(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    with patch(_PATCH_HTTPX_POST, return_value=_mock_error_response(403, "API rate limit exceeded")):
        with pytest.raises(SystemExit) as exc:
            create_issue("title", "body", [])
        assert exc.value.code == 1


def test_create_issue_api_error(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    with patch(_PATCH_HTTPX_POST, return_value=_mock_error_response(422, "Validation Failed")):
        with pytest.raises(SystemExit) as exc:
            create_issue("title", "body", [])
        assert exc.value.code == 1


def test_create_issue_connect_error(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    with patch(_PATCH_HTTPX_POST, side_effect=httpx.ConnectError("connection refused")):
        with pytest.raises(SystemExit) as exc:
            create_issue("title", "body", [])
        assert exc.value.code == 1


def test_create_issue_timeout(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    with patch(_PATCH_HTTPX_POST, side_effect=httpx.ReadTimeout("timed out")):
        with pytest.raises(SystemExit) as exc:
            create_issue("title", "body", [])
        assert exc.value.code == 1


# -- cmd_report_bug body building -----------------------------------------------


def test_report_bug_body_with_system_info(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "report-bug", "--title", "crash on spawn",
        "--description", "Editor crashes when spawning",
        "--steps", "1. Open editor\n2. Spawn actor",
        "--expected", "Actor spawns",
        "--actual", "Editor crashes",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(1)) as mock_post, \
         patch("soft_ue_cli.__main__.health_check", return_value={"status": "ok"}):
        args.func(args)

    call_body = mock_post.call_args.kwargs["json"]["body"]
    assert "## Description" in call_body
    assert "Editor crashes when spawning" in call_body
    assert "## Steps to Reproduce" in call_body
    assert "## Expected Behavior" in call_body
    assert "## Actual Behavior" in call_body
    assert "## System Information" in call_body
    assert "CLI version:" in call_body
    assert "Python:" in call_body
    assert "OS:" in call_body
    assert "Bridge: reachable" in call_body


def test_report_bug_body_no_system_info(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "report-bug", "--title", "test bug",
        "--description", "desc", "--no-system-info",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(2)) as mock_post:
        args.func(args)

    call_body = mock_post.call_args.kwargs["json"]["body"]
    assert "## System Information" not in call_body


def test_report_bug_optional_fields_omitted(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "report-bug", "--title", "minimal bug",
        "--description", "just a description", "--no-system-info",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(3)) as mock_post:
        args.func(args)

    call_body = mock_post.call_args.kwargs["json"]["body"]
    assert "## Description" in call_body
    assert "## Steps to Reproduce" not in call_body
    assert "## Expected Behavior" not in call_body
    assert "## Actual Behavior" not in call_body


# -- cmd_request_feature --------------------------------------------------------


def test_request_feature_body(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "request-feature", "--title", "add undo support",
        "--description", "Support undo for spawn-actor",
        "--use-case", "LLM agents need to revert mistakes",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(10)) as mock_post:
        args.func(args)

    call_body = mock_post.call_args.kwargs["json"]["body"]
    assert "## Description" in call_body
    assert "Support undo for spawn-actor" in call_body
    assert "## Use Case" in call_body
    assert "LLM agents need to revert mistakes" in call_body


def test_request_feature_labels_enhancement(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "request-feature", "--title", "feat",
        "--description", "desc",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(11)) as mock_post:
        args.func(args)

    call_labels = mock_post.call_args.kwargs["json"]["labels"]
    assert call_labels == ["enhancement"]


def test_request_feature_labels_nice_to_have(monkeypatch):
    monkeypatch.setenv("GITHUB_TOKEN", "ghp_test")
    parser = build_parser()
    args = parser.parse_args([
        "request-feature", "--title", "feat",
        "--description", "desc", "--priority", "nice-to-have",
    ])

    with patch(_PATCH_HTTPX_POST, return_value=_mock_issue_response(12)) as mock_post:
        args.func(args)

    call_labels = mock_post.call_args.kwargs["json"]["labels"]
    assert call_labels == ["nice-to-have"]

