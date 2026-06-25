# E-commerce: 1.61M Rows

Generates 100,000 users, 10,000 products, 500,000 orders, and 1,000,000 order items. This case
measures dependency-ordered generation across two relationship levels and two foreign keys on the
largest child table. It also exercises primary keys, unique values, numeric and text checks,
decimals, and booleans.

SQLite validation is disabled so the reported peak process RSS focuses on streaming generation.
The reduced-row integration smoke test exercises the same schema and configuration through the
real CLI.

From the repository root:

```bash
scripts/run_benchmarks.sh ecommerce_large
```

Artifacts are written to `benchmark-results/ecommerce_large.sql`,
`benchmark-results/ecommerce_large.yaml`, and `benchmark-results/ecommerce_large.log`.
