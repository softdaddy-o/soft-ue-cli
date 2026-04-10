"""Submit-testimonial command for soft-ue-cli."""

from __future__ import annotations

import json
import sys

from .github import create_issue


def _get_metadata() -> tuple[str, int, list[str]]:
    """Collect CLI version, streak length, and top tools."""
    import importlib.metadata

    try:
        cli_version = importlib.metadata.version("soft-ue-cli")
    except importlib.metadata.PackageNotFoundError:
        cli_version = "unknown"

    try:
        from .streak import get_streak, _load_data

        streak = get_streak()
        data = _load_data()
        usage = data.get("tool_usage", {})
        top_tools = sorted(usage, key=usage.get, reverse=True)[:5]
    except Exception:
        streak = 0
        top_tools = []

    return cli_version, streak, top_tools


def _build_issue_body(
    message: str,
    agent_name: str | None,
    rating: int | None,
    cli_version: str,
    streak: int,
    top_tools: list[str],
) -> str:
    """Build the markdown body for the GitHub Issue."""
    attribution = agent_name or "Anonymous"
    sections = [f"> {message}\n\n— {attribution}"]

    meta_lines = [f"- CLI version: {cli_version}", f"- Streak: {streak} days"]
    if rating is not None:
        stars = "★" * rating + "☆" * (5 - rating)
        meta_lines.append(f"- Rating: {stars}")
    if top_tools:
        meta_lines.append(f"- Top tools used: {', '.join(top_tools)}")

    sections.append("## Metadata\n" + "\n".join(meta_lines))
    return "\n\n".join(sections)


def cmd_submit_testimonial(args) -> None:
    """Handler for the submit-testimonial CLI command."""
    # Consent check
    if not args.yes:
        if not sys.stdin.isatty():
            print(
                "error: cannot prompt for consent in non-interactive mode.\n"
                "Pass --yes to confirm, or run interactively.",
                file=sys.stderr,
            )
            sys.exit(1)

        answer = input("Post this testimonial as a GitHub Issue? [y/N] ").strip().lower()
        if answer != "y":
            print("Testimonial not sent.", file=sys.stderr)
            return

    cli_version, streak, top_tools = _get_metadata()

    title = f"Testimonial from {args.agent_name}" if args.agent_name else "Testimonial"
    body = _build_issue_body(
        message=args.message,
        agent_name=args.agent_name,
        rating=args.rating,
        cli_version=cli_version,
        streak=streak,
        top_tools=top_tools,
    )

    result = create_issue(title, body, ["testimonial"])
    print(json.dumps(result, indent=2, ensure_ascii=False))
