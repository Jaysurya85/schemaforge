"""The AI proposal is data, never an executable engine config."""

import json
from pathlib import Path

import pytest
import yaml
from pydantic import ValidationError

from schemaforge_api.ai_plan import AIPlan, PlanApplicationError, apply_ai_plan
from schemaforge_api.models import ColumnSettings, RunRequest, TableSettings
from schemaforge_api.runner import compose_generation_config, render_generation_config


CASES = json.loads((Path(__file__).parents[2] / "docs" / "ai-planner-cases.json").read_text())["cases"]
MULTI_TABLE = next(case for case in CASES if case["id"] == "multi_table_supported")
INSPECTION = {
    "tables": [
        {"name": "customers", "rows": 10, "column_types": {"id": "INTEGER", "age": "INTEGER", "phone": "TEXT"}},
        {"name": "orders", "rows": 10, "column_types": {"id": "INTEGER", "customer_id": "INTEGER", "total": "DECIMAL"}},
    ]
}


def plan_with(**changes: object) -> AIPlan:
    data = {
        "version": 1, "table_rows": [], "number_ranges": [], "value_lists": [],
        "null_rates": [], "assumptions": [], "unapplied": [],
    }
    data.update(changes)
    return AIPlan.model_validate(data)


def test_documented_scenarios_follow_the_strict_plan_contract() -> None:
    assert len(CASES) == 6
    for case in CASES:
        assert AIPlan.model_validate(case["expected"]).version == 1


def test_plan_applies_supported_settings_without_mutating_manual_request() -> None:
    request = RunRequest(
        sql=MULTI_TABLE["schema"], seed=7, realistic=True,
        tables={"customers": TableSettings(rows=40, columns={"age": ColumnSettings(values=[21])})},
    )
    applied = apply_ai_plan(request, AIPlan.model_validate(MULTI_TABLE["expected"]), INSPECTION)
    assert request.tables["customers"].rows == 40
    assert request.tables["customers"].columns["age"].values == [21]
    assert applied.tables["customers"].rows == 1000
    assert applied.tables["orders"].rows == 3000
    assert applied.tables["customers"].columns["age"].model_dump(by_alias=True, exclude_none=True) == {
        "min": 18, "max": 75,
    }
    assert applied.tables["orders"].columns["total"].minimum == 10
    assert applied.tables["customers"].columns["phone"].null_probability == 0.05
    assert applied.seed == 7 and applied.realistic is True


def test_composer_serializes_the_same_settings_for_manual_and_ai_runs(monkeypatch, tmp_path: Path) -> None:
    request = apply_ai_plan(
        RunRequest(sql=MULTI_TABLE["schema"], validate_with="none"),
        AIPlan.model_validate(MULTI_TABLE["expected"]), INSPECTION,
    )
    base_config = {
        "schema": str(tmp_path / "schema.sql"),
        "generation": {"seed": 42, "default_rows": 10},
        "tables": {"customers": {"rows": 10}, "orders": {"rows": 10}},
    }
    monkeypatch.setattr("schemaforge_api.runner.initial_config", lambda _request, _directory: base_config.copy())
    config = compose_generation_config(request, tmp_path)
    rendered = render_generation_config(config)
    assert rendered == render_generation_config(config)
    parsed = yaml.safe_load(rendered)
    assert parsed["tables"]["customers"]["rows"] == 1000
    assert parsed["tables"]["customers"]["columns"]["age"] == {"min": 18.0, "max": 75.0}
    assert parsed["tables"]["customers"]["columns"]["phone"] == {"null_probability": 0.05}
    assert parsed["tables"]["orders"]["columns"]["total"] == {"min": 10.0, "max": 250.0}
    assert parsed["generation"]["realistic"] is False
    assert parsed["validation"] == {"sqlite": False, "postgres": False}


@pytest.mark.parametrize("change", [
    {"table_rows": [{"table": "missing", "rows": 5}]},
    {"number_ranges": [{"table": "customers", "column": "missing", "min": 1, "max": 5}]},
    {"number_ranges": [{"table": "customers", "column": "phone", "min": 1, "max": 5}]},
    {"number_ranges": [{"table": "customers", "column": "age", "min": 1.5, "max": 5}]},
    {"value_lists": [{"table": "customers", "column": "age", "values": ["18"]}]},
    {"value_lists": [{"table": "customers", "column": "phone", "values": [123]}]},
])
def test_plan_rejects_unknown_or_wrongly_typed_targets(change: dict[str, object]) -> None:
    with pytest.raises(PlanApplicationError):
        apply_ai_plan(RunRequest(sql=MULTI_TABLE["schema"]), plan_with(**change), INSPECTION)


@pytest.mark.parametrize("change", [
    {"version": 2},
    {"weighted_distribution": {"premium": 0.1}},
    {"table_rows": [{"table": "customers", "rows": 2_000_001}]},
    {"table_rows": [{"table": "customers", "rows": 5}, {"table": "customers", "rows": 6}]},
    {"number_ranges": [{"table": "customers", "column": "age", "min": 9, "max": 2}]},
    {"number_ranges": [{"table": "customers", "column": "age", "min": 10 ** 1000, "max": 10 ** 1001}]},
    {"value_lists": [{"table": "customers", "column": "phone", "values": ["a", "a"]}]},
    {"null_rates": [{"table": "customers", "column": "phone", "probability": 1.5}]},
    {"number_ranges": [{"table": "customers", "column": "age", "min": 1, "max": 5}],
     "value_lists": [{"table": "customers", "column": "age", "values": [2]}]},
])
def test_invalid_or_unsupported_proposal_shapes_are_rejected(change: dict[str, object]) -> None:
    with pytest.raises(ValidationError):
        plan_with(**change)
