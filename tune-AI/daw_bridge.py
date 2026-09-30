#!/usr/bin/env python3
"""Small JSON-only bridge between StudioForge DAW and Tune-AI analysis.

It intentionally has no UI, Cubase automation, browser monitoring, or MIDI
mapping. StudioForge invokes it only after an explicit user request.
"""

from __future__ import annotations

import argparse
import json
import sys


def emit(payload: dict) -> int:
    print(json.dumps(payload, ensure_ascii=False), flush=True)
    return 0 if payload.get("ok") else 1


def analyse(url: str) -> dict:
    from core.services.tone_cache_service import ToneCacheService
    from core.services.tone_service import ToneService

    cache = ToneCacheService()
    cached = cache.find_by_url(url)
    if cached:
        cache.mark_used(str(cached.get("video_id", "")))
        return {
            "ok": True,
            "status": "cached",
            "key": cached.get("key", ""),
            "scale": cached.get("scale", ""),
            "confidence": cached.get("confidence", 1.0),
            "title": cached.get("title", ""),
        }

    result = ToneService().detect_tone_from_youtube_url(url, verbose=False)
    cache.save_detect_result(url, result.get("title", ""), result)
    return {
        "ok": True,
        "status": "detected",
        "key": result.get("key", ""),
        "scale": result.get("scale", ""),
        "confidence": result.get("confidence", 0.0),
        "title": result.get("title", ""),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    args = parser.parse_args()
    try:
        return emit(analyse(args.url.strip()))
    except Exception as error:  # Cross-process boundary: send a safe UI error.
        return emit({"ok": False, "error": str(error)})


if __name__ == "__main__":
    sys.exit(main())
