# Architecture

SchemaForge is a config-driven data generator for relational SQL schemas. It parses `CREATE TABLE`
DDL, validates schema and config feasibility, builds a dependency-ordered generation plan, streams
typed rows, writes the selected output format, and optionally validates generated data against a
database.

## Pipeline

The main pipeline is:

1. SQL schema file
2. `ParserAdapter`
3. Schema model
4. Schema, config, and generation validation
5. Dependency graph and topological table ordering
6. Capacity analysis and key-source planning
7. Row streaming
8. SQL, CSV, or PostgreSQL COPY writer
9. Optional SQLite or PostgreSQL Docker validation
10. Benchmark report

`init` runs the front half of this pipeline and writes an editable YAML config. `generate` reads
that config and runs the full pipeline.

## Parser Adapter

The bundled Hyrise SQL parser provides the initial parse tree. `ParserAdapter` isolates the rest of
the codebase from parser-specific types by converting Hyrise column types, constraints, table
definitions, foreign-key specs, and supported `CHECK` expressions into SchemaForge domain objects.
This adapter also preprocesses `CHECK` clauses that are useful for generation but awkward to consume
directly from the parser.

Keeping parser logic behind an adapter makes the schema model stable even if parser internals or
supported SQL syntax change later.

## Schema Model

The schema model centers on `Table`, `Column`, `TableConstraint`, `ForeignKeySpec`,
`ForeignKey`, and `ColumnCheckConstraint`.

Columns carry normalized type information such as data type, length, precision, and scale. Table
constraints represent primary keys, unique constraints, nullability, and foreign keys. After parse,
foreign-key specs are resolved into `ForeignKey` objects that point at actual local and referenced
columns, which lets generation and validation work with direct model references instead of repeated
name lookups.

## Validation Stages

Validation is intentionally staged:

- Schema-file validation checks for missing or unreadable schema files and known unsupported types.
- Schema validation checks parseable constraints, duplicate names, missing referenced tables or
  columns, type mismatches, unsupported checks, unsupported key-generation types, dependency cycles,
  and unsupported self references.
- Config validation checks unknown tables or columns, type-compatible configured values, min/max
  ranges, nullability rules, and local foreign-key config restrictions.
- Generation validation checks row-count feasibility against finite domains, unique constraints,
  composite key capacity, and parent/child foreign-key row-count requirements.

Errors are aggregated where practical so users can fix related issues in one pass.

## Dependency Graph

`DependencyGraph` builds table dependencies from resolved foreign keys. A topological sort produces
the generation order, ensuring parent tables are generated before child tables. That order is used
by all output formats so foreign-key references point to already registered keys.

Cycles and self-references are rejected today because they require a different insert/update
strategy.

## Planning And Capacity

`CapacityAnalyzer` estimates finite domains before generation, especially for unique booleans,
limited `CHAR(n)` keys, `CHECK IN` values, bounded numeric checks, and composite key combinations.
Generation validation uses that analysis to reject impossible row counts before any output file is
opened.

The generation plan combines schema constraints, SQL checks, YAML column controls, realistic
heuristics, and default typed generators. Configured values and ranges are validated before use.
SQL constraints take precedence over generic name-based generation.

## Keys And Foreign Keys

`KeyRegistry` is the bridge between parent key generation and child foreign-key sampling. It
registers primary-key and unique key sources, including composite keys, and exposes deterministic
row-indexed keys plus seeded random key sampling.

Single-column integer keys use compact sequential values. Text-like keys use stable pattern sources
such as email-style or table-key values. Composite keys register tuple sources so child rows can
copy a complete referenced tuple instead of independently sampling each column.

## Typed Values

Generated values are represented as `GeneratedValue`, a typed variant covering integers, numeric
values, booleans, text, dates, times, datetimes, and NULL. Writers format these typed values for
their target output instead of parsing strings back into types.

This keeps formatting decisions localized:

- SQL writers emit quoted identifiers, single-quoted strings, `NULL`, booleans, decimals, and
  padded date/time values.
- CSV writers emit headers, escaped fields, empty fields for NULL, and typed scalar formatting.
- PostgreSQL COPY writers emit transactional `COPY FROM STDIN` blocks, tab-separated text format,
  `\N` for NULL, and PostgreSQL escape sequences.

## Streaming Output

The main generation path streams `GeneratedRow` records to a consumer. SQL, CSV, and COPY outputs
therefore do not need to retain the full generated dataset in memory. SQLite validation is the main
path that can materially increase process memory because it imports generated SQL into an in-memory
database.

## Benchmarking

Each generate run writes a benchmark YAML report with configured row counts, total rows, generation
time, throughput, output size, validation status and timing, total command time, and peak process
memory when available on the platform. Throughput is based on generation time only, while validation
time is reported separately.

## Current Limitations

- Only `CREATE TABLE` schema input is supported.
- Generation supports a practical subset of SQL types and simple `CHECK` constraints.
- Cyclic and self-referencing foreign keys are rejected.
- SQLite validation is limited to SQL `INSERT` output.
- PostgreSQL validation requires Docker and is optional.
- The PostgreSQL COPY file assumes target tables already exist.
- Cross-table semantic consistency beyond foreign keys is intentionally limited.

## Future Work

Likely next steps include broader SQL dialect support, richer check-expression support, cyclic
relationship strategies, configurable distributions, stronger multi-table semantic modeling, and
database validators that do not depend on Docker.
