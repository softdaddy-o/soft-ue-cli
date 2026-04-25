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
            text=True,
            check=True,
        )
        token = result.stdout.strip()
        if token:
            return token
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


_GRAPHQL_URL = "https://api.github.com/graphql"


def _graphql(query: str, variables: dict) -> dict:
    """Execute a GitHub GraphQL query and return the response data."""
    token = _resolve_token()
    try:
        response = httpx.post(
            _GRAPHQL_URL,
            headers={
                "Authorization": f"Bearer {token}",
                "Accept": "application/vnd.github+json",
            },
            json={"query": query, "variables": variables},
            timeout=30.0,
        )
    except httpx.ConnectError:
        print("error: cannot connect to GitHub GraphQL API", file=sys.stderr)
        sys.exit(1)
    except httpx.TimeoutException:
        print("error: GitHub GraphQL API request timed out", file=sys.stderr)
        sys.exit(1)

    data = response.json()
    if "errors" in data:
        msg = data["errors"][0].get("message", "unknown GraphQL error")
        print(f"error: GitHub GraphQL: {msg}", file=sys.stderr)
        sys.exit(1)

    return data["data"]


def _get_discussion_category_id(repo_id: str, category_slug: str) -> str:
    """Resolve a Discussion category name to its node ID."""
    query = """
    query($owner: String!, $name: String!) {
      repository(owner: $owner, name: $name) {
        discussionCategories(first: 25) {
          nodes { id name slug }
        }
      }
    }
    """
    owner, name = GITHUB_REPO.split("/")
    data = _graphql(query, {"owner": owner, "name": name})
    categories = data["repository"]["discussionCategories"]["nodes"]
    for cat in categories:
        if cat["slug"] == category_slug or cat["name"].lower() == category_slug.lower():
            return cat["id"]

    # Fall back to "General" if requested category not found
    for cat in categories:
        if cat["slug"] == "general" or cat["name"].lower() == "general":
            return cat["id"]

    print(
        f"error: no Discussion category '{category_slug}' or 'General' found.\n"
        "Create a 'Testimonials' category in repo Settings > Discussions.",
        file=sys.stderr,
    )
    sys.exit(1)


def _get_repo_id() -> str:
    """Get the repository node ID for GraphQL mutations."""
    query = """
    query($owner: String!, $name: String!) {
      repository(owner: $owner, name: $name) { id }
    }
    """
    owner, name = GITHUB_REPO.split("/")
    data = _graphql(query, {"owner": owner, "name": name})
    return data["repository"]["id"]


def create_discussion(title: str, body: str, category_name: str) -> dict:
    """Create a GitHub Discussion and return {"discussion_number": int, "url": str}."""
    repo_id = _get_repo_id()
    category_id = _get_discussion_category_id(repo_id, category_name)

    mutation = """
    mutation($repoId: ID!, $categoryId: ID!, $title: String!, $body: String!) {
      createDiscussion(input: {
        repositoryId: $repoId,
        categoryId: $categoryId,
        title: $title,
        body: $body
      }) {
        discussion { number url }
      }
    }
    """
    data = _graphql(mutation, {
        "repoId": repo_id,
        "categoryId": category_id,
        "title": title,
        "body": body,
    })
    disc = data["createDiscussion"]["discussion"]
    return {"discussion_number": disc["number"], "url": disc["url"]}
