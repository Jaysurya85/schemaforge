"""Extract explicit settings from a testing request using inspected schema names."""

import re
from dataclasses import dataclass, field
from typing import Any


Target = tuple[str, str]
Number = int | float
NUM = r"-?(?:\d{1,3}(?:,\d{3})+|\d+)(?:\.\d+)?"
ROW_NUM = r"(?:\d{1,3}(?:,\d{3})+|\d+)"
NUMERIC_TYPES = {"INT", "INTEGER", "SMALLINT", "BIGINT", "LONG", "DECIMAL", "FLOAT", "DOUBLE", "REAL"}


@dataclass
class ExplicitSettings:
    ranges: dict[Target, tuple[Number, Number]] = field(default_factory=dict)
    values: dict[Target, list[str | int | float | bool]] = field(default_factory=dict)
    null_rates: dict[Target, float] = field(default_factory=dict)


def singular(name: str) -> str:
    if name.endswith("ies"):
        return name[:-3] + "y"
    return name[:-1] if name.endswith("s") and not name.endswith("ss") else name


def _aliases(name: str) -> set[str]:
    plain = name.replace("_", " ")
    aliases = {name, plain}
    if not plain.endswith("s"):
        aliases.add(plain[:-1] + "ies" if plain.endswith("y") else plain + "s")
    return aliases


def row_counts(goal: str, table: str, ignored_span: tuple[int, int] | None = None) -> set[int]:
    """Recognize counts attached to the singular or plural table name."""
    result: set[int] = set()
    for alias in {table, singular(table)}:
        name = re.escape(alias)
        patterns = (
            rf"(?<![\w.])({ROW_NUM})(?![.\d])\s+(?:rows?\s+(?:of|for|in)\s+)?(?:the\s+)?{name}\b",
            rf"\b{name}\s+(?:with|for|of|at|to)\s+({ROW_NUM})(?![.\d])\s+rows?\b",
            rf"\b(?:set|make|generate|create)\s+(?:the\s+)?{name}\s+(?:table\s+)?(?:row\s+count\s+)?(?:to|at)\s+({ROW_NUM})(?![.\d])\s+rows?\b",
        )
        for pattern in patterns:
            for match in re.finditer(pattern, goal, re.IGNORECASE):
                if ignored_span is None or not (ignored_span[0] <= match.start() and match.end() <= ignored_span[1]):
                    result.add(int(match.group(1).replace(",", "")))
    return result


def _field_mentions(goal: str, inspection: dict[str, Any]) -> list[tuple[int, int, Target]]:
    tables = inspection.get("tables", [])
    owners: dict[str, set[str]] = {}
    for table in tables:
        for column in table["column_types"]:
            for alias in _aliases(column):
                owners.setdefault(alias.lower(), set()).add(table["name"])
    candidates: list[tuple[int, int, Target]] = []
    for table in tables:
        name = table["name"]
        for column in table["column_types"]:
            for alias in _aliases(column):
                for table_alias in {name, singular(name)}:
                    pattern = rf"\b{re.escape(table_alias)}\s*(?:\.\s*|\s+){re.escape(alias)}\b"
                    candidates += [(m.start(), m.end(), (name, column))
                                   for m in re.finditer(pattern, goal, re.IGNORECASE)]
                if owners[alias.lower()] == {name}:
                    candidates += [(m.start(), m.end(), (name, column))
                                   for m in re.finditer(rf"\b{re.escape(alias)}\b", goal, re.IGNORECASE)]
    result: list[tuple[int, int, Target]] = []
    for item in sorted(candidates, key=lambda part: (part[0] - part[1], part[0])):
        if not any(item[0] < end and start < item[1] for start, end, _ in result):
            result.append(item)
    return sorted(result)


