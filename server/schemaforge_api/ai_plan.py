"""Strict AI proposal contract and deterministic mapping to run settings."""

import math
import re
from dataclasses import dataclass
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictFloat, StrictInt, StrictStr, ValidationError, model_validator

from .explicit_requests import extract_explicit_settings, row_counts, singular
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


@dataclass(frozen=True)
class FanoutRequest:
    parent: str
    child: str
    per_parent: int
    phrase: str
    parent_rows: int | None
    child_rows: int | None
    inferred_parent: bool
    span: tuple[int, int]
    from_current: bool = False


def _mentioned_rows(goal: str, table_name: str, ignored_span: tuple[int, int] | None = None) -> set[int]:
    return row_counts(goal, table_name, ignored_span)


def _singular_table_name(name: str) -> str:
    return singular(name)


def canonicalize_model_targets(plan: AIPlan, inspection: dict[str, Any]) -> AIPlan:
    """Map harmless singular/case/spacing variants to actual schema identifiers."""
    tables = {table["name"]: table for table in inspection["tables"]}
    aliases: dict[str, set[str]] = {}
    for name in tables:
        for alias in {name, singular(name)}:
            aliases.setdefault(alias.lower(), set()).add(name)

    def table_name(raw: str) -> str:
        matches = aliases.get(raw.lower(), set())
        return next(iter(matches)) if len(matches) == 1 else raw

    def column_name(table: str, raw: str) -> str:
        if table not in tables:
            return raw
        choices = tables[table]["column_types"]
        normalized = raw.lower().replace(" ", "_")
        exact = [name for name in choices if name.lower() == normalized]
        if len(exact) == 1:
            return exact[0]
        for prefix in (table.lower(), singular(table).lower()):
            if normalized.startswith(prefix + "."):
                normalized = normalized[len(prefix) + 1:]
            if normalized.startswith(prefix + "_"):
                normalized = normalized[len(prefix) + 1:]
        matches = [name for name in choices if name.lower() == normalized]
        return matches[0] if len(matches) == 1 else raw

    def mapped(item: Any) -> Any:
        table = table_name(item.table)
        changes = {"table": table}
        if hasattr(item, "column"):
            changes["column"] = column_name(table, item.column)
        return item.model_copy(update=changes)

    candidate = plan.model_copy(update={
        "table_rows": [mapped(item) for item in plan.table_rows],
        "number_ranges": [mapped(item) for item in plan.number_ranges],
        "value_lists": [mapped(item) for item in plan.value_lists],
        "null_rates": [mapped(item) for item in plan.null_rates],
    })
    try:
        return AIPlan.model_validate(candidate.model_dump())
    except ValidationError as error:
        raise PlanApplicationError(f"AI plan contains duplicate or conflicting schema targets: {error.errors()[0]['msg']}") from error


def drop_unchanged_model_rules(plan: AIPlan, current: dict[str, Any]) -> AIPlan:
    """Ignore settings merely echoed from the current controls."""
    tables = current.get("tables", {})

    def settings(table: str, column: str) -> dict[str, Any]:
        return tables.get(table, {}).get("columns", {}).get(column, {})

    ranges = [item for item in plan.number_ranges
              if not (settings(item.table, item.column).get("min") == item.min
                      and settings(item.table, item.column).get("max") == item.max)]
    values = [item for item in plan.value_lists
              if settings(item.table, item.column).get("values") != item.values]
    nulls = [item for item in plan.null_rates
             if settings(item.table, item.column).get("null_probability") != item.probability]
    return plan.model_copy(update={"number_ranges": ranges, "value_lists": values, "null_rates": nulls})


