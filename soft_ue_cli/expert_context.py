"""Opt-in client for the public Expert Context service."""

from __future__ import annotations

import json
import os
import re
from collections.abc import Mapping, Sequence
from typing import Any

try:
    from . import __version__ as _CLI_VERSION
except Exception:  # pragma: no cover - defensive for unusual package loading
    _CLI_VERSION = "unknown"


REQUEST_SCHEMA = "soft-ue.expert-context-request.v1"
RESPONSE_SCHEMA = "soft-ue.expert-context.v1"
MAX_EVIDENCE_ITEMS = 32
MAX_RESPONSE_BYTES = 2 * 1024 * 1024

_WINDOWS_USER_PATH_RE = re.compile(r"(?i)\b[A-Z]:\\+Users\\+[^\\\s]+\\+")
_WINDOWS_ABSOLUTE_PATH_RE = re.compile(r"(?i)\b[A-Z]:[\\/][^\s\"']+")
_UNC_PATH_RE = re.compile(r"\\\\[^\\/\s]+[\\/][^\\/\s]+[\\/][^\s\"']+")
_UNIX_USER_PATH_RE = re.compile(r"(?i)(?:^|[\s\"'])(?:/Users|/home)/[^/\s]+/")
_TOKEN_RE = re.compile(r"(sk-[A-Za-z0-9_-]+|ghp_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+)")


class ExpertContextError(Exception):
    """Base class for Expert Context client failures."""


class ExpertContextConfigError(ExpertContextError):
    """Raised when the Expert Context client is not configured."""


class ExpertContextPrivacyError(ExpertContextError):
    """Raised when a request appears to include private local data."""


class ExpertContextAuthError(ExpertContextError):
    """Raised when the service rejects authentication."""


class ExpertContextUnavailableError(ExpertContextError):
    """Raised when the service reports temporary unavailability."""


class ExpertContextContractError(ExpertContextError):
    """Raised when the service response violates the public contract."""


class ExpertContextTransportError(ExpertContextError):
    """Raised when a network or timeout error prevents the request."""


def _redact(text: object, secrets: Sequence[str] = ()) -> str:
    message = str(text)
    for secret in secrets:
        if secret:
            message = message.replace(secret, "<redacted>")
    return _TOKEN_RE.sub("<redacted>", message)


def _privacy_violation(value: str) -> str | None:
    if _WINDOWS_USER_PATH_RE.search(value):
        return "Windows user path"
    if _WINDOWS_ABSOLUTE_PATH_RE.search(value):
        return "Windows absolute path"
    if _UNC_PATH_RE.search(value):
        return "UNC path"
    if _UNIX_USER_PATH_RE.search(value):
        return "Unix user path"
    if _TOKEN_RE.search(value):
        return "token"
    return None


def _scan_privacy(value: Any, path: str = "request") -> None:
    if isinstance(value, str):
        violation = _privacy_violation(value)
        if violation:
            raise ExpertContextPrivacyError(f"{violation} detected in {path}")
        return
    if isinstance(value, Mapping):
        for key, child in value.items():
            _scan_privacy(str(key), f"{path}.key")
            _scan_privacy(child, f"{path}.{key}")
        return
    if isinstance(value, Sequence) and not isinstance(value, (bytes, bytearray)):
        for index, child in enumerate(value):
            _scan_privacy(child, f"{path}[{index}]")


def _validate_evidence(evidence: object) -> list[dict[str, str]]:
    if not isinstance(evidence, list):
        raise ValueError("evidence must be a list of objects")
    if len(evidence) > MAX_EVIDENCE_ITEMS:
        raise ValueError(f"evidence must contain at most {MAX_EVIDENCE_ITEMS} items")

    validated: list[dict[str, str]] = []
    expected_keys = {"kind", "value", "source"}
    for index, item in enumerate(evidence):
        if not isinstance(item, dict) or set(item) != expected_keys:
            raise ValueError(f"evidence item {index} must contain string kind, value, and source")
        if not all(isinstance(item[key], str) for key in expected_keys):
            raise ValueError(f"evidence item {index} must contain string kind, value, and source")
        validated.append({key: item[key] for key in ("kind", "value", "source")})
    return validated


