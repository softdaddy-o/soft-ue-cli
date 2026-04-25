"""Skill discovery and retrieval for soft-ue-cli."""

from __future__ import annotations

from pathlib import Path

_SKILLS_DIR = Path(__file__).parent


def _parse_frontmatter(text: str) -> dict[str, str]:
    """Extract YAML frontmatter fields from a markdown file's text."""
    if not text.startswith("---"):
        return {}
    end = text.find("---", 3)
    if end == -1:
        return {}
    fields: dict[str, str] = {}
    for line in text[3:end].strip().splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            fields[key.strip()] = value.strip()
    return fields


def list_skills() -> list[dict[str, str]]:
    """Return a list of available skills with name and description."""
    skills = []
    for md_file in sorted(_SKILLS_DIR.glob("*.md")):
        meta = _parse_frontmatter(md_file.read_text(encoding="utf-8"))
        if "name" in meta and "description" in meta:
            skills.append({"name": meta["name"], "description": meta["description"]})
    return skills


def get_skill(name: str) -> str | None:
    """Return the full content of a skill by name, or None if not found."""
    if "/" in name or "\\" in name or name.startswith("."):
        return None
    path = _SKILLS_DIR / f"{name}.md"
    if not path.is_file():
        return None
    return path.read_text(encoding="utf-8")
