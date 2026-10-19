# AI dataset planner: version 1 contract

This document defines the first AI planning feature. It does not add AI calls or
change generation behavior. The user supplies a SQL `CREATE TABLE` schema and a
plain-English testing goal. The planner proposes a **sparse change set** for the
existing SchemaForge controls; the C++ engine remains responsible for validation
and row generation.

## Boundary

- The model proposes settings. It never writes SchemaForge YAML, SQL schema, or
  generated rows. Python builds the YAML from the CLI's base config.
- A proposal does not change the current controls or start generation. The user
  reviews the proposed changes, unapplied requests, and read-only YAML first.
- Only settings explicitly supported below may enter executable configuration.
  A requested distribution or cross-column business relationship must appear
  under `unapplied`, even if the model can describe it convincingly.
- Manual row counts and rules remain available. If a proposal would replace a
  manual setting, the review must show the old and proposed values.
- AI planning is optional and sends the pasted schema and testing request to a
  hosted provider. Local generation without AI remains available. The API key
  belongs on the server, never in browser code or the repository.

## Proposal shape

The model returns one JSON object with **exactly** these required fields. Empty
arrays are used when a section has no entries; extra fields are rejected.

```json
{
  "version": 1,
  "table_rows": [{ "table": "customers", "rows": 1000 }],
  "number_ranges": [
    { "table": "customers", "column": "age", "min": 18, "max": 75 }
  ],
  "value_lists": [
    { "table": "customers", "column": "status", "values": ["active", "inactive"] }
  ],
  "null_rates": [
    { "table": "customers", "column": "phone", "probability": 0.05 }
  ],
  "assumptions": [],
  "unapplied": [
    {
      "request": "10% of customers should be premium",
      "reason": "unsupported_distribution",
      "explanation": "Weighted value frequencies are not supported by this generator."
    }
  ]
}
```

`table_rows` maps to `tables.<name>.rows`. `number_ranges` maps to per-column
`min` and `max`; both bounds are required. `value_lists` maps to per-column
`values` and contains JSON string, number, or boolean scalars, never `null`.
`null_rates.probability` maps to `null_probability` and is a fraction from 0
through 1. It is a sampling probability, not a promise of an exact percentage
in every dataset. `assumptions` contains short user-facing sentences about choices the
request did not state explicitly. Each `unapplied` entry quotes the relevant
request, names a reason, and explains what was not done.

Allowed `unapplied.reason` values for v1 are `unsupported_distribution`,
`unsupported_relationship`, `unknown_schema_field`, `ambiguous_request`, and
`conflicting_request`. A provider failure or invalid JSON is an error response,
not an `unapplied` item.

The proposal does **not** set dialect, output format, validation method, random
seed, realistic-value mode, schema DDL, or any hidden engine option. Those stay
under existing user controls. It also does not imply that a listed categorical
value will occur at a particular frequency.

## Acceptance gates

1. Parse the response into a strict typed model: version, fields, scalar types,
   finite numbers, nonempty value lists, bounds, row limits, and probability
   bounds. Reject duplicate or incompatible rules for one column.
2. Compare every table and column name with the inspected schema. Check that
   numeric rules target numeric columns and values match their column type.
3. Build YAML by applying the accepted change set to the CLI-created base
   config. Never pass model-authored YAML directly to the engine.
4. Run SchemaForge's own config and generation-feasibility checks before the
   plan is marked ready. They are authoritative for keys, foreign keys, CHECK
   constraints, nullability, and unique capacity. An impossible proposal is
   rejected; it is not presented as a valid plan.
5. Show the user what changed, assumptions, unapplied requests, and the config
   to be executed. After any edit, repeat validation. Generating from an
   accepted plan must use the reviewed configuration rather than rebuilding a
   different one silently.

Passing these gates means the configuration is supported and feasible according
to the engine. It does not prove that the model understood every nuance of the
user's intent; the user review is the final semantic check.

## Scenario fixtures

[`ai-planner-cases.json`](ai-planner-cases.json) records concrete requests and
expected proposals for later model, compiler, and preflight tests. The
`expected_result` field distinguishes a ready proposal from one the C++
preflight must reject. Tests should compare meaningful settings and coverage,
not rely on exact prose in explanations. `unique_capacity_rejected` is a
preflight fixture: a planner that recognizes the conflict before proposing it
may instead place that request under `unapplied`.

The first demo should show a supported multi-table request and at least one
unapplied requirement. Weighted distributions, target correlations, and
application test execution remain future work.
