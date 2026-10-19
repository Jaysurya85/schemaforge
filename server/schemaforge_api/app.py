"""HTTP routes for local SchemaForge runs."""

from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from .models import RunRequest, SchemaRequest
from .runner import EngineError, binary_path, call_engine, download_path, generate, inspect_schema, read_run


ROOT = Path(__file__).resolve().parents[2]
app = FastAPI(title="SchemaForge API", version="0.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
    allow_methods=["GET", "POST"],
    allow_headers=["Content-Type"],
)


def engine_messages(error: EngineError) -> list[str]:
    """Return the actionable diagnostics without the CLI's progress output."""
    messages: list[str] = []
    in_errors = False
    for line in error.log.splitlines():
        stripped = line.strip()
        if stripped == "Errors:":
            in_errors = True
        elif in_errors and stripped.startswith("- "):
            messages.append(stripped[2:].strip())
        elif in_errors and stripped:
            in_errors = False
    return messages or [str(error)]


def engine_http_error(error: EngineError) -> HTTPException:
    return HTTPException(
        status_code=422,
        detail={"message": str(error), "errors": engine_messages(error), "log": error.log},
    )


@app.get("/api/health")
def health() -> dict[str, object]:
    try:
        call_engine(["--help"])
        return {"status": "ready", "binary": str(binary_path())}
    except EngineError as error:
        return {"status": "unavailable", "binary": str(binary_path()), "message": str(error)}


@app.get("/api/examples/ecommerce")
def ecommerce_example() -> dict[str, str]:
    schema = ROOT / "examples" / "final_demo" / "schema.sql"
    return {"name": "E-commerce", "sql": schema.read_text(encoding="utf-8")}


@app.post("/api/schemas/inspect")
def inspect(request: SchemaRequest) -> dict[str, object]:
    try:
        return inspect_schema(request)
    except EngineError as error:
        raise engine_http_error(error) from error


@app.post("/api/runs")
def create_run(request: RunRequest) -> dict[str, object]:
    try:
        return generate(request)
    except EngineError as error:
        raise engine_http_error(error) from error


@app.get("/api/runs/{run_id}")
def get_run(run_id: str) -> dict[str, object]:
    result = read_run(run_id)
    if result is None:
        raise HTTPException(status_code=404, detail="Run not found")
    return result


@app.get("/api/runs/{run_id}/download")
def download(run_id: str) -> FileResponse:
    artifact = download_path(run_id)
    if artifact is None:
        raise HTTPException(status_code=404, detail="Run artifact not found")
    return FileResponse(artifact, filename=artifact.name)


WEB_DIST = ROOT / "web" / "dist"
if WEB_DIST.is_dir():
    app.mount("/", StaticFiles(directory=WEB_DIST, html=True), name="web")
