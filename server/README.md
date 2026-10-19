# SchemaForge HTTP API

The server is a local HTTP interface to the existing C++ CLI. Each run uses its
own directory and writes the same output and benchmark files as the CLI. The
web app and `curl` both use this API.

## API contract

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/health` | Check whether the server can find the CLI binary. |
| `GET` | `/api/examples/ecommerce` | Load the bundled demonstration schema. |
| `POST` | `/api/schemas/inspect` | Ask the CLI to validate SQL and create a base configuration. |
| `POST` | `/api/runs` | Generate an artifact and return its run ID, preview, and metrics. |
| `GET` | `/api/runs/{run_id}` | Read a completed run's metadata. |
| `GET` | `/api/runs/{run_id}/download` | Download the generated artifact. |

`POST /api/runs` accepts `sql`, `dialect`, `seed`, `default_rows`, `realistic`,
`tables`, `output_format`, and `validate_with`. Supported column controls are
`min`, `max`, `values`, and `null_probability`. Unsupported request properties
are rejected. The server owns all filesystem paths and never accepts output
paths from a request.

The initial implementation runs each job to completion before responding.
The run ID and resource URLs allow later work to add asynchronous execution
without changing the frontend's data model.