def build_context_request(
    *,
    task: str,
    evidence: list[dict[str, str]],
    environment: dict[str, Any],
) -> dict[str, Any]:
    """Build and privacy-check a public Expert Context request."""
    if not isinstance(task, str) or not task:
        raise ValueError("task must be a non-empty string")
    if not isinstance(environment, dict):
        raise ValueError("environment must be a JSON object")

    validated_evidence = _validate_evidence(evidence)
    request = {
        "schema": REQUEST_SCHEMA,
        "task": task,
        "evidence": validated_evidence,
        "environment": environment,
        "privacy": {
            "project_identifiers_removed": True,
            "raw_files_included": False,
        },
        "client": {
            "cli_version": _CLI_VERSION or "unknown",
            "agent_contract": "senior-ue-programmer@1",
        },
    }
    _scan_privacy(request)
    return request


class ExpertContextClient:
    """HTTP client for the opt-in public Expert Context endpoint."""

    def __init__(self, endpoint: str, api_key: str | None = None) -> None:
        endpoint = endpoint.strip().rstrip("/")
        if not endpoint:
            raise ExpertContextConfigError("Expert Context endpoint is not configured")
        self.endpoint = endpoint
        self.api_key = api_key or None

    @classmethod
    def from_environment(cls) -> "ExpertContextClient":
        endpoint = os.environ.get("SOFT_UE_EXPERT_SERVER_URL")
        if not endpoint:
            raise ExpertContextConfigError(
                "Expert Context endpoint is not configured; set SOFT_UE_EXPERT_SERVER_URL"
            )
        return cls(endpoint, os.environ.get("SOFT_UE_EXPERT_API_KEY"))

    def context(self, request: dict[str, Any]) -> dict[str, Any]:
        import httpx

        headers = {}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"

        try:
            with httpx.Client(
                base_url=self.endpoint,
                headers=headers,
                follow_redirects=False,
                timeout=30.0,
            ) as client:
                with client.stream("POST", "/v1/context", json=request) as response:
                    content = self._read_capped_response(response)
                    self._raise_for_status(response, content)
        except (httpx.TimeoutException, httpx.NetworkError, httpx.TransportError) as exc:
            raise ExpertContextTransportError(_redact(exc, (self.api_key or "",))) from exc

        return self._parse_response_content(content)

    def _raise_for_status(self, response: Any, content: bytes) -> None:
        if response.status_code in {401, 403}:
            detail = self._decode_response_detail(content) or f"HTTP {response.status_code}"
            raise ExpertContextAuthError(_redact(detail, (self.api_key or "",)))
        if response.status_code == 503:
            raise ExpertContextUnavailableError("Expert Context service is unavailable")
        if response.status_code >= 400:
            raise ExpertContextTransportError(f"Expert Context HTTP {response.status_code}")

    def _read_capped_response(self, response: Any) -> bytes:
        chunks: list[bytes] = []
        total = 0
        for chunk in response.iter_bytes():
            if not chunk:
                continue
            total += len(chunk)
            if total > MAX_RESPONSE_BYTES:
                raise ExpertContextContractError("Expert Context response is too large")
            chunks.append(chunk)
        return b"".join(chunks)

    def _decode_response_detail(self, content: bytes) -> str:
        try:
            return content.decode("utf-8", errors="replace")
        except Exception:
            return ""

    def _parse_response_content(self, content: bytes) -> dict[str, Any]:
        try:
            payload = json.loads(content.decode("utf-8"))
        except (ValueError, json.JSONDecodeError) as exc:
            raise ExpertContextContractError("Expert Context response is not valid JSON") from exc
        if not isinstance(payload, dict):
            raise ExpertContextContractError("Expert Context response must be a JSON object")
        if payload.get("schema") != RESPONSE_SCHEMA:
            raise ExpertContextContractError("Expert Context response schema is invalid")
        return payload
