# Users and Orders: 1.1M Rows

Generates 100,000 users and 1,000,000 orders to measure large parent-child generation. Orders
sample valid user primary keys, while unique values and text and decimal checks are generated at
scale.

SQLite validation is enabled. The reported peak process RSS therefore includes the in-memory
SQLite database as well as generation. This case proves that the generated foreign keys and other
constraints remain valid across all 1,100,000 rows.

From the repository root:

```bash
scripts/run_benchmarks.sh users_orders_1m
```

Artifacts are written to `benchmark-results/users_orders_1m.sql`,
`benchmark-results/users_orders_1m.yaml`, and `benchmark-results/users_orders_1m.log`.