def ground_explicit_settings(goal: str, plan: AIPlan, inspection: dict[str, Any]) -> AIPlan:
    """Make unambiguous user settings authoritative over a model proposal."""
    explicit = extract_explicit_settings(goal, inspection)
    conflicting = explicit.ranges.keys() & explicit.values.keys()
    if conflicting:
        targets = ", ".join(f"{table}.{column}" for table, column in sorted(conflicting))
        raise PlanApplicationError(f"Both a range and a value list were requested for {targets}.")
    rows = {item.table: item for item in plan.table_rows}
    fanout = derive_fanout_request(goal, inspection)
    for table in inspection["tables"]:
        counts = row_counts(goal, table["name"], fanout.span if fanout is not None else None)
        if len(counts) == 1:
            count = next(iter(counts))
            if count > 2_000_000:
                raise PlanApplicationError(f"The requested {count:,} {table['name']} exceeds the 2,000,000-row table limit.")
            rows[table["name"]] = TableRows(table=table["name"], rows=count)
    ranges = {(item.table, item.column): item for item in plan.number_ranges}
    values = {(item.table, item.column): item for item in plan.value_lists}
    nulls = {(item.table, item.column): item for item in plan.null_rates}
    try:
        for (table, column), (minimum, maximum) in explicit.ranges.items():
            values.pop((table, column), None)
            ranges[(table, column)] = NumberRange(table=table, column=column, min=minimum, max=maximum)
        for (table, column), requested in explicit.values.items():
            ranges.pop((table, column), None)
            values[(table, column)] = ValueList(table=table, column=column, values=requested)
        for (table, column), probability in explicit.null_rates.items():
            nulls[(table, column)] = NullRate(table=table, column=column, probability=probability)
    except ValidationError as error:
        raise PlanApplicationError(f"The requested column rule is invalid: {error.errors()[0]['msg']}") from error
    return plan.model_copy(update={
        "table_rows": list(rows.values()), "number_ranges": list(ranges.values()),
        "value_lists": list(values.values()), "null_rates": list(nulls.values()),
    })


def derive_fanout_request(goal: str, inspection: dict[str, Any],
                          current: dict[str, Any] | None = None) -> FanoutRequest | None:
    """Recognize simple 'each parent has N children' requests for count checks."""
    names = [table["name"] for table in inspection.get("tables", [])]
    words = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5}
    for parent in names:
        singular = _singular_table_name(parent)
        for child in names:
            if child == parent:
                continue
            count = r"(?P<count>\d[\d,]*|one|two|three|four|five)"
            patterns = (
                rf"\b(?:each|every)\s+{re.escape(singular)}\s+"
                rf"(?:should\s+)?(?:have|has|gets?|with)\s+{count}\s+{re.escape(child)}\b",
                rf"\b{count}\s+{re.escape(child)}\s+per\s+{re.escape(singular)}\b",
            )
            match = next((found for pattern in patterns
                          if (found := re.search(pattern, goal, re.IGNORECASE)) is not None), None)
            if match is None:
                continue
            raw_count = match.group("count").lower()
            per_parent = words[raw_count] if raw_count in words else int(raw_count.replace(",", ""))
            if per_parent < 1:
                continue
            parent_mentions = _mentioned_rows(goal, parent, match.span())
            parent_rows = next(iter(parent_mentions)) if len(parent_mentions) == 1 else None
            inferred = False
            if parent_rows is None and not any(_mentioned_rows(goal, name, match.span()) for name in names):
                # An unlabeled count next to this relationship describes the parent
                # population. Explicit "total rows" remains ambiguous and is not inferred.
                bare = {int(item.group(1).replace(",", "")) for item in
                        re.finditer(r"\b(\d[\d,]*)\s+rows?\b", goal, re.IGNORECASE)}
                if len(bare) == 1 and not re.search(r"\b(?:total|combined|overall)\b", goal, re.IGNORECASE):
                    parent_rows = next(iter(bare))
                    inferred = True
            from_current = False
            if parent_rows is None and current is not None:
                settings = current.get("tables", {}).get(parent, {})
                inspected = next(table for table in inspection["tables"] if table["name"] == parent)
                parent_rows = settings.get("rows", inspected["rows"])
                from_current = True
            child_rows = parent_rows * per_parent if parent_rows is not None else None
            return FanoutRequest(parent, child, per_parent, match.group(), parent_rows, child_rows,
                                 inferred, match.span(), from_current)
    return None


