# SchemaForge

SchemaForge parses a SQL schema, validates table relationships, generates deterministic sample
data, writes SQL `INSERT` statements, and validates the result in SQLite.

## Clone

Using SSH:

```bash
git clone --recurse-submodules git@github.com:Jaysurya85/schemaforge.git
cd schemaforge
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build and Run

Install the YAML C++ development package before building on a fresh system.

Ubuntu:

```bash
sudo apt install libyaml-cpp-dev
```

Arch:

```bash
sudo pacman -S yaml-cpp
```

Build the bundled SQL parser library:

```bash
make -C external/sql-parser
```

```bash
make build
```

Create an editable generation config from a schema:

```bash
./build/schemaforge init --schema tests/valid/basic_fk/schema.sql --config schemaforge.yaml
```

Generate SQL INSERT statements from the config:

```bash
./build/schemaforge generate --config schemaforge.yaml
```

The generated config stores the schema path, seed, default row count, output file, benchmark file,
SQLite validation setting, and per-table row counts. Edit `schemaforge.yaml` before running
`generate` to change table row counts or output settings.

Useful init options:

```bash
./build/schemaforge init --schema tests/valid/basic_fk/schema.sql --config schemaforge.yaml --seed 42 --default-rows 10
```

`generate` writes INSERT statements to `output.sql` and benchmark metrics to `benchmark.yaml`.
Generation throughput is calculated from generated rows divided by generation time only; SQLite
validation time is reported separately. On Linux, the benchmark also reports peak process memory
usage. Output file size is reported on every supported platform.

### CSV Output

Set the output format and directory in the generation config:

```yaml
output:
  format: csv
  directory: output/
```

Then run the normal generation command:

```bash
./build/schemaforge generate --config schemaforge.yaml
```

Each table is streamed to its own file, such as `output/users.csv` and `output/orders.csv`. Files
include a header in schema column order, and rows retain generation order. Missing directories are
created automatically; `output.directory` is required for CSV configs.

Text containing commas, quotes, CR, newlines, or surrounding whitespace is quoted, and embedded
quotes are doubled. NULL is an empty field while an empty text value is written as `""`. Booleans
use `true`/`false`, decimals use two fractional digits, and date/time values use the same padded
formats as SQL output.

SQLite validation is skipped for CSV because the current validator executes SQL statements. The
benchmark still reports rows, throughput, memory, and the combined size of all generated CSV files.

### PostgreSQL COPY Output

Use `postgres_copy` to create one self-contained data file for `psql`:

```yaml
output:
  format: postgres_copy
  file: output.copy.sql
```

Generate and import it with:

```bash
./build/schemaforge generate --config schemaforge.yaml
psql --dbname your_database --file output.copy.sql
```

The target tables must already exist. The generated file wraps all tables in one transaction and
emits `COPY ... FROM STDIN` blocks in foreign-key dependency order. Rows use PostgreSQL text COPY
format: fields are tab-separated, NULL is `\N`, and backslashes and control characters are escaped.
Table and column identifiers are double-quoted.

PostgreSQL COPY output streams rows directly to one file and retains deterministic seed behavior.
SQLite validation is skipped because COPY syntax is PostgreSQL-specific; benchmark rows,
throughput, memory, and output size are still reported.

### PostgreSQL Docker Validation

PostgreSQL validation is opt-in for CSV and `postgres_copy` output:

```yaml
validation:
  sqlite: false
  postgres: true
