"""Optional end-to-end checks against a native SchemaForge binary."""

import json
import os
import subprocess
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from schemaforge_api.ai_plan import AIPlan, apply_ai_plan
from schemaforge_api.app import app
from schemaforge_api.models import RunRequest
from schemaforge_api.runner import inspect_schema
from schemaforge_api.runner import initial_config, render_generation_config
from schemaforge_api.model_provider import ProviderError


pytestmark = pytest.mark.skipif(
    not Path(os.environ.get("SCHEMAFORGE_BINARY", "")).is_file(),
    reason="Set SCHEMAFORGE_BINARY to a native CLI to run integration checks",
)
client = TestClient(app)


class FakeProvider:
    def __init__(self, plan: dict | None = None, error: str | None = None):
        self.plan, self.error = plan, error

    def propose(self, **kwargs):
        if self.error:
            raise ProviderError(self.error)
        return AIPlan.model_validate(self.plan)


def base_plan(**overrides):
    return {"version": 1, "table_rows": [], "number_ranges": [], "value_lists": [],
            "null_rates": [], "assumptions": [], "unapplied": [], **overrides}


def planning_body():
    return {"request": {"sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY, tier TEXT);",
                        "validate_with": "none"}, "testing_goal": "Generate customers for testing"}


def test_ai_endpoint_returns_review_draft_without_generation(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    plan = base_plan(table_rows=[{"table": "customers", "rows": 4}],
                     value_lists=[{"table": "customers", "column": "tier", "values": ["gold", "silver"]}],
                     unapplied=[{"request": "normal distribution", "reason": "unsupported_distribution",
                                 "explanation": "Only constraints are available."}])
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: FakeProvider(plan))
    body = planning_body()
    body["testing_goal"] = "Generate 4 customers with gold or silver tiers."
    response = client.post("/api/ai/plan", json=body)
    assert response.status_code == 200, response.text
    data = response.json()
    assert data["plan"]["unapplied"][0]["reason"] == "unsupported_distribution"
    assert data["draft"]["request"]["tables"]["customers"]["rows"] == 4
    assert "gold" in data["draft"]["yaml"]
    assert not (tmp_path / "runs" / data["draft"]["id"] / "output.sql").exists()
    generated = client.post(f"/api/configs/{data['draft']['id']}/generate")
    assert generated.status_code == 200, generated.text
    assert generated.json()["metrics"]["generation"]["total_rows"] == 4
    assert (tmp_path / "runs" / data["draft"]["id"] / "schemaforge.yaml").read_text() == data["draft"]["yaml"]


def test_ai_endpoint_retries_a_plan_that_changes_an_explicit_count(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))

    class RetryProvider:
        def __init__(self):
            self.feedback = []

        def propose(self, **kwargs):
            self.feedback.append(kwargs["feedback"])
            rows = 10 if len(self.feedback) == 1 else 4
            return AIPlan.model_validate(base_plan(table_rows=[{"table": "customers", "rows": rows}]))

    provider = RetryProvider()
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: provider)
    body = planning_body()
    body["testing_goal"] = "Generate 4 customers."
    response = client.post("/api/ai/plan", json=body)
    assert response.status_code == 200, response.text
    assert response.json()["draft"]["request"]["tables"]["customers"]["rows"] == 4
    assert provider.feedback[0] is None
    assert "requested 4 rows" in provider.feedback[1]


def test_ai_endpoint_corrects_derived_order_count_and_explains_fanout_limit(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))

    class FanoutRetryProvider:
        def __init__(self):
            self.feedback = []

        def propose(self, **kwargs):
            self.feedback.append(kwargs["feedback"])
            order_rows = 3000 if len(self.feedback) == 1 else 6000
            return AIPlan.model_validate(base_plan(
                table_rows=[{"table": "customers", "rows": 3000},
                            {"table": "orders", "rows": order_rows}],
                unapplied=[{"request": "every customer should have 2 orders",
                            "reason": "conflicting_request",
                            "explanation": "6000 orders exceeds the current 3000-row limit."}],
            ))

    provider = FanoutRetryProvider()
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: provider)
    body = {"request": {"sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY); "
                               "CREATE TABLE orders (id INTEGER PRIMARY KEY, "
                               "customer_id INTEGER NOT NULL REFERENCES customers(id));",
                        "validate_with": "none"},
            "testing_goal": "Make around 3000 rows and every customer should have 2 orders"}
    response = client.post("/api/ai/plan", json=body)
    assert response.status_code == 200, response.text
    data = response.json()
    assert data["draft"]["request"]["tables"]["customers"]["rows"] == 3000
    assert data["draft"]["request"]["tables"]["orders"]["rows"] == 6000
    assert data["plan"]["unapplied"][0]["reason"] == "unsupported_relationship"
    assert "average" in data["plan"]["unapplied"][0]["explanation"]
    assert "limit" not in data["plan"]["unapplied"][0]["explanation"]
    assert "requested 6000 rows for orders" in provider.feedback[1]


