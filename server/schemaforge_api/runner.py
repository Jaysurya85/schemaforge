"""File-based adapter for SchemaForge's existing CLI."""

import csv
import os
import shutil
import subprocess
import tempfile
import uuid
from pathlib import Path
from typing import Any

import sqlparse
import yaml

from .models import RunRequest, SchemaRequest


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = ROOT / "build" / "schemaforge"
DEFAULT_RUNS = Path(tempfile.gettempdir()) / "schemaforge-web-runs"


class EngineError(Exception):
    def __init__(self, message: str, log: str = "") -> None:
        super().__init__(message)
        self.log = log[-8_000:]


def binary_path() -> Path:
    return Path(os.environ.get("SCHEMAFORGE_BINARY", str(DEFAULT_BINARY))).resolve()


def run_root() -> Path:
    return Path(os.environ.get("SCHEMAFORGE_RUN_DIR", str(DEFAULT_RUNS))).resolve()


def ensure_schema_only(sql: str) -> None:
    """Keep the CLI's database validation input to table DDL only."""
    statements = sqlparse.parse(sql)
    if not statements:
        raise EngineError("Paste at least one CREATE TABLE statement.")
    for statement in statements:
        keywords = [
            token.normalized.upper()
            for token in statement.flatten()
            if token.ttype in sqlparse.tokens.Keyword and not token.is_whitespace
        ]
        if statement.get_type() != "CREATE" or keywords[:2] != ["CREATE", "TABLE"]:
            raise EngineError("Only CREATE TABLE statements are accepted in the schema editor.")


def call_engine(arguments: list[str]) -> str:
    binary = binary_path()
    if not binary.is_file():
        raise EngineError(f"SchemaForge binary not found at {binary}. Build the C++ CLI first.")
    try:
        result = subprocess.run(
            [str(binary), *arguments],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
    except OSError as exc:
        raise EngineError(f"Could not start SchemaForge: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise EngineError("SchemaForge exceeded the 180-second run limit.") from exc
    log = "\n".join(part for part in (result.stdout.strip(), result.stderr.strip()) if part)
    if result.returncode != 0:
        raise EngineError("SchemaForge rejected this schema or configuration.", log)
    return log


def initial_config(request: SchemaRequest, directory: Path) -> dict[str, Any]:
    ensure_schema_only(request.sql)
    schema_path = directory / "schema.sql"
    config_path = directory / "schemaforge.yaml"
    schema_path.write_text(request.sql, encoding="utf-8")
    call_engine(
        [
            "init", "--schema", str(schema_path), "--config", str(config_path),
            "--seed", str(request.seed), "--default-rows", str(request.default_rows),
        ]
    )
    config = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or not isinstance(config.get("tables"), dict):
        raise EngineError("SchemaForge did not produce a usable configuration.")
    config["schema"] = str(schema_path)
    config["dialect"] = request.dialect
    return config


def inspect_schema(request: SchemaRequest) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="schemaforge-inspect-") as temporary:
        config = initial_config(request, Path(temporary))
        return {
            "tables": [
                {"name": name, "rows": settings.get("rows", request.default_rows)}
                for name, settings in config["tables"].items()
            ],
            "dialect": request.dialect,
        }


def preview_file(path: Path, limit: int = 12) -> dict[str, Any]:
    if path.suffix == ".csv":
        with path.open(newline="", encoding="utf-8") as stream:
            rows = []
            for index, row in enumerate(csv.reader(stream)):
                if index >= limit:
                    break
                rows.append(row)
        return {"name": path.name, "kind": "table", "rows": rows}
    with path.open(encoding="utf-8") as stream:
        lines = []
        for index, line in enumerate(stream):
            if index >= limit:
                break
            lines.append(line.rstrip("\n")[:1000])
    return {"name": path.name, "kind": "code", "lines": lines}


def generate(request: RunRequest) -> dict[str, Any]:
    if request.output_format == "postgres_copy" and request.dialect != "postgres":
        raise EngineError("PostgreSQL COPY output requires the PostgreSQL dialect.")
    if request.validate_with == "sqlite" and (request.dialect != "sqlite" or request.output_format != "sql"):
        raise EngineError("SQLite validation is available for SQLite SQL output.")
    if request.validate_with == "postgres" and request.dialect != "postgres":
        raise EngineError("PostgreSQL validation requires the PostgreSQL dialect.")

    run_id = uuid.uuid4().hex
    directory = run_root() / run_id
    directory.mkdir(parents=True, exist_ok=False)
    try:
        config = initial_config(request, directory)
        unknown_tables = set(request.tables) - set(config["tables"])
        if unknown_tables:
            raise EngineError("Unknown tables: " + ", ".join(sorted(unknown_tables)))
        for name, settings in request.tables.items():
            table = config["tables"][name]
            table["rows"] = settings.rows
            if settings.columns:
                table["columns"] = {
                    column: value.model_dump(by_alias=True, exclude_none=True)
                    for column, value in settings.columns.items()
                }

        config["generation"]["realistic"] = request.realistic
        config["validation"] = {
            "sqlite": request.validate_with == "sqlite",
            "postgres": request.validate_with == "postgres",
        }
        config["benchmark"] = {"file": str(directory / "benchmark.yaml")}
        if request.output_format == "csv":
            config["output"] = {"format": "csv", "directory": str(directory / "csv")}
        else:
            suffix = "copy.sql" if request.output_format == "postgres_copy" else "sql"
            config["output"] = {"format": request.output_format, "file": str(directory / f"output.{suffix}")}

        config_path = directory / "schemaforge.yaml"
        config_path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
        log = call_engine(["generate", "--config", str(config_path)])
        if request.output_format == "csv":
            files = sorted((directory / "csv").glob("*.csv"))
            if not files:
                raise EngineError("SchemaForge did not create CSV output.", log)
            artifact = Path(shutil.make_archive(str(directory / "output"), "zip", directory / "csv"))
        else:
            artifact = Path(config["output"]["file"])
            files = [artifact]
        if not artifact.is_file():
            raise EngineError("SchemaForge did not create the output artifact.", log)
        benchmark = directory / "benchmark.yaml"
        metrics = yaml.safe_load(benchmark.read_text(encoding="utf-8")) if benchmark.is_file() else {}
        result = {
            "id": run_id,
            "status": "completed",
            "format": request.output_format,
            "tables": [{"name": name, "rows": settings.get("rows", request.default_rows)} for name, settings in config["tables"].items()],
            "previews": [preview_file(path) for path in files],
            "metrics": metrics or {},
            "log": log,
            "download_name": artifact.name,
        }
        (directory / "result.yaml").write_text(yaml.safe_dump(result, sort_keys=False), encoding="utf-8")
        return result
    except Exception:
        shutil.rmtree(directory, ignore_errors=True)
        raise


def read_run(run_id: str) -> dict[str, Any] | None:
    if len(run_id) != 32 or any(character not in "0123456789abcdef" for character in run_id):
        return None
    result = run_root() / run_id / "result.yaml"
    return yaml.safe_load(result.read_text(encoding="utf-8")) if result.is_file() else None


def download_path(run_id: str) -> Path | None:
    result = read_run(run_id)
    if result is None:
        return None
    artifact = run_root() / run_id / result["download_name"]
    return artifact if artifact.is_file() else None
