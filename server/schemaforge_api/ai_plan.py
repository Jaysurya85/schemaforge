"""Strict AI proposal contract and deterministic mapping to run settings."""

import math
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictFloat, StrictInt, StrictStr, model_validator

from .models import ColumnSettings, RunRequest, TableSettings


Number = StrictInt | StrictFloat
Scalar = StrictStr | StrictInt | StrictFloat | StrictBool
INTEGER_TYPES = {"INT", "INTEGER", "SMALLINT", "BIGINT", "LONG"}
DECIMAL_TYPES = {"DECIMAL", "FLOAT", "DOUBLE", "REAL"}
TEXT_TYPES = {"TEXT", "VARCHAR", "CHAR", "DATE", "DATETIME", "TIME"}
MAX_SAFE_INTEGER = 9_007_199_254_740_991


def safe_number(value: int | float) -> bool:
    """Reject non-finite and lossy JSON numbers before they reach YAML."""
    return abs(value) <= MAX_SAFE_INTEGER if isinstance(value, int) else math.isfinite(value)


class ProposalModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class TableRows(ProposalModel):
    table: StrictStr = Field(min_length=1)
    rows: StrictInt = Field(ge=0, le=2_000_000)


class NumberRange(ProposalModel):
    table: StrictStr = Field(min_length=1)
    column: StrictStr = Field(min_length=1)
    min: Number
    max: Number

    @model_validator(mode="after")
    def check_bounds(self) -> "NumberRange":
        if not safe_number(self.min) or not safe_number(self.max) or self.min > self.max:
            raise ValueError("Range bounds must be finite safe numbers and min must not exceed max")
        return self


class ValueList(ProposalModel):
    table: StrictStr = Field(min_length=1)
    column: StrictStr = Field(min_length=1)
    values: list[Scalar] = Field(min_length=1)

    @model_validator(mode="after")
    def check_values(self) -> "ValueList":
        if any(isinstance(value, (int, float)) and not safe_number(value) for value in self.values):
            raise ValueError("Value lists must contain finite safe scalars")
        if len({(type(value), value) for value in self.values}) != len(self.values):
            raise ValueError("Value lists must not repeat values")
        return self


class NullRate(ProposalModel):
    table: StrictStr = Field(min_length=1)
    column: StrictStr = Field(min_length=1)
    probability: Number = Field(ge=0, le=1)

    @model_validator(mode="after")
    def check_probability(self) -> "NullRate":
        if not safe_number(self.probability):
            raise ValueError("Null probability must be finite")
        return self


class UnappliedRequest(ProposalModel):
    request: StrictStr = Field(min_length=1)
    reason: Literal[
        "unsupported_distribution", "unsupported_relationship", "unknown_schema_field",
        "ambiguous_request", "conflicting_request",
    ]
    explanation: StrictStr = Field(min_length=1)


class AIPlan(ProposalModel):
    version: Literal[1]
    table_rows: list[TableRows]
    number_ranges: list[NumberRange]
    value_lists: list[ValueList]
    null_rates: list[NullRate]
    assumptions: list[StrictStr]
    unapplied: list[UnappliedRequest]

    @model_validator(mode="after")
    def check_duplicate_rules(self) -> "AIPlan":
        groups = (
            ("table_rows", [item.table for item in self.table_rows]),
            ("number_ranges", [(item.table, item.column) for item in self.number_ranges]),
            ("value_lists", [(item.table, item.column) for item in self.value_lists]),
            ("null_rates", [(item.table, item.column) for item in self.null_rates]),
        )
        for name, keys in groups:
            if len(keys) != len(set(keys)):
                raise ValueError(f"Duplicate entries in {name}")
        ranges = {(item.table, item.column) for item in self.number_ranges}
        values = {(item.table, item.column) for item in self.value_lists}
        if ranges & values:
            raise ValueError("A column cannot have both a number range and a value list")
        return self


class PlanApplicationError(ValueError):
    """A well-formed proposal cannot be applied to this inspected schema."""


def apply_ai_plan(request: RunRequest, plan: AIPlan, inspection: dict[str, Any]) -> RunRequest:
    """Apply only executable proposal settings; leave the caller's request unchanged."""
    inspected = {table["name"]: table for table in inspection["tables"]}
    unknown_existing = set(request.tables) - set(inspected)
    if unknown_existing:
        raise PlanApplicationError("Unknown existing tables: " + ", ".join(sorted(unknown_existing)))

    updated = request.model_copy(deep=True)
    updated.tables = {
        name: updated.tables.get(name, TableSettings(rows=table["rows"]))
        for name, table in inspected.items()
    }

    def column_settings(table_name: str, column_name: str) -> tuple[ColumnSettings, str]:
        table = inspected.get(table_name)
        if table is None:
            raise PlanApplicationError(f"Unknown table: {table_name}")
        column_type = table["column_types"].get(column_name)
        if column_type is None:
            raise PlanApplicationError(f"Unknown column: {table_name}.{column_name}")
        settings = updated.tables[table_name].columns.setdefault(column_name, ColumnSettings())
        return settings, column_type

    for item in plan.table_rows:
        if item.table not in inspected:
            raise PlanApplicationError(f"Unknown table: {item.table}")
        updated.tables[item.table].rows = item.rows

    for item in plan.number_ranges:
        settings, column_type = column_settings(item.table, item.column)
        if column_type in INTEGER_TYPES:
            if not float(item.min).is_integer() or not float(item.max).is_integer():
                raise PlanApplicationError(f"Integer range required for {item.table}.{item.column}")
            minimum, maximum = int(item.min), int(item.max)
        elif column_type in DECIMAL_TYPES:
            minimum, maximum = float(item.min), float(item.max)
        else:
            raise PlanApplicationError(f"Numeric range is not valid for {item.table}.{item.column}")
        settings.minimum, settings.maximum, settings.values = minimum, maximum, None

    for item in plan.value_lists:
        settings, column_type = column_settings(item.table, item.column)
        values = list(item.values)
        if column_type in INTEGER_TYPES:
            if any(isinstance(value, bool) or not isinstance(value, int) for value in values):
                raise PlanApplicationError(f"Integer values required for {item.table}.{item.column}")
        elif column_type in DECIMAL_TYPES:
            if any(isinstance(value, bool) or not isinstance(value, (int, float)) for value in values):
                raise PlanApplicationError(f"Numeric values required for {item.table}.{item.column}")
        elif column_type == "BOOLEAN":
            if any(not isinstance(value, bool) for value in values):
                raise PlanApplicationError(f"Boolean values required for {item.table}.{item.column}")
        elif column_type in TEXT_TYPES:
            if any(not isinstance(value, str) for value in values):
                raise PlanApplicationError(f"Text values required for {item.table}.{item.column}")
        else:
            raise PlanApplicationError(f"Unsupported column type for {item.table}.{item.column}")
        settings.values, settings.minimum, settings.maximum = values, None, None

    for item in plan.null_rates:
        settings, _ = column_settings(item.table, item.column)
        settings.null_probability = float(item.probability)

    return updated
