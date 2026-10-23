# SchemaForge

A C++ relational test-data generator with a local web workspace and HTTP API. SchemaForge reads SQL `CREATE TABLE` definitions, checks supported constraints, generates repeatable data, streams SQL/CSV/PostgreSQL COPY output, and can validate the result with SQLite or PostgreSQL.

Use the engine directly from the command line or through the React website and FastAPI server. An optional, early-stage AI planner can propose row counts and column rules from a plain-English test goal. The user reviews the resulting controls and YAML before the C++ engine generates anything.

SchemaForge is an educational systems project exploring parser integration, domain modeling, constraint validation, dependency ordering, deterministic generation, streaming output, database-backed checks, and benchmark reporting.

| Area | Status |
|---|---|
| C++ engine, CLI, output writers, and database validation | Built |
| Local website and FastAPI interface | Built |
| AI-assisted configuration planning and review | Prototype; planning accuracy has not been measured |
| Exact per-parent counts, distributions, and test-goal verification | Proposed |

## Table of contents
-   [Architecture](#architecture)
-   [Features](#features)
-   [Stack and dependencies](#stack-and-dependencies)
-   [Project structure](#project-structure)
-   [Build](#build)
-   [Web workspace and HTTP API](#web-workspace-and-http-api)
-   [AI planning and review](#ai-planning-and-review)
-   [Testing](#testing)
-   [Quick demo](#quick-demo)
-   [Configuration](#configuration)
-   [Output formats](#output-formats)
-   [Validation](#validation)
-   [Generation model](#generation-model)
-   [Benchmarks](#benchmarks)
-   [Supported SQL and types](#supported-sql-and-types)
-   [Troubleshooting](#troubleshooting)
-   [Limitations and future work](#limitations-and-future-work)
-   [Extending the codebase](#extending-the-codebase)
-   [Statistics](#statistics)
-   [License and references](#license-and-references)

## Architecture

![SchemaForge architecture showing the built C++ engine, local website and server, prototype AI planning, and proposed capabilities](docs/images/schemaforge-architecture.png)

The diagram shows the intended system. Labels distinguish built components, the early AI prototype, and proposed capabilities.

### Components

-   **Web workspace** — paste a schema, inspect tables, adjust settings, review a plan, generate data, and download the result.
-   **FastAPI server** — handles browser and HTTP requests, builds validated YAML, and invokes the C++ CLI. The browser does not run C++ directly.
-   **Optional AI planner** — a hosted model proposes a structured change set; Python checks it against the schema and stated goal, then prepares a config for human review. It does not generate rows or write executable YAML directly.
-   **CLI** — `init` creates editable configs; `generate` runs the generation pipeline independently of the website.
-   **Parser adapter** — wraps Hyrise SQL Parser and converts parser output into SchemaForge domain objects.
-   **Schema model** — represents tables, columns, SQL types, keys, unique constraints, foreign keys, nullability, and checks.
-   **Config model** — loads generation settings, output settings, validation settings, row counts, and column controls.
-   **Validation** — validates schema references, type compatibility, config correctness, constraint support, and generation feasibility.
-   **Dependency graph** — orders tables by foreign-key dependencies.
-   **Generation plan** — prepares table/column generation based on constraints and config.
-   **Typed generators** — generate deterministic values for supported SQL types.
-   **Output writers** — stream rows to SQL INSERT, CSV, or PostgreSQL COPY.
-   **Database validation** — optionally imports generated output into SQLite or PostgreSQL.
-   **Benchmark engine** — records rows, timing, throughput, output size, validation time, and memory usage.

### Design choices

-   **Parser boundary**: external parser structures are converted once into internal C++ objects instead of leaking parser-specific types across the codebase.
-   **Domain model first**: generation operates on SchemaForge's own table, column, type, and constraint model.
-   **Dependency-first generation**: parent tables are generated before child tables by topologically ordering the foreign-key graph.
-   **Deterministic output**: seed-based generation makes results reproducible across runs.
-   **Streaming writers**: large datasets are written row-by-row instead of requiring all rows to be buffered before output.
-   **Validation before side effects**: unsupported schemas and impossible configs are rejected before output files are opened.
-   **Database-backed correctness**: generated data can be imported into real databases so real constraints verify correctness.
-   **Review before generation**: AI suggestions are checked against schema names, supported settings, explicit requests, and C++ feasibility checks. The user sees the controls, assumptions, unapplied requests, and exact YAML before generating.

## Features

### Schema parsing and relationship modeling

-   Parses SQL CREATE TABLE schemas.
-   Uses the vendored Hyrise SQL Parser as the SQL parsing frontend.
-   Converts parser output into SchemaForge's internal C++ domain model.
-   Models tables, columns, SQL types, primary keys, unique constraints, checks, nullable columns, and foreign keys.
-   Supports single-column and composite primary keys.
-   Supports single-column and composite unique constraints.
-   Supports foreign keys that reference primary-key or unique columns.
-   Builds a foreign-key dependency graph and generates parent tables before dependent child tables.
-   Rejects invalid schemas and impossible generation configurations before output is written.

### Constraint-aware generation

-   Generates deterministic data from a configurable seed.
-   Supports default row counts and per-table row counts.
-   Supports per-column numeric ranges, fixed value lists, and null probabilities.
-   Supports nullable single-column foreign keys.
-   Supports composite foreign-key null behavior by making the complete local tuple NULL together.
-   Registers generated parent primary-key / unique-key values as key sources.
-   Samples child foreign keys from valid parent key sources instead of generating unrelated random values.
-   Performs finite-domain capacity checks for unique constraints.
-   Supports simple CHECK constraints:
    -   numeric ranges
    -   BETWEEN
    -   numeric IN
    -   text IN

### Output, validation, and benchmarking

-   Streams SQL INSERT output.
-   Streams one headered CSV file per table.
-   Streams transactional PostgreSQL COPY FROM STDIN output.
-   Optionally validates generated SQL in SQLite.
-   Optionally validates SQL, CSV, and PostgreSQL COPY output using a temporary PostgreSQL Docker container.
-   Writes benchmark metrics for rows, generation time, throughput, output size, validation time, total command time, and Linux peak RSS memory.

### Local website, API, and planning prototype

-   Inspects pasted `CREATE TABLE` statements and shows discovered tables and columns.
-   Lets users set row counts, numeric ranges, allowed values, and null probabilities through controls; SQL is the default output.
-   Offers SQL INSERT, CSV, and PostgreSQL COPY output with previews, downloads, and validation results.
-   Uses a dark theme by default, with a light-theme toggle.
-   Exposes the same local workflow through FastAPI for HTTP clients.
-   Optionally proposes settings from a plain-English test goal, applies them to the visible controls, and shows a read-only YAML preview.
-   Lets users edit the proposed controls or undo AI changes; edits trigger a fresh config preview and validation before generation.
-   Shows assumptions and unsupported requests instead of silently claiming to satisfy them.

## Stack and dependencies


| Component | Details |
|---|---|
| Engine and CLI | C++ and CMake; `schemaforge` binary |
| SQL parsing | Hyrise SQL Parser Git submodule under `external/sql-parser` |
| Configuration | YAML via yaml-cpp |
| HTTP server and AI plan checks | Python, FastAPI, Pydantic |
| Web workspace | React, TypeScript, Vite |
| Optional AI providers | UF NaviGator AI Toolkit or OpenAI API; server-side key required |
| SQLite validation | SQLite runtime and development headers |
| PostgreSQL validation | Docker and temporary `postgres:17-alpine` container |
| Primary platforms | Linux, Arch/Manjaro, Ubuntu/Debian, macOS |

## Project structure

```
schemaforge/
├── CMakeLists.txt
├── Makefile
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── ai-planner-contract.md
│   └── images/schemaforge-architecture.png
├── examples/
│   ├── final_demo/                 # End-to-end demo configs, schema, output samples, benchmarks
│   └── benchmarks/                 # Large benchmark fixtures
│       ├── single_table_1m/
│       ├── users_orders_1m/
│       └── ecommerce_large/
├── external/
│   └── sql-parser/                 # Hyrise SQL Parser submodule
├── include/
│   └── schemaforge/
│       ├── benchmark/              # Benchmark reporting interfaces
│       ├── config/                 # YAML generation config model
│       ├── domain/                 # Column/domain resolution
│       ├── generator/              # Typed value generation and generation plans
│       ├── graph/                  # Dependency graph / ordering logic
│       ├── io/                     # File and stream helpers
│       ├── output/                 # SQL, CSV, PostgreSQL COPY writers
│       ├── parser/                 # Parser adapter boundary
│       ├── schema/                 # Table, column, type, and constraint model
│       └── validation/             # Schema/config/database validation
├── src/
│   ├── benchmark/
│   │   └── BenchmarkEngine.cpp
│   ├── config/
│   │   └── GenerationConfig.cpp
│   ├── domain/
│   │   └── ColumnDomainResolver.cpp
│   └── generator/
│       ├── BooleanGenerator.cpp
│       ├── DecimalGenerator.cpp
│       ├── GenerationPlan.cpp
│       └── IntGenerator.cpp
├── scripts/
│   ├── run_benchmarks.sh
│   ├── run_integration_tests.sh
│   └── run_postgres_validation_tests.sh
├── server/                        # FastAPI interface, config compiler, AI plan checks
├── web/                           # React and TypeScript workspace
├── benchmark-results/              # Generated benchmark artifacts, ignored by Git
├── schemaforge.yaml                # Example/generated config
├── benchmark.yaml                  # Example/generated benchmark report
└── output.sql                      # Example/generated SQL output

```

Generated directories such as `build/`, `benchmark-results/`, `.cache/`, and `.git/` are not part of the source design.

## Build

Clone the repository with submodules:

```bash
git clone --recurse-submodules git@github.com:Jaysurya85/schemaforge.git
cd schemaforge

```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive

```

Install system dependencies before configuring CMake.

**Ubuntu / Debian**

```bash
sudo apt update
sudo apt install cmake g++ make libyaml-cpp-dev sqlite3 libsqlite3-dev

```

**Arch / Manjaro**

```bash
sudo pacman -S cmake gcc make yaml-cpp sqlite

```

**macOS with Homebrew**

```bash
brew install cmake yaml-cpp sqlite

```

The website also needs Python 3.11 or newer, Node.js, and pnpm. On macOS,
`make setup` installs missing Homebrew build tools and Node/pnpm; install Python
separately if needed. On Linux, install Node.js and pnpm before running that
target. Docker is optional and needed only for PostgreSQL validation.

Configure and build:

```bash
cmake -S . -B build
cmake --build build

```

CMake builds the bundled Hyrise SQL Parser library before linking the schemaforge binary.

For a fully clean rebuild:

```bash
rm -rf build
cmake -S . -B build
cmake --build build

```

Optional install:

```bash
cmake --install build
schemaforge --help

```

Install somewhere writable without sudo:

```bash
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/schemaforge" --help

```

## Web workspace and HTTP API

The local website uses the same C++ CLI as the command-line workflow. FastAPI
accepts browser and HTTP requests, constructs the engine's configuration, runs
the CLI, and returns previews, metrics, validation results, and a download. The
browser does not run C++ directly. The server binds to `127.0.0.1` by default
and is intended for local use; it has no authentication layer.

The website workflow is:

1. Paste SQL `CREATE TABLE` statements or load the example and click **Inspect schema**.
2. Set table row counts and optional column rules in **Shape the data**. The AI planner is optional.
3. Choose SQL, CSV, or PostgreSQL COPY and an available validation method.
4. Generate, inspect the preview and metrics, then download the output.

Manual configuration and generation work without a model provider. The UI uses
a dark theme by default and remembers a light-theme preference locally.

The HTTP API includes `/api/schemas/inspect`, `/api/configs/preview`,
`/api/ai/plan`, `/api/configs/{draft_id}/generate`, and run/download endpoints.
Open `http://127.0.0.1:8000/docs` while the server is running for interactive
API documentation. The full request and response contract is in
[server/README.md](server/README.md).

From the repository root, run these commands once to install dependencies and
build the engine and website:

```bash
make setup
make build
```

Then start the complete website and API with one command:

```bash
make run
```

Open `http://127.0.0.1:8000`. Press Ctrl+C to stop. Run `make run` again next
time. After frontend edits, run `make web`; after C++ edits, run `make native`.
Run `make help` to see all targets. If the CLI is outside `build/schemaforge`,
use `make run SCHEMAFORGE_BINARY=/absolute/path/to/schemaforge`. For AI
planning, put `NAVIGATOR_TOOLKIT_API_KEY='your-key'` in the Git-ignored
`server/.env.local`, run `chmod 600 server/.env.local`, and restart `make run`.
Keep the key out of the browser and Git. `make run` loads this file into the
server process. See [server/README.md](server/README.md) for provider selection,
the HTTP contract, and a `curl` example.

On macOS, `make native` cleans parser objects from a Linux build before
compiling, if it finds a Linux parser library in the checkout.

## AI planning and review

The AI path is an **optional prototype** for translating a test goal into
supported configuration changes. It does not train a model or let a model run
SQL or generate rows. AI planning sends the pasted schema, current settings,
and test goal to the configured hosted provider; local generation remains in
the C++ engine.

| Example goal | Reviewed setting |
|---|---|
| “Make 250 users” | `users` row count becomes 250 |
| “Set orders.id between 2 and 20” | A numeric range rule appears for `orders.id` |
| “Use only pending and paid for orders.status” | An allowed-values rule appears |
| “Make 10% of customers.phone values null” | A 0.1 null sampling probability appears, if the column is nullable |

The model returns a structured **proposal**, not engine YAML. Python checks
table and column names, compatible rule types, and explicitly stated counts,
ranges, values, and null rates. It derives implied table totals for simple
per-parent requests where possible. It then builds YAML from the existing
settings and runs the C++ validation and feasibility checks. An invalid plan is
rejected rather than offered as executable configuration. The model gets one
chance to correct an invalid proposal.

On success, the proposed settings appear in the existing controls. The review
shows assumptions, requests that could not be applied, and a read-only YAML
preview. Users can edit the controls or undo the AI changes. After an edit,
the server validates a fresh preview; generation uses the reviewed draft.

For a goal such as “3,000 customers with exactly two orders each,” the planner
can suggest 3,000 customers and 6,000 orders. The engine can create those table
totals and valid foreign keys, but it **does not guarantee** exactly two orders
for each customer. Exact per-parent counts, weighted distributions, and
verification of the full natural-language test goal remain future work. A null
rate is a sampling probability, not an exact final percentage. AI planning
accuracy has not yet been measured against a test set.

UF NaviGator AI Toolkit is selected when `NAVIGATOR_TOOLKIT_API_KEY` is present;
its default model is `gpt-oss-20b`. `OPENAI_API_KEY` enables the OpenAI
provider. Set `SCHEMAFORGE_AI_PROVIDER=navigator` or `openai` to choose
explicitly, and see [server/README.md](server/README.md) for model overrides.
No key is needed for the manual path. The browser does not receive the key.
The planning contract and example scenarios are documented in
[docs/ai-planner-contract.md](docs/ai-planner-contract.md).

## Testing

Run the integration test suite:

```bash
scripts/run_integration_tests.sh

```

Valid test outputs and benchmark reports are written to:

```
tests/artifacts/

```

Run real PostgreSQL import tests for both CSV and PostgreSQL COPY output:

```bash
scripts/run_postgres_validation_tests.sh

```

The PostgreSQL validation script exits successfully with a skip message when Docker is unavailable.

Run the API and AI-planner tests with the replaceable provider test double:

```bash
server/.venv/bin/pytest server/tests
```

Rebuild the web app to check its TypeScript and production bundle:

```bash
make web
```

These tests do not measure live-model planning accuracy. A live provider test
requires a configured key and provider access.

## Quick demo

Check the local build:

```bash
./build/schemaforge --help

```

Create an editable generation config from a schema:

```bash
./build/schemaforge init \
  --schema examples/final_demo/schema.sql \
  --config schemaforge.yaml \
  --seed 42 \
  --default-rows 1000

```

Generate data from the config:

```bash
./build/schemaforge generate --config schemaforge.yaml

# Check schema, config, and generation feasibility without writing output
./build/schemaforge validate --config schemaforge.yaml

```

SchemaForge parses the schema, validates relationships and generation settings, builds a foreign-key dependency order, generates deterministic rows, streams the selected output format, and optionally validates the result.

For a compact end-to-end demo covering SQL, CSV, PostgreSQL COPY, validation, deterministic output, constraints, foreign-key ordering, composite keys, and benchmark reporting, see:

```
examples/final_demo/

```

For the internal pipeline and design rationale, see:

```
docs/architecture.md

```

## Configuration

`init` writes an editable YAML config containing the schema path, dialect, seed, row counts, output settings, benchmark settings, validation settings, and per-table/per-column controls.

Minimal example:

```yaml
schema: schema.sql
dialect: sqlite

generation:
  seed: 42
  default_rows: 100

output:
  format: sql
  file: output.sql

benchmark:
  file: benchmark.yaml

validation:
  sqlite: true
  postgres: false

```

Supported dialects:

-   `sqlite`
-   `postgres`

### Column controls and realistic data

Realistic generation is opt-in and deterministic. Related fields in the same generated row share one profile, so names, emails, phone numbers, and city/state/ZIP values remain coherent. No external faker library or system clock is used; the same seed produces the same output.

```yaml
generation:
  seed: 42
  default_rows: 100
  realistic: true

tables:
  users:
    rows: 100
    columns:
      age:
        min: 18
        max: 80
      status:
        values: [active, inactive]
      middle_name:
        null_probability: 0.4

```

Column control rules:

-   `min` and `max` apply to numeric columns.
-   `values` accepts YAML scalar values and converts them to the target SQL type.
-   `null_probability` must be between 0.0 and 1.0.
-   `null_probability` may only be used on nullable, non-key columns.
-   Nullable single-column foreign keys may be NULL.
-   For composite foreign keys, configuring one local column makes the complete tuple NULL together.
-   Column settings are validated against SQL types and CHECK constraints before output is opened.
-   Settings on parent PRIMARY KEY or UNIQUE columns define the registered key source used by child foreign keys.
-   Local foreign-key columns may only set `null_probability`.
-   Explicit SQL constraints and column settings take precedence over name-based realistic-data heuristics.
-   When `realistic` is omitted or false, SchemaForge uses deterministic placeholder generation.

## Output formats

### SQL INSERT output

Default SQL output writes INSERT statements to a single SQL file.

```yaml
output:
  format: sql
  file: output.sql

```

Behavior:

-   Uses standard single-quote string escaping.
-   Uses `true` / `false` booleans.
-   Uses double-quoted identifiers for SQLite and PostgreSQL dialects.
-   Preserves deterministic seed behavior.
-   Can be validated with SQLite or PostgreSQL, depending on dialect and validation settings.

### CSV output

Set the output format and directory:

```yaml
output:
  format: csv
  directory: output/

```

Generate normally:

```bash
./build/schemaforge generate --config schemaforge.yaml

```

Behavior:

-   Each table is streamed to its own file, such as `output/users.csv` and `output/orders.csv`.
-   Files include a header in schema column order.
-   Rows retain generation order.
-   Missing output directories are created automatically.
-   `output.directory` is required for CSV configs.
-   Text containing commas, quotes, carriage returns, newlines, or surrounding whitespace is quoted.
-   Embedded quotes are doubled.
-   NULL is written as an empty field.
-   Empty text is written as `""`.
-   Booleans use `true` / `false`.
-   Decimals use two fractional digits.
-   Date/time values use the same padded formats as SQL output.
-   SQLite validation is skipped for CSV because the SQLite validator executes SQL statements.
-   Benchmarks still report rows, throughput, memory, and combined CSV output size.

### PostgreSQL COPY output

Use `postgres_copy` to create one self-contained data file for `psql`:

```yaml
dialect: postgres

output:
  format: postgres_copy
  file: output.copy.sql

validation:
  sqlite: false

```

Generate and import:

```bash
./build/schemaforge generate --config schemaforge.yaml
psql --dbname your_database --file output.copy.sql

```

Behavior:

-   Target tables must already exist.
-   Output is wrapped in one transaction.
-   Tables are emitted in foreign-key dependency order.
-   Rows use PostgreSQL text COPY format.
-   Fields are tab-separated.
-   NULL is written as `\N`.
-   Backslashes and control characters are escaped.
-   Table and column identifiers are double-quoted.
-   `postgres_copy` is only allowed with `dialect: postgres`.
-   Rows are streamed directly to one file.
-   SQLite validation is skipped because COPY syntax is PostgreSQL-specific.
-   Benchmark rows, throughput, memory, and output size are still reported.

## Validation

SchemaForge validates schema, config, and generation feasibility before writing output. Validation reports aggregated errors instead of stopping at the first issue.

### Static validation

SchemaForge checks for:

-   missing schema paths and missing schema files
-   unsupported column types
-   duplicate table names
-   duplicate column names
-   primary-key constraints that reference missing columns
-   unique constraints that reference missing columns
-   foreign keys with missing local columns
-   foreign keys with missing referenced tables
-   foreign keys with missing referenced columns
-   mismatched local and referenced foreign-key column counts
-   mismatched local and referenced foreign-key column types
-   foreign keys that do not reference a PRIMARY KEY or UNIQUE constraint
-   dependency cycles between tables
-   unsupported self-referencing foreign keys
-   unsupported generation types
-   unsupported CHECK expressions
-   YAML config entries that reference unknown tables
-   child tables that request rows while a referenced parent table has zero rows
-   row-count requests that exceed finite unique capacities

### SQLite validation

SQLite validation can be enabled for SQL output:

```yaml
validation:
  sqlite: true

```

SQLite validates generated SQL by executing the generated statements and checking configured table row counts.

### PostgreSQL Docker validation

PostgreSQL validation is selected by `dialect: postgres` and can be enabled for SQL INSERT, CSV, and PostgreSQL COPY output:

```yaml
dialect: postgres

validation:
  sqlite: false
  postgres: true

```

SchemaForge starts a temporary `postgres:17-alpine` container without publishing a host port, executes the schema, imports the generated data, and verifies every configured table row count.

PostgreSQL enforces:

-   primary keys
-   unique constraints
-   check constraints
-   foreign keys

The container is removed after validation.

If Docker is missing, inaccessible, or unresponsive, generation succeeds and the benchmark reports:

```
postgres: unavailable

```

Schema or import errors report:

```
postgres: failed

```

and fail the command.




## Generation model

### Foreign-key dependency ordering

Foreign keys create a generation-order problem.

-   If `orders.user_id` references `users.id`, then `users` must be generated before `orders`.
-   If `order_items.order_id` references `orders.id`, then `orders` must be generated before `order_items`.

SchemaForge models these table dependencies as a graph and emits tables in foreign-key-safe order.

```
users -> orders -> order_items

```

### Key source registration

-   Parent primary-key and unique-key values become registered key sources.
-   Child foreign-key columns sample from those registered parent values rather than generating unrelated random values.
-   For composite foreign keys, SchemaForge samples the full referenced tuple together so child rows preserve the complete relationship.

### Unique capacity checks

Some schemas/configs cannot generate the requested number of unique rows.

Examples:

-   `BOOLEAN UNIQUE` can produce at most two non-null values.
-   `CHAR(1) UNIQUE` has a finite capacity.
-   `CHECK (status IN (...))` limits the domain.
-   Composite unique constraints multiply finite domains.
-   A child table cannot request rows when the referenced parent table has zero rows.

SchemaForge detects these cases before generation.

## Benchmarks

Representative local benchmark run. Timing, throughput, output size, and peak memory vary by machine.


| Case | Rows | Output | Generation time | Throughput | Output size | Peak memory | Validation | 
|---|---|---|---|---|---|---|---| 
| single_table_1m | 1,000,000 | SQL | 6.706 s | 149,122 rows/sec | 125.0 MiB | 5.5 MiB | SQLite skipped |
 | users_orders_1m | 1,100,000 | SQL | 11.878 s | 92,609 rows/sec | 152.9 MiB | 136.8 MiB | SQLite passed in 5.268 s | 
 | ecommerce_large | 1,610,000 | SQL | 10.938 s | 147,195 rows/sec | 191.6 MiB | 5.7 MiB | SQLite skipped |

Run all benchmark examples:

```bash
make build
scripts/run_benchmarks.sh

```

Run selected cases:

```bash
scripts/run_benchmarks.sh single_table_1m
scripts/run_benchmarks.sh users_orders_1m ecommerce_large

```

The benchmark examples cover:

-   one million rows in a single table
-   1.1 million parent-child rows with SQLite foreign-key validation
-   a 1.61 million-row e-commerce dependency graph

The runner prints row count, generation time, throughput, output size, peak process memory, and validation status.

Detailed schemas, configs, and notes live under:

```
examples/benchmarks/

```

Generated SQL, reports, and logs are retained under:

```
benchmark-results/

```

Remove generated benchmark artifacts with:

```bash
rm -rf benchmark-results

```

Peak memory is process-wide RSS. For `users_orders_1m`, the reported memory includes the in-memory SQLite validation database; the other benchmark cases disable SQLite so memory usage focuses on generation.

## Supported SQL and types

### Supported schema features

-   SQL CREATE TABLE
-   PRIMARY KEY
-   composite PRIMARY KEY
-   UNIQUE
-   composite UNIQUE
-   FOREIGN KEY
-   composite FOREIGN KEY
-   NOT NULL
-   nullable columns
-   simple CHECK constraints

### Generated SQL types

SchemaForge generates values for:

-   INT
-   SMALLINT
-   BIGINT
-   TEXT
-   VARCHAR
-   CHAR
-   DECIMAL
-   FLOAT
-   DOUBLE
-   REAL
-   BOOLEAN
-   DATE
-   DATETIME
-   TIME

### Known unsupported / rejected types

The validator rejects known unsupported types such as:

-   JSON
-   UUID
-   ARRAY
-   BLOB
-   ENUM
-   INET

## Troubleshooting

### yaml-cpp not found

Install the development package for your platform and re-run CMake from a clean build directory.

Ubuntu/Debian:

```bash
sudo apt install libyaml-cpp-dev

```

Arch/Manjaro:

```bash
sudo pacman -S yaml-cpp

```

macOS:

```bash
brew install yaml-cpp

```

Then:

```bash
rm -rf build
cmake -S . -B build

```

If yaml-cpp is installed in a custom prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/prefix

```

### Hyrise SQL Parser files missing

Initialize submodules:

```bash
git submodule update --init --recursive

```

If the parser build fails, run it directly to see the Makefile error:

```bash
make -C external/sql-parser

```

Then rebuild SchemaForge:

```bash
cmake --build build

```

### SQLite missing

SQLite validation needs both the SQLite runtime and development headers.

Ubuntu/Debian:

```bash
sudo apt install sqlite3 libsqlite3-dev

```

Arch/Manjaro:

```bash
sudo pacman -S sqlite

```

macOS:

```bash
brew install sqlite

```

### PostgreSQL Docker validation unavailable

PostgreSQL validation is optional and requires Docker.

If Docker is not installed, not running, or the current user cannot access the Docker daemon, generation still succeeds and the benchmark reports:

```
postgres: unavailable

```

Verify Docker access:

```bash
docker version

```

On macOS, this requires Docker Desktop.

## Limitations and future work

### Current limitations

-   Only a subset of SQL DDL is supported.
-   Self-referencing foreign keys are rejected.
-   Dependency cycles between tables are rejected.
-   Some SQL types are intentionally unsupported.
-   CHECK support is limited to supported numeric/text expressions.
-   SQLite validation is skipped for CSV and PostgreSQL COPY output.
-   PostgreSQL validation requires Docker.
-   Target PostgreSQL tables must already exist before importing generated COPY output.
-   Benchmark numbers are local-machine dependent.
-   Realistic data generation uses deterministic built-in data rather than an external faker library.
-   The AI planner has no measured planning-accuracy score yet.
-   The engine does not guarantee exact per-parent row counts, value distributions, or full natural-language test outcomes.
-   AI planning sends the schema and goal to the selected hosted provider; manual generation runs without that call.

### Possible enhancements

-   Broader SQL dialect support.
-   More complete CHECK expression evaluation.
-   MySQL/MariaDB output and validation.
-   More realistic domain-specific data generation.
-   Exact relationship patterns, distributions, and test-goal verification.
-   A measured evaluation set for AI planning accuracy and failure cases.
-   Async producer-consumer generation pipeline with bounded queues.
-   Parallel generation where dependency ordering allows it.
-   More detailed reporting for insufficient generation domains.
-   Stage-level benchmarks for parse, validate, generate, write, and database validation.
-   Release binaries or package-manager distribution.

## Extending the codebase

### Add a new SQL type

1.  Extend the internal SQL type model under `include/schemaforge/schema/`.
2.  Add parser adapter mapping under `include/schemaforge/parser/` and the corresponding `src/` implementation.
3.  Add config validation rules under `include/schemaforge/validation/`.
4.  Add deterministic generation behavior under `include/schemaforge/generator/` and `src/generator/`.
5.  Add formatting support in SQL, CSV, and PostgreSQL COPY writers.
6.  Add validation fixtures and output-format tests.

### Add a new constraint

1.  Extend the internal constraint model.
2.  Add parser adapter conversion.
3.  Add schema/config validation.
4.  Add generation feasibility checks.
5.  Add generation-time enforcement.
6.  Add database validation fixtures.

### Add a new output format

1.  Implement a writer backend under the output module.
2.  Preserve table generation order.
3.  Preserve deterministic row ordering.
4.  Define formatting rules for every supported SQL type.
5.  Decide whether SQLite/PostgreSQL validation applies.
6.  Add benchmark output-size reporting.
7.  Add integration tests.

### Add a new validation backend

1.  Add validation config options.
2.  Execute/import the schema.
3.  Import generated output.
4.  Verify configured table row counts.
5.  Surface passed, failed, or unavailable status.
6.  Ensure optional validation failure modes are handled consistently.

## Statistics


| Metric | Value | 
|---|---| 
| Public module groups | benchmark, config, domain, generator, graph, io, output, parser, schema, validation | 
| Large benchmark cases | single_table_1m, users_orders_1m, ecommerce_large | 
| Largest benchmark fixture | 1.61 million generated rows | 
| Output formats | SQL INSERT, CSV, PostgreSQL COPY | 
| Validation backends | SQLite, PostgreSQL via Docker | 
| Main scripts | integration tests, benchmark runner, PostgreSQL validation tests | 
| External parser | Hyrise SQL Parser submodule |
| Local interfaces | CLI, FastAPI HTTP API, React website |
| AI planning | Optional reviewed configuration prototype |

To refresh file/line counts locally:

```bash
find include src -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' \) | wc -l
find tests -type f | wc -l
cloc include src tests

```

## License and references

SchemaForge is an educational systems project for exploring database-aware test-data generation in C++.

Useful references and technologies:

-   C++ standard library
-   CMake
-   Hyrise SQL Parser
-   SQLite
-   PostgreSQL COPY
-   Docker
-   YAML / yaml-cpp
-   Relational database constraints
-   Dependency graphs and topological ordering
-   Deterministic test-data generation
