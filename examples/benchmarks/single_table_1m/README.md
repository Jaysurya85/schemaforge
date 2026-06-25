# Single Table: 1M Rows

Generates 1,000,000 `users` rows to measure raw streaming SQL throughput and process memory.
The schema exercises an integer primary key, unique text values, numeric `CHECK` constraints,
`NOT NULL`, decimal generation, and booleans.

SQLite validation is disabled so the peak process RSS primarily reflects parsing and generation,
not an in-memory validation database.

From the repository root:

```bash
scripts/run_benchmarks.sh single_table_1m
```

Artifacts are written to `benchmark-results/single_table_1m.sql`,
`benchmark-results/single_table_1m.yaml`, and `benchmark-results/single_table_1m.log`.
