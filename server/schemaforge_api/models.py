"""The public request shapes shared by browser and HTTP clients."""

from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)


class SchemaRequest(StrictModel):
    sql: str = Field(min_length=1, max_length=100_000)
    dialect: Literal["sqlite", "postgres"] = "sqlite"
    seed: int = Field(default=42, ge=0, le=4_294_967_295)
    default_rows: int = Field(default=10, ge=0, le=2_000_000)


class ColumnSettings(StrictModel):
    minimum: float | None = Field(default=None, alias="min")
    maximum: float | None = Field(default=None, alias="max")
    values: list[str | int | float | bool] | None = None
    null_probability: float | None = Field(default=None, ge=0, le=1)


class TableSettings(StrictModel):
    rows: int = Field(ge=0, le=2_000_000)
    columns: dict[str, ColumnSettings] = Field(default_factory=dict)


class RunRequest(SchemaRequest):
    realistic: bool = False
    tables: dict[str, TableSettings] = Field(default_factory=dict)
    output_format: Literal["sql", "csv", "postgres_copy"] = "sql"
    validate_with: Literal["none", "sqlite", "postgres"] = "sqlite"
