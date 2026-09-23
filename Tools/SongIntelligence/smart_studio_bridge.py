#!/usr/bin/env python3
"""Local JSON bridge from StudioForge to the existing smart_studio analyzers.

This is deliberately a command-line worker, not an HTTP service.  StudioForge
starts it only for an explicit analysis or catalog-search request, then reads a
single JSON response from stdout.  It must never be started from an audio
callback.

Development usage:
  <smart_studio>/.venv/Scripts/python.exe smart_studio_bridge.py request.json

The request contains either:
  {"operation":"analyze_file", "audio_path":"...", "input_type":"Full Mix"}
or
  {"operation":"catalog_search", "title":"...", "artist":"..."}

Set STUDIOFORGE_SMART_STUDIO_ROOT when smart_studio is not a sibling checkout.
"""

from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Any


DEFAULT_ANALYSIS_TYPE = "Full Mix"


def smart_studio_root() -> Path:
    configured = os.environ.get("STUDIOFORGE_SMART_STUDIO_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()

    # This keeps a developer checkout convenient without making an installed
    # DAW depend on a machine-specific absolute path.
    return Path(__file__).resolve().parents[3] / "smart_studio"


def configure_imports(root: Path) -> None:
    api_root = root / "basic" / "api"
    if not root.is_dir() or not api_root.is_dir():
        raise RuntimeError(
            "smart_studio was not found. Set STUDIOFORGE_SMART_STUDIO_ROOT "
            "to its checkout or install the packaged helper."
        )

    sys.path.insert(0, str(root))
    sys.path.insert(0, str(api_root))


def normalized_analysis(result: Any) -> dict[str, Any]:
    """Keep the DAW contract small and independent from FastAPI/Pydantic."""
    return {
        "status": "ok",
        "operation": "analyze_file",
        "suggestion": {
            "key": result.primary_key,
            "scale": result.scale_type,
            "bpm": result.tempo.bpm,
            "tempo_confidence": result.tempo.confidence,
            "tempo_source": result.tempo.source,
            "input_type": result.input_type.value,
            "input_quality": result.input_quality,
            "pitch_stability": result.pitch_stability,
            "key_candidates": [
                {
                    "key": candidate.key_label,
                    "confidence": candidate.confidence,
                    "evidence": list(candidate.evidence),
                }
                for candidate in result.key_candidates[:5]
            ],
            "chords": [
                {
                    "name": chord.chord_name,
                    "start_seconds": chord.start_time,
                    "end_seconds": chord.end_time,
                    "confidence": chord.confidence,
                }
                for chord in result.chords
            ],
            "warnings": list(result.warnings),
            "evidence": list(result.evidence),
        },
        "requires_user_confirmation": True,
    }


def analyze_file(request: dict[str, Any]) -> dict[str, Any]:
    audio_path = Path(str(request.get("audio_path", ""))).expanduser()
    if not audio_path.is_file():
        raise ValueError("audio_path must name an existing local audio file.")

    from studio_agent.services.pipeline import StudioAnalysisPipeline

    result = StudioAnalysisPipeline().analyze(
        str(audio_path),
        str(request.get("input_type") or DEFAULT_ANALYSIS_TYPE),
        enable_source_separation=bool(request.get("enable_source_separation", False)),
    )
    return normalized_analysis(result)


def catalog_search(request: dict[str, Any]) -> dict[str, Any]:
    """Return selectable title/artist candidates without resolving one."""
    title = str(request.get("title", "")).strip()
    artist = str(request.get("artist", "")).strip()
    if not title:
        raise ValueError("title is required for catalog_search.")

    from app.services.analysis import SongIdentity, search_catalog_candidates_for_identity

    result = asyncio.run(
        search_catalog_candidates_for_identity(SongIdentity(title, artist or None))
    )
    return {
        "status": "ok",
        "operation": "catalog_search",
        "query": result.query,
        "candidates": [candidate.model_dump() for candidate in result.candidates],
        "requires_user_confirmation": True,
    }


def normalized_identity_analysis(response: Any) -> dict[str, Any]:
    """Normalize the optimized smart_studio metadata/chart lookup result."""
    metrics = response.metrics
    return {
        "status": "ok",
        "operation": "lookup_song",
        "suggestion": {
            "key": metrics.primary_key,
            "scale": metrics.scale_type,
            "bpm": metrics.bpm,
            "tempo_confidence": metrics.tempo_confidence,
            "tempo_source": metrics.tempo_source,
            "chord_progression": list(metrics.chord_progression),
            "chord_source": metrics.chord_source,
            "recommended_capo": metrics.recommended_capo,
            "chords": [
                {
                    "name": chord.chord_name,
                    "start_seconds": chord.start_time,
                    "end_seconds": chord.end_time,
                    "confidence": chord.confidence,
                }
                for chord in response.chords
            ],
            "warnings": list(response.warnings),
            "source_status": list(response.source_status),
            "sources": [source.model_dump() for source in response.sources],
            "timings": [step.model_dump() for step in response.timings],
        },
        "requires_user_confirmation": True,
    }


def lookup_song(request: dict[str, Any]) -> dict[str, Any]:
    """Use smart_studio's cache-first parallel metadata/chart crawl path."""
    title = str(request.get("title", "")).strip()
    artist = str(request.get("artist", "")).strip()
    if not title:
        raise ValueError("title is required for lookup_song.")

    from app.services.analysis import analysis_service

    # analyze_identity is the production route: it fans out SongBPM, SongData
    # and chart lookups concurrently, retains their individual timeouts, and
    # returns cached information first when it exists.
    response = asyncio.run(
        analysis_service.analyze_identity(title_hint=title, artist_hint=artist)
    )
    return normalized_identity_analysis(response)


def handle_request(request: dict[str, Any]) -> dict[str, Any]:
    root = smart_studio_root()
    configure_imports(root)

    operation = str(request.get("operation", "")).strip()
    if operation == "analyze_file":
        return analyze_file(request)
    if operation == "catalog_search":
        return catalog_search(request)
    if operation == "lookup_song":
        return lookup_song(request)
    raise ValueError("operation must be 'analyze_file', 'catalog_search', or 'lookup_song'.")


def load_request(path_argument: str) -> dict[str, Any]:
    request_path = Path(path_argument).expanduser()
    with request_path.open("r", encoding="utf-8") as input_file:
        value = json.load(input_file)
    if not isinstance(value, dict):
        raise ValueError("The request JSON must contain one object.")
    return value


def main() -> int:
    if len(sys.argv) != 2:
        print(json.dumps({"status": "error", "message": "Expected one request JSON file."}))
        return 2

    try:
        response = handle_request(load_request(sys.argv[1]))
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        response = {"status": "error", "message": str(error), "requires_user_confirmation": True}
    except Exception as error:  # Keep the DAW process isolated from optional integrations.
        response = {"status": "error", "message": f"Song intelligence failed: {error}", "requires_user_confirmation": True}

    print(json.dumps(response, ensure_ascii=False))
    return 0 if response["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
