import io
import json

import pytest

from schemaforge_api.app import planning_provider
from schemaforge_api.model_provider import NaviGatorProvider, OpenAIProvider, ProviderError, planning_messages


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


def test_navigator_adapter_sends_schema_and_validates_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    captured = {}

    def fake_urlopen(request, timeout):
        captured["url"] = request.full_url
        captured["auth"] = request.get_header("Authorization")
        captured["payload"] = json.loads(request.data)
        captured["timeout"] = timeout
        return FakeResponse(json.dumps({"choices": [{"finish_reason": "stop", "message": {
            "content": json.dumps(EMPTY_PLAN)}}]}).encode())

    monkeypatch.setattr("schemaforge_api.model_provider.urllib.request.urlopen", fake_urlopen)
    plan = NaviGatorProvider(api_key="test-only").propose(
        sql="CREATE TABLE t (id INT);", request="two rows", inspection={"tables": []}, current={})
    assert plan.version == 1
    assert captured["url"] == "https://api.ai.it.ufl.edu/v1/chat/completions"
    assert captured["auth"] == "Bearer test-only"
    assert captured["payload"]["model"] == "gpt-oss-20b"
    assert captured["payload"]["response_format"]["json_schema"]["strict"] is True
    assert "CREATE TABLE t" in captured["payload"]["messages"][1]["content"]
    assert captured["timeout"] == 45


def test_navigator_adapter_rejects_invalid_response(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("schemaforge_api.model_provider.urllib.request.urlopen", lambda *_args, **_kwargs:
                        FakeResponse(json.dumps({"choices": [{"finish_reason": "stop", "message": {
                            "content": '{"version":1}'}}]}).encode()))
    with pytest.raises(ProviderError, match="failed SchemaForge validation"):
        NaviGatorProvider(api_key="test-only").propose(sql="x", request="x", inspection={}, current={})


def test_provider_selection_prefers_configured_navigator_key(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("SCHEMAFORGE_AI_PROVIDER", raising=False)
    monkeypatch.setenv("NAVIGATOR_TOOLKIT_API_KEY", "test-only")
    assert isinstance(planning_provider(), NaviGatorProvider)
    monkeypatch.setenv("SCHEMAFORGE_AI_PROVIDER", "openai")
    assert isinstance(planning_provider(), OpenAIProvider)


def test_planning_prompt_includes_derived_parent_and_child_counts() -> None:
    messages = planning_messages(
        "CREATE TABLE customers (id INT); CREATE TABLE orders (id INT, customer_id INT);",
        "Make around 3000 rows and every customer should have 2 orders",
        {"tables": [{"name": "customers"}, {"name": "orders"}]}, {},
    )
    guidance = json.loads(messages[1]["content"])["derived_guidance"]
    assert guidance["parent_rows"] == 3000
    assert guidance["child_rows"] == 6000
    assert "not maximum limits" in messages[0]["content"]
