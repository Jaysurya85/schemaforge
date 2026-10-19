"""Replaceable AI planner provider. No model call happens without an API key."""

import json
import os
import urllib.error
import urllib.request
from typing import Any, Protocol

from pydantic import ValidationError

from .ai_plan import AIPlan


class ProviderError(Exception):
    pass


class PlanProvider(Protocol):
    def propose(self, *, sql: str, request: str, inspection: dict[str, Any],
                current: dict[str, Any]) -> AIPlan: ...


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


class OpenAIProvider:
    def __init__(self, *, api_key: str | None = None, model: str | None = None) -> None:
        self.api_key = api_key or os.environ.get("OPENAI_API_KEY")
        self.model = model or os.environ.get("SCHEMAFORGE_OPENAI_MODEL", "gpt-4o-mini")

    def propose(self, *, sql: str, request: str, inspection: dict[str, Any],
                current: dict[str, Any]) -> AIPlan:
        if not self.api_key:
            raise ProviderError("Set OPENAI_API_KEY on the server to enable AI planning.")
        system = (
            "You plan constraint-valid test data for SchemaForge. Return JSON matching the schema. "
            "Use only inspected table and column names, and only supported table rows, numeric ranges, "
            "value lists, and null rates. Do not claim distributions or relationship patterns are "
            "implemented; put those requests in unapplied. State assumptions. If a request is ambiguous "
            "or impossible, put it in unapplied rather than inventing a rule. Never emit SQL or YAML."
        )
        payload = {
            "model": self.model,
            "store": False,
            "max_output_tokens": 3000,
            "input": [
                {"role": "system", "content": system},
                {"role": "user", "content": json.dumps({
                    "schema": sql, "inspection": inspection, "current_settings": current,
                    "testing_request": request}, ensure_ascii=False)},
            ],
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
