"""Tests for GitHub Discussion creation via GraphQL."""

import json
from unittest.mock import patch, MagicMock

import httpx
import pytest

from soft_ue_cli.github import create_discussion


@patch("soft_ue_cli.github._resolve_token", return_value="ghp_test")
@patch("soft_ue_cli.github.httpx.post")
def test_create_discussion_success(mock_post, mock_token):
    # Mock _get_repo_id
    repo_response = MagicMock()
    repo_response.status_code = 200
    repo_response.json.return_value = {
        "data": {"repository": {"id": "R_abc123"}}
    }

    # Mock _get_discussion_category_id
    cat_response = MagicMock()
    cat_response.status_code = 200
    cat_response.json.return_value = {
        "data": {
            "repository": {
                "discussionCategories": {
                    "nodes": [
                        {"id": "DC_123", "name": "Testimonials", "slug": "testimonials"},
                        {"id": "DC_456", "name": "General", "slug": "general"},
                    ]
                }
            }
        }
    }

    # Mock createDiscussion mutation
    disc_response = MagicMock()
    disc_response.status_code = 200
    disc_response.json.return_value = {
        "data": {
            "createDiscussion": {
                "discussion": {
                    "number": 42,
                    "url": "https://github.com/softdaddy-o/soft-ue-cli/discussions/42",
                }
            }
        }
    }

    mock_post.side_effect = [repo_response, cat_response, disc_response]
    result = create_discussion("Test Title", "Test Body", "Testimonials")
    assert result["discussion_number"] == 42
    assert "discussions/42" in result["url"]


@patch("soft_ue_cli.github._resolve_token", return_value="ghp_test")
@patch("soft_ue_cli.github.httpx.post")
def test_create_discussion_graphql_error(mock_post, mock_token):
    response = MagicMock()
    response.status_code = 200
    response.json.return_value = {
        "errors": [{"message": "category not found"}]
    }
    mock_post.return_value = response
    with pytest.raises(SystemExit):
        create_discussion("Title", "Body", "NonExistent")


@patch("soft_ue_cli.github._resolve_token", return_value="ghp_test")
@patch("soft_ue_cli.github.httpx.post")
def test_create_discussion_connection_error(mock_post, mock_token):
    mock_post.side_effect = httpx.ConnectError("refused")
    with pytest.raises(SystemExit):
        create_discussion("Title", "Body", "Testimonials")


@patch("soft_ue_cli.github._resolve_token", return_value="ghp_test")
@patch("soft_ue_cli.github.httpx.post")
def test_create_discussion_falls_back_to_general(mock_post, mock_token):
    """When requested category doesn't exist, falls back to General."""
    repo_response = MagicMock()
    repo_response.status_code = 200
    repo_response.json.return_value = {
        "data": {"repository": {"id": "R_abc123"}}
    }

    cat_response = MagicMock()
    cat_response.status_code = 200
    cat_response.json.return_value = {
        "data": {
            "repository": {
                "discussionCategories": {
                    "nodes": [
                        {"id": "DC_456", "name": "General", "slug": "general"},
                    ]
                }
            }
        }
    }

    disc_response = MagicMock()
    disc_response.status_code = 200
    disc_response.json.return_value = {
        "data": {
            "createDiscussion": {
                "discussion": {
                    "number": 1,
                    "url": "https://github.com/softdaddy-o/soft-ue-cli/discussions/1",
                }
            }
        }
    }

    mock_post.side_effect = [repo_response, cat_response, disc_response]
    result = create_discussion("Title", "Body", "Testimonials")
    assert result["discussion_number"] == 1