def normalize_fanout_plan(goal: str, plan: AIPlan, inspection: dict[str, Any],
                          current: dict[str, Any]) -> AIPlan:
    """Use the editable current count when a per-parent request gives no size."""
    fanout = derive_fanout_request(goal, inspection, current)
    if (fanout is None or not fanout.from_current or fanout.child_rows is None
            or fanout.child_rows > 2_000_000):
        return plan
    rows = [item for item in plan.table_rows if item.table not in {fanout.parent, fanout.child}]
    rows.extend([TableRows(table=fanout.parent, rows=fanout.parent_rows),
                 TableRows(table=fanout.child, rows=fanout.child_rows)])
    return plan.model_copy(update={"table_rows": rows})


def drop_unrequested_zero_null_rates(goal: str, plan: AIPlan, inspection: dict[str, Any]) -> AIPlan:
    """A model's zero-rate defaults add no constraint and can invalidate required columns."""
    requested = extract_explicit_settings(goal, inspection).null_rates
    rates = [item for item in plan.null_rates
             if item.probability != 0 or (item.table, item.column) in requested]
    return plan.model_copy(update={"null_rates": rates})


def explain_unsupported_fanout(goal: str, plan: AIPlan, inspection: dict[str, Any],
                               current: dict[str, Any] | None = None) -> AIPlan:
    """Replace model-authored relationship excuses with the engine's actual limit."""
    request = derive_fanout_request(goal, inspection, current)
    if request is None:
        return plan
    singular = _singular_table_name(request.parent)
    matching = [item for item in plan.unapplied if
                re.search(rf"\b{re.escape(singular)}\b", item.request, re.IGNORECASE) and
                re.search(rf"\b{re.escape(request.child)}\b", item.request, re.IGNORECASE)]
    remaining = [item for item in plan.unapplied if item not in matching]
    explanation = (f"SchemaForge samples valid {singular} IDs for {request.child} and cannot guarantee "
                   f"exactly {request.per_parent} {request.child} for every {singular}.")
    if request.child_rows is not None:
        explanation += (f" {request.child_rows:,} {request.child} rows gives an average of "
                        f"{request.per_parent} per {singular}, not an exact count.")
    remaining.append(UnappliedRequest(request=request.phrase, reason="unsupported_relationship",
                                      explanation=explanation))
    assumptions = list(plan.assumptions)
    if request.inferred_parent:
        assumptions.insert(0, f"Interpreted the row count as {request.parent_rows:,} {request.parent}.")
    if request.from_current:
        assumptions.insert(0, f"Used the current {request.parent_rows:,} {request.parent} setting because no count was specified.")
    return plan.model_copy(update={"unapplied": remaining, "assumptions": assumptions})


