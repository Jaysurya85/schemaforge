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
| `POST` | `/api/configs/preview` | Validate a run request and return the exact YAML draft. |
| `POST` | `/api/configs/{draft_id}/generate` | Generate from the unchanged, reviewed YAML draft. |
| `POST` | `/api/ai/plan` | Propose settings from a testing goal, preflight them, and return a review draft. |
| `GET` | `/api/runs/{run_id}` | Read a completed run's metadata. |
| `GET` | `/api/runs/{run_id}/download` | Download the generated artifact. |

`POST /api/runs` accepts `sql`, `dialect`, `seed`, `default_rows`, `realistic`,
`tables`, `output_format`, and `validate_with`. Supported column controls are
`min`, `max`, `values`, and `null_probability`. Unsupported request properties
are rejected. The server owns all filesystem paths and never accepts output
paths from a request.

`POST /api/ai/plan` accepts `{"request": <run request>, "testing_goal": "..."}`.
It returns a `plan` with proposed row counts and column rules, assumptions,
and `unapplied` requests, plus a `draft` containing an ID, the applied request,
and its exact YAML. The server checks the schema, names, types, and generation
capacity before returning it. The endpoint does not generate data. A successful
plan fills the controls in **Shape the data** and shows the validated YAML there
for review. The user can edit those controls or undo the AI changes. After an
edit, the browser calls `/api/configs/preview` to validate and display the new
YAML; generation is disabled until the preview matches the visible controls.
Generating from the reviewed draft uses that exact saved YAML.

AI planning sends the SQL schema, current settings, and testing goal to the
configured provider. To use UF NaviGator AI Toolkit, create a personal key with
access to the UF-hosted `gpt-oss-20b` model and set `NAVIGATOR_TOOLKIT_API_KEY`
in the server's environment before launching it. The server selects NaviGator
when that key exists. `SCHEMAFORGE_NAVIGATOR_MODEL` overrides the model. The
existing OpenAI provider remains available through `OPENAI_API_KEY`; select it
explicitly with `SCHEMAFORGE_AI_PROVIDER=openai` if both keys are present.
`SCHEMAFORGE_OPENAI_MODEL` overrides its default `gpt-4o-mini` model. You can
also explicitly select NaviGator with `SCHEMAFORGE_AI_PROVIDER=navigator`.
Keep API keys out of the browser, source files, Git, and logs. Without a key,
manual generation and config preview still work, while AI planning returns a
clear setup error. Model output is treated
as a proposal: a strict response schema, Pydantic checks, schema-name and type
checks, checks against explicit counts, ranges, values, and null percentages in
the request, and the C++ validation-only preflight all run before a draft is
offered. If the proposal contradicts those explicit details, the server asks
the model to correct it once; a second contradiction returns a validation
error instead of a misleading draft.
For a request such as "around 3,000 rows and every customer should have 2
orders," the planner treats 3,000 as the customer count and checks the implied
6,000 order rows. This is an assumption shown in the review. The engine may
generate those row counts, but it still samples valid customer IDs for orders;
exactly two orders per customer is reported as an unsupported relationship.
The provider adapter is replaceable for offline tests. The current engine can
apply constraints, not probability distributions; those requests appear in
`unapplied` instead of being represented as generated behavior.

For a manual preview without AI, post the same run request to
`/api/configs/preview`, inspect its `yaml`, then post to
`/api/configs/{draft_id}/generate`. Drafts and completed runs live under
`SCHEMAFORGE_RUN_DIR` (the system temporary directory by default). They are
retained there until you remove them. A draft can be generated once. The API
listens on loopback in the commands below; it has no authentication layer.

The initial implementation runs each job to completion before responding.
The run ID and resource URLs allow later work to add asynchronous execution
without changing the frontend's data model.

For example, from another local terminal:

```sh
curl -s http://127.0.0.1:8000/api/health
curl -s -X POST http://127.0.0.1:8000/api/schemas/inspect \
  -H 'Content-Type: application/json' \
  -d '{"sql":"CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);","dialect":"sqlite","default_rows":10}'
```

## Run locally

Build the C++ CLI for the current machine first. From the repository root:

```sh
python3 -m venv server/.venv
server/.venv/bin/pip install -e 'server[test]'
server/.venv/bin/uvicorn schemaforge_api.app:app --app-dir server --host 127.0.0.1 --port 8000
```

Set `SCHEMAFORGE_BINARY` if the executable is elsewhere. Set
`SCHEMAFORGE_RUN_DIR` to choose where run artifacts are retained. The default
is the operating system's temporary directory. The local API is available at
`http://127.0.0.1:8000/api/health`; interactive API documentation is at
`http://127.0.0.1:8000/docs`.

For a single-server website, run `pnpm install --frozen-lockfile` and
`pnpm build` in `web/` before starting the API. The API serves `web/dist` at
`http://127.0.0.1:8000` when the build exists. During frontend development,
`pnpm dev` serves the site at `http://127.0.0.1:5173` and proxies API calls.

Run the server tests with `server/.venv/bin/pytest server/tests`. Set
`SCHEMAFORGE_BINARY` to a native executable to include the real-engine checks.
