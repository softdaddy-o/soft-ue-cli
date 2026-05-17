"""Tests for startup recovery prompt handling."""

from __future__ import annotations

import json

import pytest


from soft_ue_cli.startup_recovery import (
    StartupRecoveryBlocked,
    StartupRecoveryPrompt,
    handle_startup_recovery_prompt,
    load_startup_recovery_action,
    save_startup_recovery_action,
    startup_recovery_settings_path,
)


def _prompt() -> StartupRecoveryPrompt:
    return StartupRecoveryPrompt(
        hwnd=10,
        title="Unreal Editor",
        text="Recover assets from the previous session?",
        buttons={
            "Recover": 101,
            "Don't Recover": 102,
            "Cancel": 103,
        },
    )


def test_startup_recovery_settings_are_project_local(tmp_path, monkeypatch):
    (tmp_path / "Game.uproject").write_text("{}", encoding="utf-8")
    child = tmp_path / "Content" / "Maps"
    child.mkdir(parents=True)
    monkeypatch.chdir(child)

    path = startup_recovery_settings_path()

    assert path == tmp_path / ".soft-ue-bridge" / "settings.json"


def test_startup_recovery_missing_choice_blocks_unattended(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    with pytest.raises(StartupRecoveryBlocked) as exc:
        handle_startup_recovery_prompt(
            "ask",
            interactive=False,
            prompt_provider=_prompt,
        )

    assert "startup recovery prompt" in str(exc.value).lower()
    assert "recover" in str(exc.value).lower()
    assert "skip" in str(exc.value).lower()


def test_startup_recovery_uses_remembered_recover_choice(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    save_startup_recovery_action("recover")
    clicked: list[int] = []

    result = handle_startup_recovery_prompt(
        "ask",
        interactive=False,
        prompt_provider=_prompt,
        clicker=clicked.append,
    )

    assert clicked == [101]
    assert result is not None
    assert result.action == "recover"
    assert result.remembered is True


def test_startup_recovery_interactive_skip_can_be_remembered(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    answers = iter(["s", "y"])
    clicked: list[int] = []

    result = handle_startup_recovery_prompt(
        "ask",
        interactive=True,
        prompt_provider=_prompt,
        clicker=clicked.append,
        input_fn=lambda _prompt: next(answers),
    )

    assert clicked == [102]
    assert result is not None
    assert result.action == "skip"
    assert result.remembered is False
    assert result.saved is True
    assert load_startup_recovery_action() == "skip"
    settings = json.loads(startup_recovery_settings_path().read_text(encoding="utf-8"))
    assert settings["startup_recovery_action"] == "skip"


def test_startup_recovery_manual_leaves_prompt_for_user(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    clicked: list[int] = []

    result = handle_startup_recovery_prompt(
        "manual",
        interactive=False,
        prompt_provider=_prompt,
        clicker=clicked.append,
    )

    assert clicked == []
    assert result is not None
    assert result.action == "manual"
    assert result.clicked_button is None
