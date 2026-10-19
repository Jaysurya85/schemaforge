import io
import json

import pytest

from schemaforge_api.model_provider import OpenAIProvider, ProviderError


EMPTY_PLAN = {"version": 1, "table_rows": [], "number_ranges": [], "value_lists": [],
              "null_rates": [], "assumptions": [], "unapplied": []}


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def test_openai_adapter_requests_strict_schema_without_storing(monkeypatch: pytest.MonkeyPatch) -> None:
    captured = {}

    def fake_urlopen(request, timeout):
        captured["payload"] = json.loads(request.data)
        captured["timeout"] = timeout
        return FakeResponse(json.dumps({"status": "completed", "output": [{"type": "message", "content": [
            {"type": "output_text", "text": json.dumps(EMPTY_PLAN)}]}]}).encode())

    monkeypatch.setattr("schemaforge_api.model_provider.urllib.request.urlopen", fake_urlopen)
    plan = OpenAIProvider(api_key="test-only").propose(
        sql="CREATE TABLE t (id INT);", request="two rows", inspection={"tables": []}, current={})
    assert plan.version == 1
    assert captured["payload"]["text"]["format"]["strict"] is True
    assert captured["payload"]["store"] is False
    assert captured["timeout"] == 45


def test_openai_adapter_rejects_invalid_response(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("schemaforge_api.model_provider.urllib.request.urlopen", lambda *_args, **_kwargs:
                        FakeResponse(json.dumps({"status": "completed", "output": [{"type": "message", "content": [
                            {"type": "output_text", "text": '{"version":1}'}]}]}).encode()))
    with pytest.raises(ProviderError, match="failed SchemaForge validation"):
        OpenAIProvider(api_key="test-only").propose(sql="x", request="x", inspection={}, current={})


def test_openai_adapter_needs_key(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(ProviderError, match="OPENAI_API_KEY"):
        OpenAIProvider().propose(sql="x", request="x", inspection={}, current={})
