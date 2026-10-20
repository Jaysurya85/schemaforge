"""Replaceable AI planner providers. No model call happens without an API key."""

import json
import os
import urllib.error
import urllib.request
from typing import Any, Protocol

from pydantic import ValidationError

from .ai_plan import AIPlan, derive_fanout_request


class ProviderError(Exception):
    pass


class PlanProvider(Protocol):
    def propose(self, *, sql: str, request: str, inspection: dict[str, Any],
                current: dict[str, Any], feedback: str | None = None) -> AIPlan: ...


def _object(properties: dict[str, Any]) -> dict[str, Any]:
    return {"type": "object", "properties": properties,
            "required": list(properties), "additionalProperties": False}


_STRING = {"type": "string"}
_NUMBER = {"type": "number"}
_SCALAR = {"anyOf": [{"type": kind} for kind in ("string", "integer", "number", "boolean")]}
PLAN_SCHEMA = _object({
    "version": {"type": "integer", "enum": [1]},
    "table_rows": {"type": "array", "items": _object({"table": _STRING, "rows": {"type": "integer"}})},
    "number_ranges": {"type": "array", "items": _object({
        "table": _STRING, "column": _STRING, "min": _NUMBER, "max": _NUMBER})},
    "value_lists": {"type": "array", "items": _object({
        "table": _STRING, "column": _STRING, "values": {"type": "array", "items": _SCALAR}})},
    "null_rates": {"type": "array", "items": _object({
        "table": _STRING, "column": _STRING, "probability": _NUMBER})},
    "assumptions": {"type": "array", "items": _STRING},
    "unapplied": {"type": "array", "items": _object({
        "request": _STRING,
        "reason": {"type": "string", "enum": [
            "unsupported_distribution", "unsupported_relationship", "unknown_schema_field",
            "ambiguous_request", "conflicting_request"]},
        "explanation": _STRING})},
})
SYSTEM_PROMPT = (
    "You plan constraint-valid test data for SchemaForge. Return JSON matching the schema. "
    "Use only inspected table and column names, and only supported table rows, numeric ranges, "
    "value lists, and null rates. Do not claim distributions or relationship patterns are "
    "implemented; put explicitly requested unsupported behavior in unapplied. "
    "Copy every explicit numeric row count, numeric range, and null percentage exactly. "
    "Current row settings are editable defaults, not maximum limits. When a parent count and "
    "children-per-parent are given, calculate the child row count, but mark the exact "
    "per-parent assignment as an unsupported relationship. Do not call a current row count a limit. "
    "Do not add row counts, ranges, value lists, or null rates that the user did not request. "
    "Never guess a row count from words like small or medium. "
    "A list of allowed categories does not imply a percentage distribution; do not mark an "
    "unstated distribution as unapplied. Do not turn requested percentages into literal values. "
    "Apply supported parts of a request even if another part is unsupported. "
    "State only necessary assumptions. If a requested behavior is ambiguous or impossible, "
    "put that specific request in unapplied rather than inventing a rule. Never emit SQL or YAML."
)


def planning_messages(sql: str, request: str, inspection: dict[str, Any],
                      current: dict[str, Any], feedback: str | None = None) -> list[dict[str, str]]:
    payload = {"schema": sql, "inspection": inspection, "current_settings": current,
               "testing_request": request}
    fanout = derive_fanout_request(request, inspection)
    if fanout is not None:
        payload["derived_guidance"] = {
            "parent_table": fanout.parent, "parent_rows": fanout.parent_rows,
            "child_table": fanout.child, "child_rows": fanout.child_rows,
            "exact_per_parent": fanout.per_parent,
            "note": "Row counts may be proposed; exact per-parent assignment is unsupported.",
        }
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": json.dumps(payload, ensure_ascii=False)},
    ]
    if feedback:
        messages.append({"role": "user", "content": (
            f"The previous proposal failed validation: {feedback} "
            "Return a corrected proposal that follows the original testing request exactly.")})
    return messages


