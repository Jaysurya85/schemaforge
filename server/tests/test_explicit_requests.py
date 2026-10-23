"""The four user-facing rule types must survive model mistakes and wording variants."""

import pytest

from schemaforge_api.ai_plan import (AIPlan, PlanApplicationError, canonicalize_model_targets, check_explicit_requests,
                                      drop_unchanged_model_rules, ground_explicit_settings)
from schemaforge_api.explicit_requests import extract_explicit_settings, row_counts


INSPECTION = {"tables": [
    {"name": "users", "rows": 100, "column_types": {"id": "INTEGER", "age": "INTEGER", "phone": "TEXT"}},
    {"name": "orders", "rows": 100, "column_types": {"id": "INTEGER", "status": "TEXT",
                                                     "total_amount": "DECIMAL", "coupon_code": "TEXT"}},
]}
EMPTY = AIPlan(version=1, table_rows=[], number_ranges=[], value_lists=[], null_rates=[],
               assumptions=[], unapplied=[])


@pytest.mark.parametrize("wording", [
    "Create 250 users.",
    "Create 250 rows of user.",
    "Set the users table to 250 rows.",
])
def test_row_count_wording(wording: str) -> None:
    assert row_counts(wording, "users") == {250}
    plan = ground_explicit_settings(wording, EMPTY, INSPECTION)
    assert [(item.table, item.rows) for item in plan.table_rows] == [("users", 250)]
    check_explicit_requests(wording, plan, INSPECTION)


@pytest.mark.parametrize("wording", [
    "Set orders.total_amount from 2 to 20.",
    "Set order total amount between 2 and 20.",
    "Set order total amount between 2 to 20.",
])
def test_numeric_range_wording(wording: str) -> None:
    plan = ground_explicit_settings(wording, EMPTY, INSPECTION)
    assert [(item.table, item.column, item.min, item.max) for item in plan.number_ranges] == [
        ("orders", "total_amount", 2, 20),
    ]
    check_explicit_requests(wording, plan, INSPECTION)


@pytest.mark.parametrize("goal", [
    "Use only pending and paid for orders.status.",
    "Set orders.status to pending or paid.",
])
def test_value_list_wording(goal: str) -> None:
    plan = ground_explicit_settings(goal, EMPTY, INSPECTION)
    assert plan.value_lists[0].values == ["pending", "paid"]
    check_explicit_requests(goal, plan, INSPECTION)


@pytest.mark.parametrize("wording", [
    "Make 10% of orders.coupon_code values null.",
    "Set 10 percent of orders.coupon_code values to null.",
])
def test_null_rate_wording_does_not_create_value_list(wording: str) -> None:
    plan = ground_explicit_settings(wording, EMPTY, INSPECTION)
    assert plan.value_lists == []
    assert [(item.table, item.column, item.probability) for item in plan.null_rates] == [
        ("orders", "coupon_code", 0.1),
    ]
    check_explicit_requests(wording, plan, INSPECTION)


def test_multiple_clauses_bind_to_the_right_fields() -> None:
    goal = ("Generate 1,000 users and 3,000 orders. User ages should be 18 to 75, "
            "order total amounts 10 to 250, and 5% of phone numbers can be null.")
    assert row_counts(goal, "users") == {1000}
    assert row_counts(goal, "orders") == {3000}
    settings = extract_explicit_settings(goal, INSPECTION)
    assert settings.ranges == {("users", "age"): (18, 75), ("orders", "total_amount"): (10, 250)}
    assert settings.null_rates == {("users", "phone"): 0.05}


def test_conflicting_range_and_value_list_are_rejected() -> None:
    goal = "Set orders.total_amount from 2 to 20. Use only 3 and 4 for orders.total_amount."
    with pytest.raises(PlanApplicationError, match="Both a range and a value list"):
        ground_explicit_settings(goal, EMPTY, INSPECTION)


def test_unsupported_null_target_is_left_to_engine_preflight() -> None:
    goal = "Make 10% of orders.id values null."
    plan = ground_explicit_settings(goal, EMPTY, INSPECTION)
    assert plan.null_rates[0].column == "id"


def test_unchanged_column_rules_are_not_treated_as_ai_inventions() -> None:
    plan = AIPlan(version=1, table_rows=[],
                  number_ranges=[{"table": "orders", "column": "total_amount", "min": 2, "max": 20}],
                  value_lists=[{"table": "orders", "column": "status", "values": ["pending", "paid"]}],
                  null_rates=[{"table": "orders", "column": "coupon_code", "probability": 0.1}],
                  assumptions=[], unapplied=[])
    current = {"tables": {"orders": {"rows": 100, "columns": {
        "total_amount": {"min": 2, "max": 20}, "status": {"values": ["pending", "paid"]},
        "coupon_code": {"null_probability": 0.1},
    }}}}
    cleaned = drop_unchanged_model_rules(plan, current)
    assert cleaned.number_ranges == cleaned.value_lists == cleaned.null_rates == []
    check_explicit_requests("Make a test.", cleaned, INSPECTION, current)


def test_model_target_aliases_map_to_schema_names() -> None:
    plan = AIPlan(version=1, table_rows=[{"table": "user", "rows": 250}],
                  number_ranges=[{"table": "order", "column": "order total amount", "min": 2, "max": 20}],
                  value_lists=[], null_rates=[], assumptions=[], unapplied=[])
    mapped = canonicalize_model_targets(plan, INSPECTION)
    assert mapped.table_rows[0].table == "users"
    assert (mapped.number_ranges[0].table, mapped.number_ranges[0].column) == ("orders", "total_amount")


def test_matching_numbers_or_words_on_wrong_column_do_not_authorize_extra_rules() -> None:
    range_goal = "Set orders.total_amount from 2 to 20."
    wrong_range = AIPlan(version=1, table_rows=[],
                         number_ranges=[{"table": "orders", "column": "id", "min": 2, "max": 20}],
                         value_lists=[], null_rates=[], assumptions=[], unapplied=[])
    with pytest.raises(PlanApplicationError, match="invented a numeric range"):
        check_explicit_requests(range_goal, wrong_range, INSPECTION)
    values_goal = "Use only pending and paid for orders.status."
    wrong_values = AIPlan(version=1, table_rows=[], number_ranges=[],
                          value_lists=[{"table": "users", "column": "phone", "values": ["pending", "paid"]}],
                          null_rates=[], assumptions=[], unapplied=[])
    with pytest.raises(PlanApplicationError, match="invented a value list"):
        check_explicit_requests(values_goal, wrong_values, INSPECTION)


def test_explicit_rules_override_wrong_model_values() -> None:
    goal = ("Create 250 rows of user. Set orders.total_amount between 2 to 20. "
            "Use only pending and paid for orders.status. Make 10% of orders.coupon_code values null.")
    wrong = AIPlan(
        version=1, table_rows=[{"table": "users", "rows": 10}],
        number_ranges=[{"table": "orders", "column": "total_amount", "min": 5, "max": 30}],
        value_lists=[{"table": "orders", "column": "status", "values": ["shipped"]}],
        null_rates=[{"table": "orders", "column": "coupon_code", "probability": 0.5}],
        assumptions=[], unapplied=[],
    )
    corrected = ground_explicit_settings(goal, wrong, INSPECTION)
    assert corrected.table_rows[0].rows == 250
    assert (corrected.number_ranges[0].min, corrected.number_ranges[0].max) == (2, 20)
    assert corrected.value_lists[0].values == ["pending", "paid"]
    assert corrected.null_rates[0].probability == 0.1
    check_explicit_requests(goal, corrected, INSPECTION)
