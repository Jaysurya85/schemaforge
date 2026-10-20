"""HTTP routes for local SchemaForge runs."""

import os
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, ConfigDict, Field

from .ai_plan import (PlanApplicationError, apply_ai_plan, check_explicit_requests,
                      drop_unrequested_zero_null_rates, explain_unsupported_fanout,
                      normalize_fanout_plan)
from .model_provider import NaviGatorProvider, OpenAIProvider, PlanProvider, ProviderError
from .models import RunRequest, SchemaRequest
from .runner import (EngineError, binary_path, call_engine, download_path, generate,
                     generate_prepared, inspect_schema, prepare_config, read_run)


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
    return list(dict.fromkeys(messages)) or [str(error)]


def engine_http_error(error: EngineError) -> HTTPException:
    return HTTPException(
        status_code=422,
        detail={"message": str(error), "errors": engine_messages(error), "log": error.log},
    )


class PlanningRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")
    request: RunRequest
    testing_goal: str = Field(min_length=3, max_length=4_000)


def planning_provider() -> PlanProvider:
    provider = os.environ.get("SCHEMAFORGE_AI_PROVIDER", "navigator" if os.environ.get("NAVIGATOR_TOOLKIT_API_KEY") else "openai").lower()
    if provider == "navigator":
        return NaviGatorProvider()
    if provider != "openai":
        raise ProviderError("SCHEMAFORGE_AI_PROVIDER must be 'navigator' or 'openai'.")
    return OpenAIProvider()


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


@app.post("/api/configs/preview")
def preview_config(request: RunRequest) -> dict[str, object]:
    try:
        return prepare_config(request)
    except EngineError as error:
        raise engine_http_error(error) from error


@app.post("/api/configs/{draft_id}/generate")
def generate_config(draft_id: str) -> dict[str, object]:
    try:
        return generate_prepared(draft_id)
    except EngineError as error:
        raise engine_http_error(error) from error


@app.post("/api/ai/plan")
def create_ai_plan(body: PlanningRequest) -> dict[str, object]:
    try:
        inspection = inspect_schema(body.request)
        provider = planning_provider()
        current = body.request.model_dump(by_alias=True)
        feedback: str | None = None
        for attempt in range(2):
            plan = provider.propose(
                sql=body.request.sql, request=body.testing_goal, inspection=inspection,
                current=current, feedback=feedback)
            plan = drop_unrequested_zero_null_rates(body.testing_goal, plan)
            plan = normalize_fanout_plan(body.testing_goal, plan, inspection, current)
            try:
                check_explicit_requests(body.testing_goal, plan, inspection, current)
                break
            except PlanApplicationError as error:
                if attempt:
                    raise
                feedback = str(error)
        plan = explain_unsupported_fanout(body.testing_goal, plan, inspection, current)
        applied = apply_ai_plan(body.request, plan, inspection)
        draft = prepare_config(applied)
        return {"plan": plan.model_dump(), "draft": draft}
    except EngineError as error:
        raise engine_http_error(error) from error
    except PlanApplicationError as error:
        raise HTTPException(status_code=422, detail={"message": str(error), "errors": [str(error)]}) from error
    except ProviderError as error:
        raise HTTPException(status_code=503, detail={"message": str(error), "errors": [str(error)]}) from error


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
