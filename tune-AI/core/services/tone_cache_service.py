import json
from datetime import datetime
from pathlib import Path
from urllib.parse import parse_qs, urlparse


class ToneCacheService:
    """
    Bộ nhớ tone tự động.

    songs.json: bài lưu chính thức.
    tone_cache.json: bài app đã tự dò hoặc anh đã xác nhận thủ công.
    Nếu manual_verified=True thì kết quả được ưu tiên tuyệt đối và không bị ghi đè bởi auto detect.
    """

    def __init__(self):
        base_dir = Path(__file__).resolve().parents[2]
        self.data_dir = base_dir / "data"
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.cache_path = self.data_dir / "tone_cache.json"
        self.correction_history_path = self.data_dir / "correction_history.json"

        if not self.cache_path.exists():
            self._write([])
        if not self.correction_history_path.exists():
            self._write_json(self.correction_history_path, [])

    # =========================================================
    # JSON
    # =========================================================

    def _read(self):
        try:
            with open(self.cache_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, list):
                return data
            return []
        except Exception:
            return []

    def _write(self, data):
        with open(self.cache_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def _read_json(self, path: Path, default):
        try:
            if not path.exists():
                return default
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            return data
        except Exception:
            return default

    def _write_json(self, path: Path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def _label(self, key: str, scale: str) -> str:
        key = str(key or "").strip()
        scale = str(scale or "").strip()
        if not key or not scale:
            return ""
        return f"{key} {scale}".strip()

    # =========================================================
    # YOUTUBE HELPERS
    # =========================================================

    def extract_video_id(self, text: str) -> str:
        text = (text or "").strip()
        if not text:
            return ""

        if len(text) == 11 and "/" not in text and "?" not in text:
            return text

        try:
            parsed = urlparse(text)

            if parsed.netloc in ("youtu.be", "www.youtu.be"):
                return parsed.path.strip("/")

            if (
                "youtube.com" in parsed.netloc
                or "youtube-nocookie.com" in parsed.netloc
                or "music.youtube.com" in parsed.netloc
            ):
                qs = parse_qs(parsed.query)
                if "v" in qs:
                    return qs["v"][0]

                parts = [p for p in parsed.path.split("/") if p]

                if "shorts" in parts:
                    idx = parts.index("shorts")
                    if idx + 1 < len(parts):
                        return parts[idx + 1]

                if "embed" in parts:
                    idx = parts.index("embed")
                    if idx + 1 < len(parts):
                        return parts[idx + 1]

        except Exception:
            return ""

        return ""

    def canonical_url(self, video_id: str) -> str:
        video_id = (video_id or "").strip()
        if not video_id:
            return ""
        return f"https://www.youtube.com/watch?v={video_id}"

    def now_text(self) -> str:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # =========================================================
    # CACHE API
    # =========================================================

    def find_by_video_id(self, video_id: str):
        video_id = (video_id or "").strip()
        if not video_id:
            return None

        for item in self._read():
            if str(item.get("video_id", "")).strip() == video_id:
                return item

        return None

    def find_by_url(self, youtube_url: str):
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            return None
        return self.find_by_video_id(video_id)

    def save_detect_result(
        self,
        youtube_url: str,
        title: str,
        result: dict,
        algorithm_version: str = "v4_safe",
        manual_verified: bool = False,
    ):
        video_id = self.extract_video_id(youtube_url)

        if not video_id:
            video_id = str(result.get("video_id", "")).strip()

        if not video_id:
            raise ValueError("Không lưu cache được vì thiếu video_id.")

        data = self._read()
        now = self.now_text()

        clean = {
            "video_id": video_id,
            "canonical_url": self.canonical_url(video_id),
            "title": str(title or result.get("title", "") or "").strip(),
            "key": str(result.get("key", "")).strip(),
            "key_index": int(result.get("key_index", 0)),
            "scale": str(result.get("scale", "")).strip(),
            "tone_label": str(result.get("label", result.get("final_label", ""))).strip(),
            "confidence": float(result.get("confidence", 0.0) or 0.0),
            "algorithm_version": algorithm_version,
            "manual_verified": bool(manual_verified),
            "detected_at": now,
            "last_used_at": now,
            "detect_count": 1,
        }

        updated = False

        for i, item in enumerate(data):
            if str(item.get("video_id", "")).strip() == video_id:
                old_count = int(item.get("detect_count", 0) or 0)
                clean["detect_count"] = old_count + 1

                # Nếu bài cũ đã được anh xác nhận tay, auto detect không được ghi đè.
                if bool(item.get("manual_verified", False)) and not manual_verified:
                    item["last_used_at"] = now
                    item["detect_count"] = old_count + 1
                    data[i] = item
                else:
                    # Giữ lại thông tin lên tone cuối bài nếu đã từng lưu.
                    if isinstance(item.get("end_modulation"), dict):
                        clean["end_modulation"] = item.get("end_modulation")
                    data[i] = clean

                updated = True
                break

        if not updated:
            data.append(clean)

        self._write(data)
        return clean

    def mark_used(self, video_id: str):
        video_id = (video_id or "").strip()
        if not video_id:
            return None

        data = self._read()
        now = self.now_text()

        for i, item in enumerate(data):
            if str(item.get("video_id", "")).strip() == video_id:
                item["last_used_at"] = now
                data[i] = item
                self._write(data)
                return item

        return None

    def list_all(self):
        return self._read()

    # =========================================================
    # CORRECTION HISTORY / SAFE LEARNING
    # =========================================================

    def list_correction_history(self):
        data = self._read_json(self.correction_history_path, [])
        return data if isinstance(data, list) else []

    def save_correction_history(
        self,
        youtube_url: str,
        title: str,
        auto_detected: dict | None,
        corrected: dict,
        fix_result: dict | None = None,
        source: str = "manual_verified",
    ) -> dict:
        """
        Lưu lịch sử sửa sai an toàn.

        Dữ liệu này KHÔNG tự thay đổi thuật toán dò tone chính.
        Nó chỉ dùng để hỗ trợ nút FIX TONE khi người dùng chủ động bấm.
        """

        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            video_id = str((corrected or {}).get("video_id", "") or "").strip()
        if not video_id:
            raise ValueError("Không lưu correction history được vì thiếu video_id.")

        auto_detected = auto_detected or {}
        fix_result = fix_result or {}
        corrected = corrected or {}

        detected_label = str(auto_detected.get("label", "") or self._label(auto_detected.get("key"), auto_detected.get("scale"))).strip()
        corrected_label = str(corrected.get("label", "") or self._label(corrected.get("key"), corrected.get("scale"))).strip()

        if not corrected_label:
            raise ValueError("Không lưu correction history được vì thiếu tone đã sửa.")

        # Nếu không có detected_label hoặc detected giống corrected thì không cần ghi lịch sử sai.
        if not detected_label or detected_label == corrected_label:
            return {
                "saved": False,
                "reason": "no_meaningful_correction",
                "detected_label": detected_label,
                "corrected_label": corrected_label,
            }

        history = self.list_correction_history()
        now = self.now_text()

        record = {
            "video_id": video_id,
            "canonical_url": self.canonical_url(video_id),
            "title": str(title or corrected.get("title", "") or auto_detected.get("title", "") or "").strip(),
            "detected_key": str(auto_detected.get("key", "") or "").strip(),
            "detected_scale": str(auto_detected.get("scale", "") or "").strip(),
            "detected_label": detected_label,
            "corrected_key": str(corrected.get("key", "") or "").strip(),
            "corrected_scale": str(corrected.get("scale", "") or "").strip(),
            "corrected_label": corrected_label,
            "detected_confidence": float(auto_detected.get("confidence", 0.0) or 0.0),
            "fix_method": str(fix_result.get("fix_method", "manual") or "manual"),
            "fix_previous_label": str(fix_result.get("previous_label", "") or ""),
            "source": str(source or "manual_verified"),
            "manual_verified": True,
            "corrected_at": now,
        }

        # Ghi đè record cùng video_id để tránh phình file nếu sửa đi sửa lại.
        updated = False
        for i, item in enumerate(history):
            if str(item.get("video_id", "") or "").strip() == video_id:
                history[i] = record
                updated = True
                break

        if not updated:
            history.append(record)

        self._write_json(self.correction_history_path, history)
        record["saved"] = True
        return record

    def get_correction_hint_for_detected_label(self, detected_label: str, min_count: int = 1) -> dict | None:
        """
        Tìm gợi ý sửa tone dựa trên lịch sử.

        Chỉ dùng cho nút FIX TONE. Không dùng trong detect tự động lần 1.
        """

        detected_label = str(detected_label or "").strip()
        if not detected_label:
            return None

        grouped = {}
        for item in self.list_correction_history():
            if str(item.get("detected_label", "") or "").strip() != detected_label:
                continue
            corrected_label = str(item.get("corrected_label", "") or "").strip()
            if not corrected_label or corrected_label == detected_label:
                continue

            rec = grouped.setdefault(corrected_label, {
                "detected_label": detected_label,
                "corrected_label": corrected_label,
                "corrected_key": str(item.get("corrected_key", "") or "").strip(),
                "corrected_scale": str(item.get("corrected_scale", "") or "").strip(),
                "count": 0,
                "latest_at": "",
                "examples": [],
            })
            rec["count"] += 1
            corrected_at = str(item.get("corrected_at", "") or "")
            if corrected_at > rec["latest_at"]:
                rec["latest_at"] = corrected_at
            if len(rec["examples"]) < 5:
                rec["examples"].append({
                    "video_id": item.get("video_id", ""),
                    "title": item.get("title", ""),
                    "corrected_at": corrected_at,
                })

        if not grouped:
            return None

        best = sorted(grouped.values(), key=lambda x: (int(x.get("count", 0)), str(x.get("latest_at", ""))), reverse=True)[0]
        if int(best.get("count", 0)) < int(min_count):
            return None

        best["source"] = "correction_history"
        return best



    # =========================================================
    # END MODULATION / LÊN TONE CUỐI BÀI
    # =========================================================

    def find_end_modulation_by_video_id(self, video_id: str):
        item = self.find_by_video_id(video_id)
        if not item:
            return None
        data = item.get("end_modulation")
        return data if isinstance(data, dict) else None

    def find_end_modulation_by_url(self, youtube_url: str):
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            return None
        return self.find_end_modulation_by_video_id(video_id)

    def save_end_modulation(
        self,
        youtube_url: str,
        title: str,
        modulation_result: dict,
        base_result: dict | None = None,
        source: str = "auto_end_modulation_confirmed",
    ):
        """
        Lưu thông tin bài có lên tone cuối bài vào tone_cache.json.
        Không tự gửi MIDI ở đây. Lần sau app có thể dùng dữ liệu này để tự đổi key/scale.
        """
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            video_id = str((modulation_result or {}).get("video_id", "") or "").strip()
        if not video_id:
            raise ValueError("Không lưu lên tone cuối bài được vì thiếu video_id.")

        base_result = base_result or {}
        modulation_result = modulation_result or {}
        now = self.now_text()

        base_key = str(modulation_result.get("base_key", base_result.get("key", "")) or "").strip()
        base_scale = str(modulation_result.get("base_scale", base_result.get("scale", "")) or "").strip()
        base_label = str(modulation_result.get("base_label", base_result.get("label", self._label(base_key, base_scale))) or "").strip()

        end_key = str(modulation_result.get("new_key", "") or "").strip()
        end_scale = str(modulation_result.get("new_scale", "") or "").strip()
        end_label = str(modulation_result.get("new_label", self._label(end_key, end_scale)) or "").strip()

        if not base_key or not base_scale or not end_key or not end_scale:
            raise ValueError("Thiếu key/scale đầu hoặc cuối bài để lưu lên tone.")

        end_modulation = {
            "enabled": True,
            "source": str(source or "auto_end_modulation_confirmed"),
            "base_key": base_key,
            "base_scale": base_scale,
            "base_label": base_label,
            "end_key": end_key,
            "end_key_index": int(modulation_result.get("new_key_index", base_result.get("key_index", 0)) or 0),
            "end_scale": end_scale,
            "end_label": end_label,
            "semitone_up": int(modulation_result.get("semitone_up", 0) or 0),
            "raise_time_sec": int(modulation_result.get("raise_time_sec", 0) or 0),
            "raise_time_text": str(modulation_result.get("raise_time_text", "00:00") or "00:00"),
            "apply_time_sec": int(modulation_result.get("apply_time_sec", max(0, int(modulation_result.get("raise_time_sec", 0) or 0) - 3)) or 0),
            "apply_time_text": str(modulation_result.get("apply_time_text", "") or ""),
            "confidence": float(modulation_result.get("confidence", 0.0) or 0.0),
            "saved_at": now,
            "manual_confirmed": True,
        }

        data = self._read()
        updated = False
        for i, item in enumerate(data):
            if str(item.get("video_id", "") or "").strip() == video_id:
                item["title"] = str(title or item.get("title", "") or "").strip()
                item["end_modulation"] = end_modulation
                item["last_used_at"] = now
                data[i] = item
                updated = True
                break

        if not updated:
            data.append({
                "video_id": video_id,
                "canonical_url": self.canonical_url(video_id),
                "title": str(title or modulation_result.get("title", "") or "").strip(),
                "key": base_key,
                "key_index": int(base_result.get("key_index", 0) or 0),
                "scale": base_scale,
                "tone_label": base_label,
                "confidence": float(base_result.get("confidence", 0.0) or 0.0),
                "algorithm_version": "end_modulation_base",
                "manual_verified": bool(base_result.get("manual_verified", False)),
                "detected_at": now,
                "last_used_at": now,
                "detect_count": 1,
                "end_modulation": end_modulation,
            })

        self._write(data)
        return end_modulation

    # =========================================================
    # CLEAR CACHE / TESTING
    # =========================================================

    def clear_all_cache_for_testing(self, make_backup: bool = True) -> dict:
        """
        Xóa cache tone để test lại từ đầu.

        KHÔNG xóa songs.json / D.S BÀI HÁT.
        Chỉ reset các dữ liệu tự dò / sửa tone / lên tone cuối bài:
        - data/tone_cache.json
        - data/correction_history.json
        - data/end_modulation_last_result.json
        """
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        tone_cache = self._read()
        correction_history = self._read_json(self.correction_history_path, [])
        if not isinstance(correction_history, list):
            correction_history = []

        backup_files = {}
        if make_backup:
            backup_dir = self.data_dir / "backups" / f"clear_cache_{stamp}"
            backup_dir.mkdir(parents=True, exist_ok=True)

            if self.cache_path.exists():
                tone_backup = backup_dir / "tone_cache.json"
                tone_backup.write_text(self.cache_path.read_text(encoding="utf-8"), encoding="utf-8")
                backup_files["tone_cache"] = str(tone_backup)

            if self.correction_history_path.exists():
                correction_backup = backup_dir / "correction_history.json"
                correction_backup.write_text(self.correction_history_path.read_text(encoding="utf-8"), encoding="utf-8")
                backup_files["correction_history"] = str(correction_backup)

            last_result_path = self.data_dir / "end_modulation_last_result.json"
            if last_result_path.exists():
                last_result_backup = backup_dir / "end_modulation_last_result.json"
                last_result_backup.write_text(last_result_path.read_text(encoding="utf-8"), encoding="utf-8")
                backup_files["end_modulation_last_result"] = str(last_result_backup)

        self._write([])
        self._write_json(self.correction_history_path, [])

        removed_extra_files = []
        for extra_name in ["end_modulation_last_result.json", "end_modulation_debug.json"]:
            p = self.data_dir / extra_name
            try:
                if p.exists():
                    p.unlink()
                    removed_extra_files.append(str(p))
            except Exception:
                pass

        return {
            "ok": True,
            "cleared_at": now,
            "tone_cache_count": len(tone_cache),
            "correction_history_count": len(correction_history),
            "removed_extra_files": removed_extra_files,
            "backup_files": backup_files,
            "message": "Đã xóa cache tone/test. D.S BÀI HÁT không bị xóa.",
        }

