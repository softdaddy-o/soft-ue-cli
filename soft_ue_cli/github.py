"""GitHub issue creation for soft-ue-cli feedback commands."""

from __future__ import annotations

import os
import subprocess
import sys

import httpx

GITHUB_REPO = "softdaddy-o/soft-ue-cli"
_API_BASE = f"https://api.github.com/repos/{GITHUB_REPO}"


def _resolve_token() -> str:
    """Resolve a GitHub token from env var or gh CLI.

    Order: GITHUB_TOKEN env var -> gh auth token subprocess.
    Required scope: public_repo (public repos) or repo (private).
    """
    token = os.environ.get("GITHUB_TOKEN", "").strip()
    if token:
        return token

    try:
        result = subprocess.run(
            ["gh", "auth", "token"],
            capture_output=True,
            stdin=subprocess.DEVNULL,
            text=True,
            timeout=10.0,
            check=True,
        )
        token = result.stdout.strip()
        if token:
            return token
    except subprocess.TimeoutExpired:
        print(
            "error: 'gh auth token' timed out while reading credentials.",
            file=sys.stderr,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass

    print(
        "error: no GitHub token found.\n"
        "Set GITHUB_TOKEN env var or run 'gh auth login'.\n"
        "Required scope: 'public_repo' (public repos) or 'repo' (private).",
        file=sys.stderr,
    )
    sys.exit(1)


def create_issue(title: str, body: str, labels: list[str]) -> dict:
    """Create a GitHub issue and return {"issue_number": int, "url": str}."""
    token = _resolve_token()
    try:
        response = httpx.post(
            f"{_API_BASE}/issues",
            headers={
                "Authorization": f"Bearer {token}",
                "Accept": "application/vnd.github+json",
                "X-GitHub-Api-Version": "2022-11-28",
            },
            json={"title": title, "body": body, "labels": labels},
            timeout=30.0,
        )
    except httpx.ConnectError:
        print("error: cannot connect to GitHub API", file=sys.stderr)
        sys.exit(1)
    except httpx.TimeoutException:
        print("error: GitHub API request timed out", file=sys.stderr)
        sys.exit(1)

    if response.status_code == 401:
        print("error: invalid or expired GitHub token", file=sys.stderr)
        sys.exit(1)

    if response.status_code >= 400:
        try:
            msg = response.json().get("message", response.text)
        except Exception:
            msg = response.text
        print(f"error: GitHub API {response.status_code}: {msg}", file=sys.stderr)
        sys.exit(1)

    data = response.json()
    return {"issue_number": data["number"], "url": data["html_url"]}

