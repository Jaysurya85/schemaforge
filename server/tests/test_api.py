"""The browser-facing contract remains usable independently of the real CLI build."""

from pathlib import Path

import yaml
from fastapi.testclient import TestClient

from schemaforge_api.app import app, engine_http_error, engine_messages
from schemaforge_api.runner import EngineError, ensure_schema_only, schema_columns


client = TestClient(app)


def test_schema_editor_accepts_tables_and_rejects_other_statements() -> None:
    ensure_schema_only("-- demo\nCREATE TABLE users (id INT PRIMARY KEY);")
    try:
        ensure_schema_only("CREATE TABLE users (id INT); DROP TABLE users;")
    except EngineError as error:
        assert "Only CREATE TABLE" in str(error)
    else:
        raise AssertionError("A non-table SQL statement was accepted")


def test_column_choices_skip_constraints_and_nested_commas() -> None:
    columns = schema_columns(
        'CREATE TABLE orders ("order id" INTEGER PRIMARY KEY, total DECIMAL(10, 2), '
        'customer_id INTEGER, CONSTRAINT fk FOREIGN KEY (customer_id) REFERENCES customers(id));'
    )
    assert columns == [["order id", "total", "customer_id"]]


def test_only_actionable_engine_errors_are_displayed() -> None:
    log = (
        "Welcome to Schemaforge\nValidation Result: Valid\n"
        "Generation Validation Result: Invalid\nErrors:\n"
        "- Cannot generate 100 rows for customers.id.\n"
        "- Choose a wider range.\n"
    )
    error = EngineError("SchemaForge rejected this schema or configuration.", log)
    assert engine_messages(error) == [
        "Cannot generate 100 rows for customers.id.",
        "Choose a wider range.",
    ]
    assert engine_http_error(error).detail["log"] == log
    assert engine_messages(EngineError("Paste a CREATE TABLE statement.")) == [
        "Paste a CREATE TABLE statement."
    ]


def test_unknown_request_settings_are_rejected() -> None:
    response = client.post(
        "/api/runs",
        json={"sql": "CREATE TABLE users (id INT PRIMARY KEY);", "weighted_distribution": {}},
    )
    assert response.status_code == 422


def test_example_is_the_repository_schema() -> None:
    response = client.get("/api/examples/ecommerce")
    assert response.status_code == 200
    assert "CREATE TABLE users" in response.json()["sql"]
    assert "CREATE TABLE orders" in response.json()["sql"]


def test_missing_run_is_not_a_filesystem_path() -> None:
    response = client.get("/api/runs/../../README.md")
    assert response.status_code in (400, 404)
    response = client.get("/api/runs/00000000000000000000000000000000/download")
    assert response.status_code == 404


def test_run_creates_a_retrievable_artifact(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.setenv("SCHEMAFORGE_RUN_DIR", str(tmp_path / "runs"))

    def fake_engine(arguments: list[str]) -> str:
        config_path = Path(arguments[arguments.index("--config") + 1])
        if arguments[0] == "init":
            config_path.write_text(
                yaml.safe_dump({
                    "schema": arguments[arguments.index("--schema") + 1],
                    "generation": {"seed": 42, "default_rows": 10},
                    "tables": {"users": {"rows": 10}},
                }),
                encoding="utf-8",
            )
            return "Initialized"
        config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
        assert config["tables"]["users"]["rows"] == 3
        assert config["generation"]["realistic"] is True
        assert config["validation"]["sqlite"] is True
        Path(config["output"]["file"]).write_text(
            'INSERT INTO "users" VALUES (1);\n', encoding="utf-8"
        )
        Path(config["benchmark"]["file"]).write_text("total_rows: 3\nsqlite: passed\n", encoding="utf-8")
        return "SQLite Validation Result: Valid"

    monkeypatch.setattr("schemaforge_api.runner.call_engine", fake_engine)
    response = client.post(
        "/api/runs",
        json={
            "sql": "CREATE TABLE users (id INT PRIMARY KEY);",
            "tables": {"users": {"rows": 3}},
            "realistic": True,
        },
    )
    assert response.status_code == 200, response.text
    result = response.json()
    assert result["status"] == "completed"
    assert result["metrics"]["total_rows"] == 3
    assert client.get(f"/api/runs/{result['id']}").json()["id"] == result["id"]
    download = client.get(f"/api/runs/{result['id']}/download")
    assert download.status_code == 200
    assert 'INSERT INTO "users"' in download.text
