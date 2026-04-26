"""Tests for cli/soft_ue_cli/skills — skill discovery and retrieval."""

from __future__ import annotations

from pathlib import Path

import pytest

from soft_ue_cli.skills import get_skill, list_skills
from soft_ue_cli.__main__ import build_parser, cmd_skills


# -- list_skills ---------------------------------------------------------------


def test_list_skills_returns_nonempty():
    skills = list_skills()
    assert len(skills) > 0


def test_list_skills_items_have_name_and_description():
    for skill in list_skills():
        assert "name" in skill
        assert "description" in skill
        assert isinstance(skill["name"], str)
        assert isinstance(skill["description"], str)


def test_list_skills_contains_blueprint_to_cpp():
    names = [s["name"] for s in list_skills()]
    assert "blueprint-to-cpp" in names
    assert "test-tools" in names
    assert "replay-changes" in names
    assert "author-test" in names
    assert "author-regression-test" in names
    assert "author-anim-state-test" in names
    assert "author-bp-parity-test" in names
    assert "author-invariant-test" in names


# -- get_skill -----------------------------------------------------------------


def test_get_skill_returns_content():
    content = get_skill("blueprint-to-cpp")
    assert content is not None
    assert len(content) > 0
    assert "blueprint-to-cpp" in content
    assert "Dependency-first planning" in content
    assert "soft-ue-cli query-enum" in content
    assert "soft-ue-cli query-struct" in content


def test_test_tools_contains_idempotent_teardown_and_insights_stop():
    content = get_skill("test-tools")
    assert content is not None
    assert "When a new CLI tool, MCP-exposed tool, or new inspect/diff section is added" in content
    assert "already removed (treated as pass)" in content
    assert "auto-stopped (treated as pass)" in content
    assert 'encoding="utf-8"' in content
    assert "open test level retry" in content
    assert "save-asset (test level before restore)" in content
    assert "inspect-uasset summary" in content
    assert "inspect-uasset properties" in content
    assert "asset_to_disk_path" in content
    assert "project_directory" in content
    assert "diff-uasset summary" in content
    assert "diff-uasset properties" in content
    assert "save-asset blueprint" in content
    assert "shutil.copy2" in content


def test_test_tools_contains_config_suite():
    content = get_skill("test-tools")
    assert content is not None
    # Bridge tools
    assert "get-config-value" in content
    assert "set-config-value" in content
    assert "validate-config-key" in content
    # CLI subcommands
    assert 'run_cli("config tree"' in content
    assert 'run_cli("config get search"' in content
    assert 'run_cli("config set user layer"' in content
    assert 'run_cli("config diff audit"' in content
    assert 'run_cli("config audit"' in content
    assert "OfflineSearchKey_" in content


def test_test_tools_contains_new_automation_features():
    content = get_skill("test-tools")
    assert content is not None
    assert "advanced-automation" in content
    assert "run-python-script helper import" in content
    assert "from soft_ue_bridge import call" in content
    assert "batch-call pie/query/logs smoke" in content
    assert "call-function transient native" in content


def test_get_skill_nonexistent_returns_none():
    assert get_skill("nonexistent-skill-xyz") is None


def test_get_skill_path_traversal_returns_none():
    assert get_skill("../../../README") is None
    assert get_skill("..\\..\\README") is None
    assert get_skill(".hidden") is None


def test_get_skill_content_has_frontmatter():
    content = get_skill("blueprint-to-cpp")
    assert content.startswith("---")


def test_replay_changes_skill_mentions_bundle_workflow():
    content = get_skill("replay-changes")
    assert content is not None
    assert "Git workflow" in content
    assert "Perforce workflow" in content
    assert "git show :1:" in content
    assert "p4 sync" in content


def test_author_test_skill_mentions_supported_subskills():
    content = get_skill("author-test")
    assert content is not None
    assert "Use a two-layer workflow:" in content
    assert "Exploration layer: CLI + bridge + Python scripts" in content
    assert "Committed layer: C++ Automation Spec tests" in content
    assert "author-regression-test" in content
    assert "author-anim-state-test" in content
    assert "author-bp-parity-test" in content
    assert "author-invariant-test" in content
    assert "C++ scaffold" in content


def test_authoring_subskills_target_cpp_committed_tests():
    regression = get_skill("author-regression-test")
    anim = get_skill("author-anim-state-test")
    parity = get_skill("author-bp-parity-test")
    invariant = get_skill("author-invariant-test")

    assert regression is not None
    assert anim is not None
    assert parity is not None
    assert invariant is not None

    assert "committed C++ gameplay regression test" in regression
    assert "Source/<Project>Tests/Private/Regression/TEST_<Slug>.cpp" in regression
    assert "committed C++ regression test for animation behavior" in anim
    assert "Source/<Project>Tests/Private/Anim/TEST_<Slug>.cpp" in anim
    assert "committed C++ Automation Spec" in parity
    assert "CLI capture is acceptable here as exploration tooling" in parity
    assert "single-property invariant" in invariant
    assert "Source/<Project>Tests/Private/Invariants/TEST_<Slug>.cpp" in invariant


# -- skill file validation -----------------------------------------------------


def test_all_skills_have_required_frontmatter():
    """Every .md skill file must have name, description, and version in frontmatter."""
    skills_dir = Path(__file__).parents[1] / "soft_ue_cli" / "skills"
    for md_file in skills_dir.glob("*.md"):
        text = md_file.read_text(encoding="utf-8")
        assert text.startswith("---"), f"{md_file.name} missing frontmatter"
        end = text.find("---", 3)
        assert end != -1, f"{md_file.name} missing closing frontmatter fence"
        front = text[3:end]
        assert "name:" in front, f"{md_file.name} missing name"
        assert "description:" in front, f"{md_file.name} missing description"
        assert "version:" in front, f"{md_file.name} missing version"


# -- CLI argument parsing ------------------------------------------------------


def test_parser_skills_list():
    parser = build_parser()
    args = parser.parse_args(["skills", "list"])
    assert args.command == "skills"
    assert args.skills_action == "list"


def test_parser_skills_get():
    parser = build_parser()
    args = parser.parse_args(["skills", "get", "blueprint-to-cpp"])
    assert args.command == "skills"
    assert args.skills_action == "get"
    assert args.skill_name == "blueprint-to-cpp"


def test_cmd_skills_list_prints_output(capsys):
    args = build_parser().parse_args(["skills", "list"])
    cmd_skills(args)
    out = capsys.readouterr().out
    assert "blueprint-to-cpp" in out


def test_cmd_skills_get_prints_content(capsys):
    args = build_parser().parse_args(["skills", "get", "blueprint-to-cpp"])
    cmd_skills(args)
    out = capsys.readouterr().out
    assert "---" in out
    assert "name: blueprint-to-cpp" in out


def test_cmd_skills_get_nonexistent_exits():
    args = build_parser().parse_args(["skills", "get", "no-such-skill"])
    with pytest.raises(SystemExit) as exc:
        cmd_skills(args)
    assert exc.value.code == 1
