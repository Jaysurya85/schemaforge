# Final Demo

This demo is a compact end-to-end run for SchemaForge. It uses one e-commerce schema to show
deterministic SQL generation, SQLite validation, CSV output, PostgreSQL COPY output, benchmark
reporting, dependency ordering, single-column and composite keys, composite foreign keys, nullable
columns, typed values, and supported `CHECK` constraints.

From the repository root, build the CLI:

```bash
cmake -S . -B build
cmake --build build
```

Create a fresh editable config from the schema:

```bash
./build/schemaforge init --schema examples/final_demo/schema.sql --config /tmp/schemaforge-final-demo-init.yaml
```

Generate deterministic SQL `INSERT` output and validate it in SQLite:

```bash
./build/schemaforge generate --config examples/final_demo/schemaforge.yaml
```

Artifacts are written to ignored files:

- `examples/final_demo/output.sql`
- `examples/final_demo/benchmark.yaml`

Generate one CSV file per table:

```bash
./build/schemaforge generate --config examples/final_demo/schemaforge_csv.yaml
```

CSV artifacts are written under `examples/final_demo/csv-output/`, with metrics in
`examples/final_demo/csv-benchmark.yaml`. SQLite validation is disabled for CSV because the current
validator executes SQL statements.

Generate PostgreSQL `COPY FROM STDIN` output:

```bash
./build/schemaforge generate --config examples/final_demo/schemaforge_postgres_copy.yaml
```

COPY artifacts are written to `examples/final_demo/postgres-copy.sql`, with metrics in
`examples/final_demo/postgres-copy-benchmark.yaml`. The output is transactional and table blocks are
emitted in foreign-key dependency order.

Optional PostgreSQL Docker validation remains outside the default demo run. To exercise the real
PostgreSQL import path, use the repository script:

```bash
scripts/run_postgres_validation_tests.sh
```

The script exits successfully with a skip message when Docker is unavailable.

## What To Inspect

- `schema.sql` includes `users`, `products`, `orders`, `order_items`, and `payments`.
- `order_items` has a composite primary key and a composite foreign key to `products(sku, seller_id)`.
- `products` has a composite unique key referenced by `order_items`.
- `orders.coupon_code`, `payments.external_reference`, and `payments.paid_at` demonstrate nullable
  output.
- `status`, `payment_method`, `quantity`, `price`, `total_amount`, and `amount` demonstrate
  supported `CHECK` constraints.
- `expected_output_sample.sql` is a deterministic excerpt from the SQL run.
- `expected_benchmark_sample.txt` documents the benchmark fields while avoiding machine-specific
  timing and memory values.
