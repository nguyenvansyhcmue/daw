"""Coordinate cache, detection, correction, and application of YouTube tones."""


class ToneWorkflowService:
    """Own the complete tone workflow without depending on the UI controller."""

    def __init__(self, tone_service, tone_cache_service, song_service, key_signature_service, tone_application_service):
        self.tone_service = tone_service
        self.tone_cache_service = tone_cache_service
        self.song_service = song_service
        self.key_signature_service = key_signature_service
        self.tone_application_service = tone_application_service
        self.last_auto_detect_by_video_id = {}
        self.last_fix_tone_by_video_id = {}

    def extract_video_id(self, youtube_url: str) -> str:
        return self.tone_cache_service.extract_video_id(youtube_url)

    def find_cached_tone_by_video_id(self, video_id: str):
        """Return the authoritative saved song or the app's prior detection."""
        video_id = str(video_id or "").strip()
        if not video_id:
            return None

        song = self._find_song_by_video_id(video_id)
        if song:
            return self._cache_payload(
                source="songs",
                video_id=video_id,
                title=song.get("title", ""),
                key=song.get("key", "C"),
                scale=song.get("scale", "Major"),
                confidence=1.0,
                manual_verified=True,
            )

        cached = self.tone_cache_service.find_by_video_id(video_id)
        if not cached:
            return None

        self.tone_cache_service.mark_used(video_id)
        return self._cache_payload(
            source="tone_cache",
            video_id=video_id,
            title=cached.get("title", ""),
            key=cached.get("key", "C"),
            scale=cached.get("scale", "Major"),
            confidence=float(cached.get("confidence", 0.0) or 0.0),
            manual_verified=bool(cached.get("manual_verified", False)),
        )

    def find_cached_tone_by_url(self, youtube_url: str):
        video_id = self.extract_video_id(youtube_url)
        return self.find_cached_tone_by_video_id(video_id) if video_id else None

    def save_tone_cache_from_result(self, youtube_url: str, title: str, result: dict):
        return self.tone_cache_service.save_detect_result(
            youtube_url=youtube_url,
            title=title,
            result=result,
            algorithm_version="v4_safe",
            manual_verified=False,
        )

    def apply_cached_tone(self, cached: dict) -> dict:
        applied = self.tone_application_service.apply_manual(cached["key"], cached["scale"])
        cached["applied"] = applied
        return applied

    def detect_tone_from_youtube_url(self, youtube_url: str) -> dict:
        result = self.tone_service.detect_tone_from_youtube_url(youtube_url, verbose=False)
        return self._normalize_result(result)

    def auto_resolve_for_youtube(self, youtube_url: str, title: str = "") -> dict:
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube.")

        cached = self.find_cached_tone_by_video_id(video_id)
        if cached:
            applied = self.apply_cached_tone(cached)
            return {
                "status": "cached", "source": cached.get("source", "cache"),
                "video_id": video_id, "url": youtube_url,
                "title": cached.get("title", title), "key": cached["key"],
                "key_index": cached["key_index"], "scale": cached["scale"],
                "label": cached["label"], "confidence": cached.get("confidence", 1.0),
                "manual_verified": cached.get("manual_verified", False), "applied": applied,
            }

        result = self.detect_tone_from_youtube_url(youtube_url)
        self.last_auto_detect_by_video_id[video_id] = dict(result)
        saved = self.save_tone_cache_from_result(youtube_url, title or result.get("title", ""), result)
        applied = self.tone_application_service.apply_detected(result)
        return {
            "status": "detected", "source": "youtube_detect", "video_id": video_id,
            "url": youtube_url, "title": title or result.get("title", ""),
            "key": result["key"], "key_index": result["key_index"],
            "scale": result["scale"], "label": result["label"],
            "confidence": result.get("confidence", 0.0), "saved_cache": saved, "applied": applied,
        }

    def fix_for_youtube(self, youtube_url: str, title: str = "", current_key: str = "", current_scale: str = "") -> dict:
        youtube_url = str(youtube_url or "").strip()
        title = str(title or "").strip()
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube để FIX TONE.")

        current_result = self._current_result(current_key, current_scale)
        correction_hint = self._correction_hint(current_result.get("label", ""))
        result = self.tone_service.detect_tone_fix_mode(
            youtube_url, current_result=current_result, correction_hint=correction_hint, verbose=False,
        )
        result = self._normalize_result(result)
        result.update({
            "video_id": video_id, "url": youtube_url, "title": title or result.get("title", ""),
            "app_controller_base_key": current_result.get("key", ""),
            "app_controller_base_scale": current_result.get("scale", ""), "status": "fix_tone",
        })
        applied = self.tone_application_service.apply_detected(result)
        self.last_fix_tone_by_video_id[video_id] = dict(result)
        return {
            "status": "fix_tone", "source": result.get("source", "youtube_fix"),
            "video_id": video_id, "url": youtube_url, "title": title or result.get("title", ""),
            "key": result["key"], "key_index": result["key_index"], "scale": result["scale"],
            "label": result["label"], "confidence": result.get("confidence", 0.0),
            "fix_method": result.get("fix_method", ""),
            "previous_label": result.get("previous_label", current_result.get("label", "")),
            "correction_hint": result.get("correction_hint", correction_hint),
            "ranked": result.get("ranked", []), "applied": applied, "saved_cache": False,
        }

    def confirm_cache(self, youtube_url: str, title: str, key_name: str, scale_name: str) -> dict:
        youtube_url, title = str(youtube_url or "").strip(), str(title or "").strip()
        scale_name, key_name = str(scale_name or "Major").strip(), str(key_name or "C").strip()
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube để xác nhận tone.")

        key_name = self.key_signature_service.normalize(key_name, scale_name)
        result = {
            "video_id": video_id, "key": key_name,
            "key_index": self.key_signature_service.key_index(key_name),
            "scale": scale_name, "label": f"{key_name} {scale_name}", "confidence": 1.0,
        }
        auto_detected = self._auto_detection_for_confirmation(video_id)
        fix_result = self.last_fix_tone_by_video_id.get(video_id, {})
        saved = self.tone_cache_service.save_detect_result(
            youtube_url=youtube_url, title=title, result=result,
            algorithm_version="manual_verified", manual_verified=True,
        )
        try:
            correction_history = self.tone_cache_service.save_correction_history(
                youtube_url=youtube_url, title=title, auto_detected=auto_detected,
                corrected=result, fix_result=fix_result, source="manual_verified",
            )
        except Exception as error:
            correction_history = {"saved": False, "error": str(error)}

        applied = self.tone_application_service.apply_manual(key_name, scale_name)
        return {
            "status": "manual_verified", "source": "tone_cache", "video_id": video_id,
            "url": youtube_url, "title": title, "key": key_name,
            "key_index": result["key_index"], "scale": scale_name, "label": result["label"],
            "confidence": 1.0, "manual_verified": True, "saved_cache": saved,
            "correction_history": correction_history, "applied": applied,
        }

    def _find_song_by_video_id(self, video_id: str):
        for song in self.song_service.list_songs():
            if str(song.get("youtube_video_id", "")).strip() == video_id:
                return song
        return None

    def _cache_payload(self, source, video_id, title, key, scale, confidence, manual_verified):
        scale = str(scale or "Major").strip()
        key = self.key_signature_service.normalize(key, scale)
        return {
            "source": source, "video_id": video_id, "title": title, "key": key,
            "key_index": self.key_signature_service.key_index(key), "scale": scale,
            "label": f"{key} {scale}", "confidence": confidence, "manual_verified": manual_verified,
        }

    def _normalize_result(self, result: dict) -> dict:
        key = self.key_signature_service.normalize(result["key"], result["scale"])
        result["key"] = key
        result["key_index"] = self.key_signature_service.key_index(key)
        result["label"] = f"{key} {result['scale']}"
        return result

    def _current_result(self, current_key: str, current_scale: str) -> dict:
        current_key, current_scale = str(current_key or "").strip(), str(current_scale or "Major").strip()
        if not current_key:
            return {}
        key = self.key_signature_service.normalize(current_key, current_scale)
        return {
            "key": key, "key_index": self.key_signature_service.key_index(key),
            "scale": current_scale, "label": f"{key} {current_scale}",
        }

    def _correction_hint(self, detected_label: str):
        try:
            return self.tone_cache_service.get_correction_hint_for_detected_label(detected_label, min_count=1)
        except Exception:
            return None

    def _auto_detection_for_confirmation(self, video_id: str) -> dict:
        auto_detected = self.last_auto_detect_by_video_id.get(video_id, {})
        fix_result = self.last_fix_tone_by_video_id.get(video_id, {})
        if auto_detected or not fix_result.get("previous_label"):
            return auto_detected
        try:
            previous_label = str(fix_result["previous_label"] or "")
            previous_parts = previous_label.split()
            if len(previous_parts) >= 2:
                return {
                    "key": previous_parts[0],
                    "scale": "Minor" if previous_parts[1].lower().startswith("min") else "Major",
                    "label": previous_label, "confidence": 0.0,
                }
        except Exception:
            pass
        return {}
