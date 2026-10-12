"""Coordinate end-modulation analysis with the saved base-tone workflow."""


class EndModulationWorkflowService:
    """Keep end-of-song modulation rules out of the application facade."""

    def __init__(self, end_modulation_service, tone_cache_service, tone_workflow_service, key_signature_service):
        self.end_modulation_service = end_modulation_service
        self.tone_cache_service = tone_cache_service
        self.tone_workflow_service = tone_workflow_service
        self.key_signature_service = key_signature_service

    def find_saved_by_url(self, youtube_url: str):
        try:
            return self.tone_cache_service.find_end_modulation_by_url(youtube_url)
        except Exception:
            return None

    def invalidate_audio_cache(self) -> None:
        try:
            self.end_modulation_service.invalidate_cache()
        except Exception:
            pass

    def analyze(self, youtube_url: str, title: str = "", base_key: str = "", base_scale: str = "", progress_cb=None) -> dict:
        youtube_url = str(youtube_url or "").strip()
        title = str(title or "").strip()
        base_key = str(base_key or "").strip()
        base_scale = str(base_scale or "").strip()
        video_id = self.tone_workflow_service.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id để phân tích lên tone cuối bài.")

        base_key, base_scale = self._resolve_base_tone(video_id, base_key, base_scale)
        if not base_key or not base_scale:
            raise ValueError("Thiếu tone gốc để phân tích lên tone cuối bài.")

        base_key = self.key_signature_service.normalize(base_key, base_scale)
        result = self.end_modulation_service.analyze_end_modulation(
            youtube_url=youtube_url,
            base_key=base_key,
            base_scale=base_scale,
            progress_cb=progress_cb,
        )
        result.update({
            "video_id": video_id,
            "url": youtube_url,
            "title": title or result.get("title", ""),
            "base_key": self.key_signature_service.normalize(
                result.get("base_key", base_key), result.get("base_scale", base_scale),
            ),
            "base_scale": str(result.get("base_scale", base_scale) or base_scale),
        })
        result["base_label"] = f"{result['base_key']} {result['base_scale']}"
        self._normalize_modulation_candidate(result)
        return result

    def save(self, youtube_url: str, title: str, modulation_result: dict, base_result: dict | None = None) -> dict:
        youtube_url = str(youtube_url or "").strip()
        video_id = self.tone_workflow_service.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id để lưu lên tone cuối bài.")

        base_result = base_result or self._cached_base_result(video_id)
        saved = self.tone_cache_service.save_end_modulation(
            youtube_url=youtube_url,
            title=title,
            modulation_result=modulation_result,
            base_result=base_result,
            source="auto_end_modulation_confirmed",
        )
        return {
            "ok": True,
            "video_id": video_id,
            "url": youtube_url,
            "title": title,
            "end_modulation": saved,
        }

    def _resolve_base_tone(self, video_id: str, base_key: str, base_scale: str) -> tuple[str, str]:
        if base_key and base_scale:
            return base_key, base_scale
        cached = self.tone_workflow_service.find_cached_tone_by_video_id(video_id)
        if not cached:
            return base_key, base_scale
        return str(cached.get("key", "") or "").strip(), str(cached.get("scale", "") or "").strip()

    def _normalize_modulation_candidate(self, result: dict) -> None:
        if not (result.get("has_modulation") or result.get("candidate_found") or result.get("new_key")):
            return
        new_scale = str(result.get("new_scale", result["base_scale"]) or result["base_scale"])
        new_key = self.key_signature_service.normalize(result.get("new_key", ""), new_scale)
        if not new_key:
            return
        result["new_key"] = new_key
        result["new_scale"] = new_scale
        result["new_key_index"] = self.key_signature_service.key_index(new_key)
        result["new_label"] = f"{new_key} {new_scale}"

    def _cached_base_result(self, video_id: str) -> dict:
        cached = self.tone_workflow_service.find_cached_tone_by_video_id(video_id)
        if not cached:
            return {}
        return {
            "key": cached.get("key", ""),
            "key_index": cached.get("key_index", 0),
            "scale": cached.get("scale", ""),
            "label": cached.get("label", cached.get("tone_label", "")),
            "confidence": cached.get("confidence", 0.0),
            "manual_verified": cached.get("manual_verified", False),
        }
