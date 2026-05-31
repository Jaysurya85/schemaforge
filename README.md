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

```bash
make build
```

Create an editable generation config from a schema:

```bash
./build/schemaforge init --schema schema.sql --config schemaforge.yaml
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
