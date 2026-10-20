"""The AI proposal is data, never an executable engine config."""

import json
from pathlib import Path

import pytest
import yaml
from pydantic import ValidationError

from schemaforge_api.ai_plan import (AIPlan, PlanApplicationError, apply_ai_plan,
                                      check_explicit_requests, derive_fanout_request,
                                      explain_unsupported_fanout)
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


def test_explicit_goal_numbers_must_survive_the_model_proposal() -> None:
    goal = "Generate 1,000 customers and 3,000 orders. 5% of phone numbers can be null."
    plan = plan_with(table_rows=[{"table": "customers", "rows": 1000},
                                {"table": "orders", "rows": 3000}],
                     null_rates=[{"table": "customers", "column": "phone", "probability": 0.05}])
    check_explicit_requests(goal, plan, INSPECTION)
    with pytest.raises(PlanApplicationError, match="requested 1000 rows"):
        check_explicit_requests(goal, plan_with(table_rows=[{"table": "customers", "rows": 10},
                                                 {"table": "orders", "rows": 3000}],
                                               null_rates=plan.null_rates), INSPECTION)
    with pytest.raises(PlanApplicationError, match="5% null rate"):
        check_explicit_requests(goal, plan_with(table_rows=plan.table_rows), INSPECTION)


def test_vague_size_cannot_create_an_unrequested_row_count() -> None:
    with pytest.raises(PlanApplicationError, match="invented a row count"):
        check_explicit_requests("Make a medium-sized customer dataset.",
                                plan_with(table_rows=[{"table": "customers", "rows": 50}]), INSPECTION)


def test_parent_count_and_per_parent_request_imply_child_rows() -> None:
    goal = "i want to heavy test on this mkae around 3000 rows and every customer should have 2 orders"
    fanout = derive_fanout_request(goal, INSPECTION)
    assert fanout is not None
    assert (fanout.parent_rows, fanout.child_rows, fanout.inferred_parent) == (3000, 6000, True)
    wrong = plan_with(table_rows=[{"table": "customers", "rows": 3000},
                                  {"table": "orders", "rows": 3000}])
    with pytest.raises(PlanApplicationError, match="requested 6000 rows for orders"):
        check_explicit_requests(goal, wrong, INSPECTION)

    correct = plan_with(table_rows=[{"table": "customers", "rows": 3000},
                                    {"table": "orders", "rows": 6000}],
                        unapplied=[{"request": "every customer should have 2 orders",
                                    "reason": "conflicting_request",
                                    "explanation": "6000 exceeds a supposed 3000-row limit."}])
    check_explicit_requests(goal, correct, INSPECTION)
    explained = explain_unsupported_fanout(goal, correct, INSPECTION)
    assert explained.unapplied[0].reason == "unsupported_relationship"
    assert "average" in explained.unapplied[0].explanation
    assert "limit" not in explained.unapplied[0].explanation
    assert explained.assumptions[0] == "Interpreted the row count as 3,000 customers."


def test_explicit_parent_count_implies_child_count_without_inference_assumption() -> None:
    goal = "Generate 3,000 customers. Every customer should have 2 orders."
    fanout = derive_fanout_request(goal, INSPECTION)
    assert fanout is not None
    assert (fanout.parent_rows, fanout.child_rows, fanout.inferred_parent) == (3000, 6000, False)
    plan = plan_with(table_rows=[{"table": "customers", "rows": 3000},
                                 {"table": "orders", "rows": 6000}])
    check_explicit_requests(goal, plan, INSPECTION)
    assert explain_unsupported_fanout(goal, plan, INSPECTION).assumptions == []


def test_per_parent_wording_also_derives_child_count() -> None:
    request = derive_fanout_request("Generate 3000 customers with 2 orders per customer.", INSPECTION)
    assert request is not None
    assert (request.parent_rows, request.child_rows) == (3000, 6000)


def test_plan_must_not_invent_ranges_values_or_null_rates() -> None:
    with pytest.raises(PlanApplicationError, match="invented a numeric range"):
        check_explicit_requests("Generate 5 customers.",
                                plan_with(table_rows=[{"table": "customers", "rows": 5}],
                                          number_ranges=[{"table": "customers", "column": "id", "min": 1, "max": 5}]),
                                INSPECTION)
    with pytest.raises(PlanApplicationError, match="invented a value"):
        check_explicit_requests("Generate 5 customers.",
                                plan_with(table_rows=[{"table": "customers", "rows": 5}],
                                          value_lists=[{"table": "customers", "column": "phone", "values": ["unknown"]}]),
                                INSPECTION)
    with pytest.raises(PlanApplicationError, match="invented a null rate"):
        check_explicit_requests("Generate 5 customers.",
                                plan_with(table_rows=[{"table": "customers", "rows": 5}],
                                          null_rates=[{"table": "customers", "column": "phone", "probability": 0}]),
                                INSPECTION)


def test_percentage_distribution_must_be_explicitly_deferred() -> None:
    goal = "Generate 1000 customers. Exactly 10% should be premium."
    rows = [{"table": "customers", "rows": 1000}]
    with pytest.raises(PlanApplicationError, match="distribution as unsupported"):
        check_explicit_requests(goal, plan_with(table_rows=rows), INSPECTION)
    deferred = [{"request": "Exactly 10% should be premium", "reason": "unsupported_distribution",
                 "explanation": "Weighted distributions are not supported."}]
    check_explicit_requests(goal, plan_with(table_rows=rows, unapplied=deferred), INSPECTION)
