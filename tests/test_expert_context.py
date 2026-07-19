from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "cli"))

from soft_ue_cli.expert_context import (  # noqa: E402
    ExpertContextAuthError,
    ExpertContextClient,
    ExpertContextConfigError,
    ExpertContextContractError,
    ExpertContextPrivacyError,
    ExpertContextUnavailableError,
    build_context_request,
)


def test_client_requires_explicit_endpoint(monkeypatch) -> None:
    monkeypatch.delenv("SOFT_UE_EXPERT_SERVER_URL", raising=False)
    with pytest.raises(ExpertContextConfigError, match="not configured"):
        ExpertContextClient.from_environment()


def test_build_request_rejects_personal_paths() -> None:
    with pytest.raises(ExpertContextPrivacyError):
        build_context_request(
            task="Build fails",
            evidence=[{"kind": "log", "value": r"C:\\Users\\alice\\secret\\Game.cpp", "source": "build-log"}],
            environment={"ue_version": "5.8"},
        )


@pytest.mark.parametrize(
    "private_path",
    [
        r"D:\srcp\PrivateProject\Content\Foo.uasset",
        "D:/Project/Content/Foo.uasset",
        r"\\server\share\Project\File.cpp",
    ],
)
def test_build_request_rejects_absolute_private_paths(private_path: str) -> None:
    with pytest.raises(ExpertContextPrivacyError):
        build_context_request(
            task="Build fails",
            evidence=[{"kind": "log", "value": private_path, "source": "build-log"}],
            environment={"ue_version": "5.8"},
        )


def test_client_posts_context_with_bearer_header(monkeypatch) -> None:
    captured: dict[str, object] = {}

    class FakeResponse:
        status_code = 200
        content = b'{"schema":"soft-ue.expert-context.v1","answer":"ok"}'

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            yield self.content

    class FakeClient:
        def __init__(self, **kwargs) -> None:
            captured["client_kwargs"] = kwargs

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]) -> FakeResponse:
            captured["method"] = method
            captured["path"] = path
            captured["json"] = json
            return FakeResponse()

    monkeypatch.setenv("SOFT_UE_EXPERT_SERVER_URL", "https://expert.example.test///")
    monkeypatch.setenv("SOFT_UE_EXPERT_API_KEY", "secret-token")
    monkeypatch.setattr("httpx.Client", FakeClient)

    client = ExpertContextClient.from_environment()
    response = client.context({"schema": "soft-ue.expert-context-request.v1"})

    assert response["answer"] == "ok"
    assert captured["method"] == "POST"
    assert captured["path"] == "/v1/context"
    assert captured["json"] == {"schema": "soft-ue.expert-context-request.v1"}
    assert captured["client_kwargs"] == {
        "base_url": "https://expert.example.test",
        "headers": {"Authorization": "Bearer secret-token"},
        "follow_redirects": False,
        "timeout": 30.0,
    }


def test_client_rejects_unavailable_response(monkeypatch) -> None:
    class FakeResponse:
        status_code = 503
        content = b'{"error":"down"}'

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            yield self.content

    class FakeClient:
        def __init__(self, **kwargs) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]) -> FakeResponse:
            return FakeResponse()

    monkeypatch.setattr("httpx.Client", FakeClient)

    client = ExpertContextClient("http://expert.test")
    with pytest.raises(ExpertContextUnavailableError):
        client.context({"schema": "soft-ue.expert-context-request.v1"})


def test_client_redacts_token_from_auth_and_transport_errors(monkeypatch) -> None:
    class AuthResponse:
        status_code = 401
        content = b'{"error":"bad secret-token"}'
        text = "bad secret-token"

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            yield self.content

    class AuthClient:
        def __init__(self, **kwargs) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]) -> AuthResponse:
            return AuthResponse()

    monkeypatch.setattr("httpx.Client", AuthClient)
    client = ExpertContextClient("http://expert.test", api_key="secret-token")

    with pytest.raises(ExpertContextAuthError) as auth_error:
        client.context({"schema": "soft-ue.expert-context-request.v1"})
    assert "secret-token" not in str(auth_error.value)

    class TransportClient(AuthClient):
        def stream(self, method: str, path: str, json: dict[str, object]):
            import httpx

            raise httpx.TimeoutException("timeout secret-token")

    monkeypatch.setattr("httpx.Client", TransportClient)

    with pytest.raises(Exception) as transport_error:
        client.context({"schema": "soft-ue.expert-context-request.v1"})
    assert transport_error.value.__class__.__name__ == "ExpertContextTransportError"
    assert "secret-token" not in str(transport_error.value)


