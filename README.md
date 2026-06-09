# SchemaForge

SchemaForge parses a SQL schema, validates table relationships, generates deterministic sample
data, writes SQL `INSERT` statements, and validates the result in SQLite.

## v1 Features

- Parses SQL `CREATE TABLE` schemas and orders tables by foreign-key dependencies.
- Generates deterministic SQL `INSERT` data with seed and row-count configuration.
- Writes an editable YAML config for schema path, output path, benchmark path, SQLite validation,
  default rows, and per-table rows.
- Validates schema, config, and generation feasibility with aggregated errors instead of stopping
  at the first issue.
- Checks duplicate names, PK/UNIQUE/FK correctness, cycles, self-references, unsupported types,
  missing schema/config targets, and row-capacity limits.
- Optionally validates generated SQL in SQLite and writes benchmark metrics.
- Includes a complex marketplace test fixture for multi-table generation.

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
validation time is reported separately.

## Tests

```bash
scripts/run_integration_tests.sh
```

Valid test outputs and benchmark reports are written to `tests/artifacts/`.
