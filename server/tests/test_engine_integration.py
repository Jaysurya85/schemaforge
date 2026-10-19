"""Optional end-to-end checks against a native SchemaForge binary."""

import os
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from schemaforge_api.app import app


pytestmark = pytest.mark.skipif(
    not Path(os.environ.get("SCHEMAFORGE_BINARY", "")).is_file(),
    reason="Set SCHEMAFORGE_BINARY to a native CLI to run integration checks",
)
client = TestClient(app)


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