def test_client_handles_real_streamed_auth_error_without_response_not_read(monkeypatch) -> None:
    import httpx

    def handler(request: httpx.Request) -> httpx.Response:
        assert request.headers["authorization"] == "Bearer secret-token"
        return httpx.Response(401, content=b'{"error":"bad secret-token"}')

    transport = httpx.MockTransport(handler)
    original_client = httpx.Client

    def client_factory(**kwargs):
        return original_client(transport=transport, **kwargs)

    monkeypatch.setattr("httpx.Client", client_factory)
    client = ExpertContextClient("http://expert.test", api_key="secret-token")

    with pytest.raises(ExpertContextAuthError) as auth_error:
        client.context({"schema": "soft-ue.expert-context-request.v1"})

    message = str(auth_error.value)
    assert "secret-token" not in message
    assert "bad <redacted>" in message


def test_client_handles_unread_streamed_auth_error_without_leaking_token(monkeypatch) -> None:
    import httpx

    class AuthResponse:
        status_code = 401

        def __init__(self) -> None:
            self._read = False
            self._content = b'{"error":"bad secret-token"}'

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        @property
        def text(self) -> str:
            if not self._read:
                raise httpx.ResponseNotRead()
            return self._content.decode("utf-8")

        def iter_bytes(self):
            self._read = True
            yield self._content

    class AuthClient:
        def __init__(self, **kwargs) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]) -> AuthResponse:
            return AuthResponse()

    monkeypatch.setattr("httpx.Client", AuthClient)
    client = ExpertContextClient("http://expert.test", api_key="secret-token")

    with pytest.raises(ExpertContextAuthError) as auth_error:
        client.context({"schema": "soft-ue.expert-context-request.v1"})

    message = str(auth_error.value)
    assert "secret-token" not in message
    assert "bad <redacted>" in message


def test_client_rejects_invalid_schema_and_oversize_response(monkeypatch) -> None:
    class BadSchemaResponse:
        status_code = 200
        content = b'{"schema":"wrong"}'

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            yield self.content

    class OversizeResponse:
        status_code = 200
        content = b"x" * (2 * 1024 * 1024 + 1)

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            yield self.content

    class FakeClient:
        response = BadSchemaResponse()

        def __init__(self, **kwargs) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]):
            return self.response

    monkeypatch.setattr("httpx.Client", FakeClient)
    client = ExpertContextClient("http://expert.test")

    with pytest.raises(ExpertContextContractError, match="schema"):
        client.context({"schema": "soft-ue.expert-context-request.v1"})

    FakeClient.response = OversizeResponse()
    with pytest.raises(ExpertContextContractError, match="too large"):
        client.context({"schema": "soft-ue.expert-context-request.v1"})


def test_client_streaming_response_cap_stops_reading_after_limit(monkeypatch) -> None:
    chunks_read = 0

    class StreamingResponse:
        status_code = 200

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def iter_bytes(self):
            nonlocal chunks_read
            chunk = b"x" * (1024 * 1024)
            for _ in range(4):
                chunks_read += 1
                yield chunk

    class StreamingClient:
        def __init__(self, **kwargs) -> None:
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb) -> None:
            return None

        def stream(self, method: str, path: str, json: dict[str, object]) -> StreamingResponse:
            assert method == "POST"
            assert path == "/v1/context"
            return StreamingResponse()

    monkeypatch.setattr("httpx.Client", StreamingClient)
    client = ExpertContextClient("http://expert.test")

    with pytest.raises(ExpertContextContractError, match="too large"):
        client.context({"schema": "soft-ue.expert-context-request.v1"})

    assert chunks_read == 3


def test_build_request_validates_evidence_shape_and_rejects_tokens() -> None:
    with pytest.raises(ExpertContextPrivacyError):
        build_context_request(
            task="Token leaked ghp_1234567890abcdef",
            evidence=[],
            environment={},
        )

    with pytest.raises(ValueError, match="evidence"):
        build_context_request(
            task="Build fails",
            evidence=[{"kind": "log", "value": "missing source"}],
            environment={},
        )


def test_build_request_includes_contract_metadata() -> None:
    request = build_context_request(
        task="Build fails",
        evidence=[{"kind": "log", "value": "UHT failed", "source": "build-log"}],
        environment={"ue_version": "5.8", "plugins": ["GameplayAbilities"]},
    )

    assert request["schema"] == "soft-ue.expert-context-request.v1"
    assert request["privacy"] == {
        "project_identifiers_removed": True,
        "raw_files_included": False,
    }
    assert request["client"]["agent_contract"] == "senior-ue-programmer@1"
    assert isinstance(request["client"]["cli_version"], str)