def test_ai_endpoint_keeps_vague_fanout_plan_and_ignores_model_zero_null_defaults(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    plan = base_plan(null_rates=[
        {"table": "customers", "column": "name", "probability": 0},
        {"table": "orders", "column": "customer_id", "probability": 0},
        {"table": "orders", "column": "total", "probability": 0},
    ])
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: FakeProvider(plan))
    body = {"request": {"sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT NOT NULL); "
                               "CREATE TABLE orders (id INTEGER PRIMARY KEY, "
                               "customer_id INTEGER NOT NULL REFERENCES customers(id), "
                               "total DECIMAL NOT NULL);",
                        "tables": {"customers": {"rows": 10}, "orders": {"rows": 10}},
                        "validate_with": "none"},
            "testing_goal": "Make many rows and every customer should have 2 orders"}
    response = client.post("/api/ai/plan", json=body)
    assert response.status_code == 200, response.text
    data = response.json()
    assert data["plan"]["null_rates"] == []
    assert data["plan"]["table_rows"] == [
        {"table": "customers", "rows": 10}, {"table": "orders", "rows": 20},
    ]
    assert data["plan"]["unapplied"][0]["reason"] == "unsupported_relationship"
    assert data["draft"]["request"]["tables"]["orders"]["rows"] == 20
    assert "null_probability" not in data["draft"]["yaml"]


def test_ambiguous_ai_request_is_visible_but_does_not_change_settings(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    plan = base_plan(unapplied=[{"request": "make some customers unusual", "reason": "ambiguous_request",
                                 "explanation": "No specific constraint was provided."}])
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: FakeProvider(plan))
    response = client.post("/api/ai/plan", json=planning_body())
    assert response.status_code == 200, response.text
    assert response.json()["plan"]["unapplied"][0]["reason"] == "ambiguous_request"
    assert response.json()["draft"]["request"]["tables"]["customers"]["rows"] == 10


@pytest.mark.parametrize("plan,error", [
    (base_plan(table_rows=[{"table": "missing", "rows": 4}]), "Unknown table"),
    (base_plan(number_ranges=[{"table": "customers", "column": "missing", "min": 1, "max": 4}]), "Unknown column"),
    (base_plan(table_rows=[{"table": "customers", "rows": 100}],
               number_ranges=[{"table": "customers", "column": "id", "min": 2, "max": 10}]),
     "Cannot generate 100 rows"),
])
def test_ai_endpoint_rejects_unexecutable_plans(monkeypatch: pytest.MonkeyPatch, tmp_path: Path,
                                                 plan: dict, error: str) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: FakeProvider(plan))
    body = planning_body()
    if error == "Cannot generate 100 rows":
        body["testing_goal"] = "Generate 100 customers with ids from 2 to 10."
    elif error == "Unknown column":
        body["testing_goal"] = "Set missing from 1 to 4."
    response = client.post("/api/ai/plan", json=body)
    assert response.status_code == 422
    assert error in str(response.json()["detail"])