class OpenAIProvider:
    def __init__(self, *, api_key: str | None = None, model: str | None = None) -> None:
        self.api_key = api_key or os.environ.get("OPENAI_API_KEY")
        self.model = model or os.environ.get("SCHEMAFORGE_OPENAI_MODEL", "gpt-4o-mini")

    def propose(self, *, sql: str, request: str, inspection: dict[str, Any],
                current: dict[str, Any], feedback: str | None = None) -> AIPlan:
        if not self.api_key:
            raise ProviderError("Set OPENAI_API_KEY on the server to enable AI planning.")
        payload = {
            "model": self.model,
            "store": False,
            "max_output_tokens": 3000,
            "input": planning_messages(sql, request, inspection, current, feedback),
            "text": {"format": {"type": "json_schema", "name": "schemaforge_plan",
                                "strict": True, "schema": PLAN_SCHEMA}},
        }
        http_request = urllib.request.Request(
            "https://api.openai.com/v1/responses",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(http_request, timeout=45) as response:
                data = json.load(response)
        except urllib.error.HTTPError as error:
            raise ProviderError(f"OpenAI planning request failed (HTTP {error.code}).") from error
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as error:
            raise ProviderError("OpenAI planning request could not be completed.") from error
        if data.get("status") != "completed":
            raise ProviderError("OpenAI did not complete the planning response.")
        contents = [content for output in data.get("output", []) if output.get("type") == "message"
                    for content in output.get("content", [])]
        if any(content.get("type") == "refusal" for content in contents):
            raise ProviderError("OpenAI declined this planning request.")
        texts = [content.get("text") for content in contents if content.get("type") == "output_text"]
        if len(texts) != 1 or not isinstance(texts[0], str):
            raise ProviderError("OpenAI returned no usable plan.")
        try:
            return AIPlan.model_validate_json(texts[0])
        except ValidationError as error:
            raise ProviderError("OpenAI returned a plan that failed SchemaForge validation.") from error


class NaviGatorProvider:
    """Use a UF personal key with a UF-hosted model for local planning."""

    def __init__(self, *, api_key: str | None = None, model: str | None = None) -> None:
        self.api_key = api_key or os.environ.get("NAVIGATOR_TOOLKIT_API_KEY")
        self.model = model or os.environ.get("SCHEMAFORGE_NAVIGATOR_MODEL", "gpt-oss-20b")

    def propose(self, *, sql: str, request: str, inspection: dict[str, Any],
                current: dict[str, Any], feedback: str | None = None) -> AIPlan:
        if not self.api_key:
            raise ProviderError("Set NAVIGATOR_TOOLKIT_API_KEY on the server to enable NaviGator planning.")
        payload = {
            "model": self.model,
            "messages": planning_messages(sql, request, inspection, current, feedback),
            "response_format": {"type": "json_schema", "json_schema": {
                "name": "schemaforge_plan", "strict": True, "schema": PLAN_SCHEMA}},
        }
        http_request = urllib.request.Request(
            "https://api.ai.it.ufl.edu/v1/chat/completions",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(http_request, timeout=45) as response:
                data = json.load(response)
        except urllib.error.HTTPError as error:
            raise ProviderError(f"NaviGator planning request failed (HTTP {error.code}).") from error
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as error:
            raise ProviderError("NaviGator planning request could not be completed.") from error
        choices = data.get("choices", [])
        if not choices or choices[0].get("finish_reason") not in ("stop", None):
            raise ProviderError("NaviGator did not complete the planning response.")
        message = choices[0].get("message", {})
        if message.get("refusal"):
            raise ProviderError("NaviGator declined this planning request.")
        content = message.get("content")
        if not isinstance(content, str) or not content:
            raise ProviderError("NaviGator returned no usable plan.")
        try:
            return AIPlan.model_validate_json(content)
        except ValidationError as error:
            raise ProviderError("NaviGator returned a plan that failed SchemaForge validation.") from error
