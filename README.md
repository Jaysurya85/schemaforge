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
make run
```

Or do everything in one command:

```bash
make
```

By default, SchemaForge reads `schema.sql`. You can also pass a schema path:

```bash
./build/schemaforge tests/valid/basic_fk/schema.sql
```

## Tests

```bash
scripts/run_integration_tests.sh
```
