"""Explicit download permission and immutable model revisions."""
from __future__ import annotations
import json
import os
from pathlib import Path
import re
from .core import UserVisibleError, ui_text


def require_model_download_permission() -> None:
    if os.environ.get("LURVIKO_SUBTITLE_AI_ALLOW_MODEL_DOWNLOAD", "0").strip().lower() not in {"1", "true", "yes", "on"}:
        raise UserVisibleError(ui_text("error_model_permission"))


def model_spec(model: str, kind: str) -> tuple[str, str]:
    pins = json.loads((Path(__file__).resolve().parents[1] / "model-revisions.json").read_text())
    alias = "turbo" if kind == "asr" and model == "large-v3-turbo" else model
    spec = pins[kind].get(alias)
    override = os.environ.get(f"LURVIKO_SUBTITLE_AI_{kind.upper()}_MODEL_REVISION", "").strip()
    repository = spec["repo"] if spec else model
    revision = override or (spec["revision"] if spec else "")
    if not re.fullmatch(r"[0-9a-f]{40}", revision) or not re.fullmatch(r"[\w.-]+/[\w.-]+", repository):
        raise UserVisibleError(ui_text("error_model_revision", model=model))
    return repository, revision


def matches_snapshot(path: Path, repository: str, revision: str) -> bool:
    return path.is_dir() and path.name == revision and path.parent.name == "snapshots" \
        and path.parent.parent.name == "models--" + repository.replace("/", "--")