```

SchemaForge starts a temporary `postgres:17-alpine` container without publishing a host port,
executes the schema, imports the generated data, and verifies every configured table row count.
PostgreSQL enforces primary keys, unique constraints, checks, and foreign keys during the import.
The container is removed after validation.

If Docker is missing, inaccessible, or unresponsive, generation succeeds and the benchmark reports
`postgres: unavailable`. Schema or import errors report `postgres: failed` and fail the command.
The schema must use PostgreSQL-compatible DDL; compatibility errors from PostgreSQL are returned in
the validation output.

Run the real Docker import tests for both COPY and CSV with:

```bash
scripts/run_postgres_validation_tests.sh
```

The script exits successfully with a skip message when Docker is unavailable.

## Tests

```bash
scripts/run_integration_tests.sh
```

Valid test outputs and benchmark reports are written to `tests/artifacts/`.

## Large-Scale Benchmarks

Build SchemaForge, then run all benchmark examples from the repository root:

```bash
make build
scripts/run_benchmarks.sh
```

Run one or more selected cases by name:

```bash
scripts/run_benchmarks.sh single_table_1m
scripts/run_benchmarks.sh users_orders_1m ecommerce_large
```

The examples cover one million rows in a single table, 1.1 million parent-child rows with SQLite
foreign-key validation, and a 1.61 million-row e-commerce dependency graph. The runner prints row
count, generation time, throughput, output size, peak process memory, and SQLite status. Detailed
schemas, configs, and notes live under `examples/benchmarks/`.

Generated SQL, reports, and logs are retained under `benchmark-results/` and are ignored by Git.
Running every case can use several hundred MiB of disk space. Remove the artifacts with:

```bash
rm -rf benchmark-results
```

Peak memory is process-wide RSS. For `users_orders_1m`, it includes the in-memory SQLite validation
database; the other two cases disable SQLite so their memory figures focus on generation.

## Supported Features

- Parses SQL `CREATE TABLE` schemas.
- Supports `PRIMARY KEY`, `UNIQUE`, and `FOREIGN KEY` constraints.
- Supports single-column and composite primary keys.
- Supports single-column and composite unique constraints.
- Supports foreign keys that reference primary-key or unique columns.
- Orders tables by foreign-key dependencies before generating rows.
- Generates deterministic SQL `INSERT` data from a seed.
- Supports configurable default row counts and per-table row counts.
- Writes an editable YAML config for schema path, output path, benchmark path, SQLite validation,
  default rows, and per-table rows.
- Generates values for `INT`, `SMALLINT`, `BIGINT`, `TEXT`, `VARCHAR`, `CHAR`, `DECIMAL`,
  `FLOAT`, `DOUBLE`, `REAL`, `BOOLEAN`, `DATE`, `DATETIME`, and `TIME`.
- Supports simple `CHECK` constraints for numeric ranges, `BETWEEN`, numeric `IN`, and text `IN`.
- Optionally validates generated SQL in SQLite.
- Streams one headered CSV file per table with typed value formatting and CSV escaping.
- Streams transactional PostgreSQL `COPY FROM STDIN` output for high-throughput imports.
- Writes benchmark metrics for generated rows, generation time, throughput, output file size,
  validation time, total command time, and Linux peak process memory usage.
- Includes a complex marketplace test fixture for multi-table generation.

## Validation Coverage

SchemaForge validates schema, config, and generation feasibility with aggregated errors instead of
stopping at the first issue.

- Missing schema paths and missing schema files.
- Known unsupported column types, including `JSON`, `UUID`, `ARRAY`, `BLOB`, `ENUM`, and `INET`.
- Duplicate table names and duplicate column names.
- Primary-key constraints that reference missing columns.
- Unique constraints that reference missing columns.
- Foreign keys with missing local columns, missing referenced tables, or missing referenced columns.
- Foreign keys with mismatched local and referenced column counts.
- Foreign keys with mismatched local and referenced column types.
- Foreign keys that do not reference a `PRIMARY KEY` or `UNIQUE` constraint.
- Dependency cycles between tables.
- Self-referencing foreign keys, which are not supported yet.
- Unsupported generation types.
- Unsupported `CHECK` constraint expressions.
- Primary-key generation types outside `INT`, `BIGINT`, `SMALLINT`, `TEXT`, `VARCHAR`, and `CHAR`.
- Foreign-key generation types outside `INT`, `BIGINT`, `SMALLINT`, `TEXT`, and `VARCHAR`.
- YAML config entries that reference unknown tables.
- Child tables that request rows while a referenced parent table has 0 rows.
- Row-count requests that exceed finite unique capacities, including `BOOLEAN`, `CHAR(n)`, limited
  `CHECK` domains, composite unique domains, and unique foreign keys.