def test_ai_endpoint_handles_provider_error(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("schemaforge_api.app.planning_provider", lambda: FakeProvider(error="Provider unavailable"))
    response = client.post("/api/ai/plan", json=planning_body())
    assert response.status_code == 503
    assert response.json()["detail"]["message"] == "Provider unavailable"


def test_validate_command_checks_capacity_without_writing_output(tmp_path: Path) -> None:
    config = initial_config(
        RunRequest(sql="CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT);"), tmp_path
    )
    output_path = tmp_path / "unused.sql"
    config["output"] = {"format": "sql", "file": str(output_path)}
    config["tables"]["customers"]["rows"] = 2
    config_path = tmp_path / "schemaforge.yaml"
    config_path.write_text(render_generation_config(config), encoding="utf-8")
    binary = os.environ["SCHEMAFORGE_BINARY"]

    valid = subprocess.run([binary, "validate", "--config", str(config_path)], capture_output=True, text=True)
    assert valid.returncode == 0, valid.stdout + valid.stderr
    assert "Preflight validation passed" in valid.stdout
    assert not output_path.exists()

    config["tables"]["customers"]["rows"] = 100
    config["tables"]["customers"]["columns"] = {"id": {"min": 2, "max": 10}}
    config_path.write_text(render_generation_config(config), encoding="utf-8")
    invalid = subprocess.run([binary, "validate", "--config", str(config_path)], capture_output=True, text=True)
    assert invalid.returncode != 0
    assert "Cannot generate 100 rows for customers.id" in invalid.stdout
    assert not output_path.exists()


def test_preview_executes_exact_reviewed_yaml(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    request = {"sql": "CREATE TABLE items (id INTEGER PRIMARY KEY, label TEXT);",
               "tables": {"items": {"rows": 3}}, "validate_with": "none"}
    preview = client.post("/api/configs/preview", json=request)
    assert preview.status_code == 200, preview.text
    draft = preview.json()
    config_path = tmp_path / "runs" / draft["id"] / "schemaforge.yaml"
    assert config_path.read_text() == draft["yaml"]
    assert not (tmp_path / "runs" / draft["id"] / "output.sql").exists()
    result = client.post(f"/api/configs/{draft['id']}/generate")
    assert result.status_code == 200, result.text
    assert result.json()["tables"] == [{"name": "items", "rows": 3}]
    assert config_path.read_text() == draft["yaml"]
    assert client.post(f"/api/configs/{draft['id']}/generate").status_code == 422


def test_invalid_or_changed_preview_cannot_generate(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    invalid = client.post("/api/configs/preview", json={
        "sql": "CREATE TABLE items (id INTEGER PRIMARY KEY);",
        "tables": {"items": {"rows": 100, "columns": {"id": {"min": 2, "max": 10}}}},
    })
    assert invalid.status_code == 422
    assert "Cannot generate 100 rows" in invalid.json()["detail"]["errors"][0]
    assert list((tmp_path / "runs").iterdir()) == []

    preview = client.post("/api/configs/preview", json={"sql": "CREATE TABLE items (id INTEGER PRIMARY KEY);"})
    assert preview.status_code == 200
    draft_id = preview.json()["id"]
    config_path = tmp_path / "runs" / draft_id / "schemaforge.yaml"
    config_path.write_text(config_path.read_text() + "\n# changed\n")
    changed = client.post(f"/api/configs/{draft_id}/generate")
    assert changed.status_code == 422
    assert "changed" in changed.json()["detail"]["message"]

def test_inspection_returns_only_real_columns() -> None:
    response = client.post(
        "/api/schemas/inspect",
        json={"sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT UNIQUE NOT NULL);"},
    )
    assert response.status_code == 200, response.text
    assert response.json()["tables"] == [
        {"name": "customers", "rows": 10, "columns": ["id", "name", "email"],
         "column_types": {"id": "INTEGER", "name": "TEXT", "email": "TEXT"}}
    ]


@pytest.mark.parametrize(
    ("output_format", "dialect", "artifact_type"),
    [("sql", "sqlite", "application/x-sql"),
     ("csv", "sqlite", "application/zip"),
     ("postgres_copy", "postgres", "application/x-sql")],
)
def test_real_engine_generates_and_downloads_each_format(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    output_format: str,
    dialect: str,
    artifact_type: str,
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    request = {
        "sql": "CREATE TABLE people (id INTEGER PRIMARY KEY, tier TEXT, score INTEGER);",
        "dialect": dialect,
        "seed": 42,
        "default_rows": 3,
        "tables": {"people": {"rows": 3, "columns": {"tier": {"values": ["gold", "silver"]}, "score": {"min": 1, "max": 9}}}},
        "realistic": False,
        "output_format": output_format,
        "validate_with": "sqlite" if output_format == "sql" else "none",
    }
    response = client.post("/api/runs", json=request)
    assert response.status_code == 200, response.text
    result = response.json()
    assert result["metrics"]["generation"]["total_rows"] == 3
    assert result["previews"]
    assert client.get(f"/api/runs/{result['id']}").json() == result
    download = client.get(f"/api/runs/{result['id']}/download")
    assert download.status_code == 200
    assert download.headers["content-type"] == artifact_type
    assert download.content


def test_real_engine_rejects_invalid_schema(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    response = client.post(
        "/api/schemas/inspect",
        json={"sql": "CREATE TABLE broken (id BOGUS_TYPE);", "dialect": "sqlite"},
    )
    assert response.status_code == 422
    assert response.json()["detail"]["log"]


def test_capacity_failure_returns_only_the_cause(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    response = client.post(
        "/api/runs",
        json={
            "sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT);",
            "tables": {"customers": {"rows": 100, "columns": {"id": {"min": 2, "max": 10}}}},
        },
    )
    assert response.status_code == 422
    detail = response.json()["detail"]
    assert len(detail["errors"]) == 1
    assert detail["errors"][0].startswith("Cannot generate 100 rows for customers.id.")
    assert "Welcome to Schemaforge" in detail["log"]


def test_numeric_value_list_reports_capacity_instead_of_type_error(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    response = client.post(
        "/api/runs",
        json={
            "sql": "CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT);",
            "tables": {"customers": {"rows": 100, "columns": {"id": {"values": [1, 5]}}}},
        },
    )
    assert response.status_code == 422
    errors = response.json()["detail"]["errors"]
    assert len(errors) == 1
    assert errors[0].startswith("Cannot generate 100 rows for customers.id.")
    assert "2 distinct values" in errors[0]


def test_ai_plan_composes_a_real_generation_config(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))
    cases = json.loads((Path(__file__).parents[2] / "docs" / "ai-planner-cases.json").read_text())["cases"]
    case = next(item for item in cases if item["id"] == "multi_table_supported")
    request = RunRequest(sql=case["schema"], validate_with="none")
    applied = apply_ai_plan(request, AIPlan.model_validate(case["expected"]), inspect_schema(request))
    response = client.post("/api/runs", json=applied.model_dump(by_alias=True))
    assert response.status_code == 200, response.text
    result = response.json()
    assert result["metrics"]["generation"]["total_rows"] == 4000
    assert result["tables"] == [{"name": "customers", "rows": 1000}, {"name": "orders", "rows": 3000}]
