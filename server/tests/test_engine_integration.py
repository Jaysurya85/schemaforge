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


pytestmark = pytest.mark.skipif(
    not Path(os.environ.get("SCHEMAFORGE_BINARY", "")).is_file(),
    reason="Set SCHEMAFORGE_BINARY to a native CLI to run integration checks",
)
client = TestClient(app)


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