def check_explicit_requests(goal: str, plan: AIPlan, inspection: dict[str, Any],
                            current: dict[str, Any] | None = None) -> None:
    """Fail closed when a proposal contradicts simple numeric user instructions."""
    errors: list[str] = []
    explicit = extract_explicit_settings(goal, inspection)
    proposed_ranges = {(item.table, item.column): (item.min, item.max) for item in plan.number_ranges}
    proposed_values = {(item.table, item.column): item.values for item in plan.value_lists}
    proposed_nulls = {(item.table, item.column): item.probability for item in plan.null_rates}
    for target, bounds in explicit.ranges.items():
        if proposed_ranges.get(target) != bounds:
            errors.append(f"AI plan did not preserve the requested range for {target[0]}.{target[1]}.")
    for target, values in explicit.values.items():
        if proposed_values.get(target) != values:
            errors.append(f"AI plan did not preserve the requested values for {target[0]}.{target[1]}.")
    for target, rate in explicit.null_rates.items():
        if target not in proposed_nulls or not math.isclose(proposed_nulls[target], rate):
            errors.append(f"AI plan did not preserve the requested null rate for {target[0]}.{target[1]}.")
    for item in plan.number_ranges:
        if (item.table, item.column) in explicit.ranges:
            continue
        if explicit.ranges:
            errors.append(f"AI plan invented a numeric range for {item.table}.{item.column}.")
            continue
        minimum, maximum = re.escape(format(item.min, "g")), re.escape(format(item.max, "g"))
        pair = rf"(?:\b{minimum}\s*(?:to|-)\s*{maximum}\b|\bbetween\s+{minimum}\s+(?:and|to)\s+{maximum}\b)"
        if not re.search(pair, goal, re.IGNORECASE):
            errors.append(f"AI plan invented a numeric range for {item.table}.{item.column}.")
    for item in plan.value_lists:
        if explicit.values and (item.table, item.column) not in explicit.values:
            errors.append(f"AI plan invented a value list for {item.table}.{item.column}.")
            continue
        for value in item.values:
            rendered = format(value, "g") if isinstance(value, (int, float)) and not isinstance(value, bool) else str(value)
            if not re.search(rf"(?<!\w){re.escape(rendered)}(?!\w)", goal, re.IGNORECASE):
                errors.append(f"AI plan invented a value for {item.table}.{item.column}.")
                break
    for match in re.finditer(r"\b\d+(?:\.\d+)?\s*%", goal):
        context = re.split(r"\.\s+", goal[match.end():match.end() + 80], maxsplit=1)[0]
        if re.search(r"\bnull\b", context, re.IGNORECASE):
            continue
        percentage = match.group().strip()
        if not any(item.reason == "unsupported_distribution" and percentage in item.request
                   for item in plan.unapplied):
            errors.append(f"AI plan did not mark the requested {percentage} distribution as unsupported.")

    proposed_rows = {item.table: item.rows for item in plan.table_rows}
    fanout = derive_fanout_request(goal, inspection, current)
    derived_counts = ({fanout.parent: fanout.parent_rows, fanout.child: fanout.child_rows}
                      if fanout is not None and fanout.parent_rows is not None else {})
    if fanout is not None and fanout.child_rows is not None and fanout.child_rows > 2_000_000:
        errors.append(f"The requested {fanout.child_rows:,} {fanout.child} exceeds the 2,000,000-row table limit.")
    matched_nulls: set[tuple[str, str]] = set(explicit.null_rates)
    for table in inspection["tables"]:
        name = table["name"]
        counts = _mentioned_rows(goal, name, fanout.span if fanout is not None else None)
        if len(counts) > 1:
            errors.append(f"Conflicting row counts requested for {name}.")
        expected = derived_counts.get(name, next(iter(counts), None))
        if counts and name in derived_counts and next(iter(counts)) != derived_counts[name]:
            errors.append(f"Requested {name} row count conflicts with the per-parent relationship.")
        proposed = proposed_rows.get(name)
        if expected is not None and proposed != expected:
            errors.append(f"AI plan did not preserve the requested {expected} rows for {name}.")
        current_rows = (current or {}).get("tables", {}).get(name, {}).get("rows", table["rows"])
        if expected is None and proposed is not None and proposed != current_rows:
            errors.append(f"AI plan invented a row count for {name}.")

        for column in table["column_types"]:
            field = re.escape(column).replace("_", r"[ _]")
            pattern = rf"\b(\d+(?:\.\d+)?)\s*%\s+of\s+(?:the\s+)?{field}\b(?:\s+\w+){{0,3}}\s+null\b"
            matches = list(re.finditer(pattern, goal, re.IGNORECASE))
            if not matches:
                continue
            matched_nulls.add((name, column))
            expected_rate = float(matches[-1].group(1)) / 100
            actual_rate = proposed_nulls.get((name, column))
            if actual_rate is None or not math.isclose(actual_rate, expected_rate):
                errors.append(
                    f"AI plan did not preserve the requested {matches[-1].group(1)}% null rate for {name}.{column}.")
    for table_name, column_name in proposed_nulls.keys() - matched_nulls:
        errors.append(f"AI plan invented a null rate for {table_name}.{column_name}.")
    if errors:
        raise PlanApplicationError(" ".join(errors))


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