def _near(goal: str, start: int, end: int, mentions: list[tuple[int, int, Target]]) -> Target | None:
    before = [(start - finish, target) for _, finish, target in mentions
              if finish <= start and start - finish <= 80 and not re.search(r"[,.;]", goal[finish:start])]
    after = [(begin - end, target) for begin, _, target in mentions
             if begin >= end and begin - end <= 60 and not re.search(r"[,.;]", goal[end:begin])]
    choices = before + after
    return min(choices, key=lambda item: item[0])[1] if choices else None


def _number(raw: str) -> Number:
    value = raw.replace(",", "")
    return float(value) if "." in value else int(value)


def _typed_values(raw: str, kind: str) -> list[str | int | float | bool] | None:
    raw = re.sub(r"^one\s+of\s+", "", raw.strip(), flags=re.IGNORECASE)
    parts = [part.strip().strip("'\"") for part in re.split(r"\s*(?:,|\bor\b|\band\b)\s*", raw) if part.strip()]
    if (not parts or len(parts) > 20 or any(len(part) > 80 for part in parts)
            or any(part.lower() in {"null", "none"} or part.lower().startswith("to null") for part in parts)):
        return None
    try:
        if kind in {"INT", "INTEGER", "SMALLINT", "BIGINT", "LONG"}:
            return [int(part) for part in parts]
        if kind in {"DECIMAL", "FLOAT", "DOUBLE", "REAL"}:
            return [float(part) for part in parts]
        if kind == "BOOLEAN":
            if any(part.lower() not in {"true", "false"} for part in parts):
                return None
            return [part.lower() == "true" for part in parts]
    except ValueError:
        return None
    return parts


def extract_explicit_settings(goal: str, inspection: dict[str, Any]) -> ExplicitSettings:
    """Resolve clear ranges, lists, and null rates; leave ambiguous prose to the model."""
    found = ExplicitSettings()
    mentions = _field_mentions(goal, inspection)
    types = {(table["name"], column): kind for table in inspection.get("tables", [])
             for column, kind in table["column_types"].items()}
    pattern = rf"(?<!\w)(?P<first>{NUM})\s*(?P<join>to|\-|and)\s*(?P<last>{NUM})(?!\w)"
    for match in re.finditer(pattern, goal, re.IGNORECASE):
        if match.group("join").lower() == "and" and not re.search(
            r"\bbetween\s*$", goal[max(0, match.start() - 20):match.start()], re.IGNORECASE
        ):
            continue
        target = _near(goal, match.start(), match.end(), mentions)
        if target is not None and types[target] in NUMERIC_TYPES:
            found.ranges[target] = (_number(match.group("first")), _number(match.group("last")))

    for match in re.finditer(r"\b(\d+(?:\.\d+)?)\s*(?:%|percent)", goal, re.IGNORECASE):
        context = goal[max(0, match.start() - 50):min(len(goal), match.end() + 80)]
        if re.search(r"\bnull\b", context, re.IGNORECASE):
            target = _near(goal, match.start(), match.end(), mentions)
            if target is not None:
                found.null_rates[target] = float(match.group(1)) / 100

    for match in re.finditer(r"\buse\s+only\s+(.+?)\s+for\s+", goal, re.IGNORECASE):
        following = [item for item in mentions if item[0] >= match.end() and item[0] - match.end() <= 5]
        if following:
            target = following[0][2]
            values = _typed_values(match.group(1), types[target])
            if values is not None:
                found.values[target] = values
    for _, end, target in mentions:
        suffix = goal[end:end + 100]
        match = re.match(r"\s+(?:should\s+be|must\s+be|use|values?\s*(?:are|of|to)?|only|to)\s+([^.;]+)",
                         suffix, re.IGNORECASE)
        if match:
            raw = match.group(1)
            next_field = next((begin for begin, _, _ in mentions if begin > end and begin < end + match.end()), None)
            if next_field is not None:
                raw = goal[end + match.start(1):next_field]
                raw = re.sub(r"(?:,\s*)?\band\s*$", "", raw, flags=re.IGNORECASE).rstrip(", ")
            values = _typed_values(raw, types[target])
            if values is not None:
                found.values[target] = values
    return found
