from __future__ import annotations

import json
import shutil
from datetime import datetime
from pathlib import Path
from typing import Any


class DataTransferService:
    """
    Xuất / nhập dữ liệu tone cache và danh sách bài hát.

    File xuất ra là 1 file JSON duy nhất, gồm:
    - tone_cache: dữ liệu các bài đã dò / đã sửa tone
    - songs: dữ liệu D.S BÀI HÁT

    Khi nhập, service sẽ MERGE dữ liệu, không xóa trắng dữ liệu hiện tại.
    """

    def __init__(self):
        base_dir = Path(__file__).resolve().parents[2]
        self.base_dir = base_dir
        self.data_dir = base_dir / "data"
        self.data_dir.mkdir(parents=True, exist_ok=True)

        self.tone_cache_path = self.data_dir / "tone_cache.json"
        self.songs_path = self.data_dir / "songs.json"
        self.correction_history_path = self.data_dir / "correction_history.json"
        self.backup_dir = self.data_dir / "backups"
        self.backup_dir.mkdir(parents=True, exist_ok=True)

        if not self.tone_cache_path.exists():
            self._write_json(self.tone_cache_path, [])
        if not self.songs_path.exists():
            self._write_json(self.songs_path, [])
        if not self.correction_history_path.exists():
            self._write_json(self.correction_history_path, [])

    # =========================================================
    # BASIC JSON
    # =========================================================

    def now_text(self) -> str:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def now_file_text(self) -> str:
        return datetime.now().strftime("%Y%m%d_%H%M%S")

    def _read_json(self, path: Path, default: Any):
        try:
            if not path.exists():
                return default
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            return data
        except Exception:
            return default

    def _write_json(self, path: Path, data: Any):
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def _read_list(self, path: Path):
        data = self._read_json(path, [])
        return data if isinstance(data, list) else []

    def _backup_current_data(self) -> dict:
        stamp = self.now_file_text()
        result = {}

        if self.tone_cache_path.exists():
            dst = self.backup_dir / f"tone_cache_before_import_{stamp}.json"
            shutil.copy2(self.tone_cache_path, dst)
            result["tone_cache_backup"] = str(dst)

        if self.songs_path.exists():
            dst = self.backup_dir / f"songs_before_import_{stamp}.json"
            shutil.copy2(self.songs_path, dst)
            result["songs_backup"] = str(dst)

        if self.correction_history_path.exists():
            dst = self.backup_dir / f"correction_history_before_import_{stamp}.json"
            shutil.copy2(self.correction_history_path, dst)
            result["correction_history_backup"] = str(dst)

        return result

    # =========================================================
    # EXPORT
    # =========================================================

    def build_default_export_filename(self) -> str:
        return f"THM_TONE_DATA_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}.json"

    def export_data(self, output_path: str) -> dict:
        output = Path(output_path)
        if output.suffix.lower() != ".json":
            output = output.with_suffix(".json")

        tone_cache = self._read_list(self.tone_cache_path)
        songs = self._read_list(self.songs_path)
        correction_history = self._read_list(self.correction_history_path)

        payload = {
            "schema": "thm_tone_data_export",
            "schema_version": 1,
            "app_name": "THM Vocal Panel",
            "exported_at": self.now_text(),
            "data": {
                "tone_cache": tone_cache,
                "songs": songs,
                "correction_history": correction_history,
            },
            "stats": {
                "tone_cache_count": len(tone_cache),
                "songs_count": len(songs),
                "correction_history_count": len(correction_history),
                "manual_verified_count": sum(1 for x in tone_cache if bool(x.get("manual_verified", False))),
            },
        }

        self._write_json(output, payload)

        return {
            "ok": True,
            "file_path": str(output),
            "tone_cache_count": len(tone_cache),
            "songs_count": len(songs),
            "correction_history_count": len(correction_history),
            "manual_verified_count": payload["stats"]["manual_verified_count"],
        }

    # =========================================================
    # IMPORT / MERGE
    # =========================================================

    def import_data(self, input_path: str) -> dict:
        input_file = Path(input_path)
        if not input_file.exists():
            raise FileNotFoundError(f"Không tìm thấy file dữ liệu:\n{input_file}")

        payload = self._read_json(input_file, None)
        if not isinstance(payload, dict):
            raise ValueError("File nhập không đúng định dạng JSON của THM Vocal Panel.")

        # Hỗ trợ cả file chuẩn mới và trường hợp người dùng đưa trực tiếp list cũ.
        data = payload.get("data") if isinstance(payload.get("data"), dict) else payload

        import_tone_cache = data.get("tone_cache", []) if isinstance(data, dict) else []
        import_songs = data.get("songs", []) if isinstance(data, dict) else []
        import_correction_history = data.get("correction_history", []) if isinstance(data, dict) else []

        if not isinstance(import_tone_cache, list):
            import_tone_cache = []
        if not isinstance(import_songs, list):
            import_songs = []
        if not isinstance(import_correction_history, list):
            import_correction_history = []

        backups = self._backup_current_data()

        current_tone_cache = self._read_list(self.tone_cache_path)
        current_songs = self._read_list(self.songs_path)
        current_correction_history = self._read_list(self.correction_history_path)

        tone_result = self._merge_tone_cache(current_tone_cache, import_tone_cache)
        songs_result = self._merge_songs(current_songs, import_songs)
        correction_result = self._merge_correction_history(current_correction_history, import_correction_history)

        self._write_json(self.tone_cache_path, tone_result["merged"])
        self._write_json(self.songs_path, songs_result["merged"])
        self._write_json(self.correction_history_path, correction_result["merged"])

        return {
            "ok": True,
            "file_path": str(input_file),
            "backups": backups,
            "tone_cache_added": tone_result["added"],
            "tone_cache_updated": tone_result["updated"],
            "tone_cache_skipped": tone_result["skipped"],
            "songs_added": songs_result["added"],
            "songs_updated": songs_result["updated"],
            "songs_skipped": songs_result["skipped"],
            "correction_history_added": correction_result["added"],
            "correction_history_updated": correction_result["updated"],
            "correction_history_skipped": correction_result["skipped"],
            "tone_cache_total": len(tone_result["merged"]),
            "songs_total": len(songs_result["merged"]),
            "correction_history_total": len(correction_result["merged"]),
        }

    def _merge_tone_cache(self, current: list[dict], incoming: list[dict]) -> dict:
        merged = list(current)
        index = {}

        for i, item in enumerate(merged):
            vid = str(item.get("video_id", "") or "").strip()
            if vid:
                index[vid] = i

        added = 0
        updated = 0
        skipped = 0

        for raw in incoming:
            if not isinstance(raw, dict):
                skipped += 1
                continue

            vid = str(raw.get("video_id", "") or "").strip()
            if not vid:
                skipped += 1
                continue

            item = dict(raw)

            if vid not in index:
                merged.append(item)
                index[vid] = len(merged) - 1
                added += 1
                continue

            old = merged[index[vid]]
            old_verified = bool(old.get("manual_verified", False))
            new_verified = bool(item.get("manual_verified", False))

            # Quy tắc an toàn:
            # 1. Dữ liệu mới đã manual_verified thì được ưu tiên ghi đè.
            # 2. Dữ liệu cũ đã manual_verified còn dữ liệu mới chưa verified thì giữ cũ.
            # 3. Nếu cả hai chưa verified, giữ bản confidence cao hơn.
            if new_verified and not old_verified:
                merged[index[vid]] = item
                updated += 1
            elif new_verified and old_verified:
                merged[index[vid]] = self._prefer_newer_tone_item(old, item)
                updated += 1
            elif old_verified and not new_verified:
                skipped += 1
            else:
                old_conf = float(old.get("confidence", 0.0) or 0.0)
                new_conf = float(item.get("confidence", 0.0) or 0.0)
                if new_conf >= old_conf:
                    merged[index[vid]] = item
                    updated += 1
                else:
                    skipped += 1

        return {
            "merged": merged,
            "added": added,
            "updated": updated,
            "skipped": skipped,
        }

    def _prefer_newer_tone_item(self, old: dict, new: dict) -> dict:
        # Nếu cả hai đều verified, ưu tiên bản nhập vào vì thường là bộ dữ liệu chuẩn từ máy anh.
        # Vẫn giữ detect_count cao hơn nếu có.
        result = dict(new)
        try:
            result["detect_count"] = max(int(old.get("detect_count", 0) or 0), int(new.get("detect_count", 0) or 0))
        except Exception:
            pass
        return result

    def _merge_songs(self, current: list[dict], incoming: list[dict]) -> dict:
        merged = list(current)
        index = {}

        for i, item in enumerate(merged):
            key = self._song_unique_key(item)
            if key:
                index[key] = i

        added = 0
        updated = 0
        skipped = 0

        for raw in incoming:
            if not isinstance(raw, dict):
                skipped += 1
                continue

            key = self._song_unique_key(raw)
            if not key:
                skipped += 1
                continue

            item = dict(raw)

            if key not in index:
                merged.append(item)
                index[key] = len(merged) - 1
                added += 1
            else:
                # Với songs, ưu tiên dữ liệu nhập vào để đồng bộ bộ bài chuẩn từ máy anh.
                # Nhưng nếu item nhập thiếu id thì giữ id cũ.
                old = merged[index[key]]
                if not str(item.get("id", "") or "").strip() and str(old.get("id", "") or "").strip():
                    item["id"] = old.get("id")
                merged[index[key]] = item
                updated += 1

        return {
            "merged": merged,
            "added": added,
            "updated": updated,
            "skipped": skipped,
        }



    def _correction_unique_key(self, item: dict) -> str:
        vid = str(item.get("video_id", "") or "").strip()
        detected = str(item.get("detected_label", "") or "").strip()
        corrected = str(item.get("corrected_label", "") or "").strip()
        if vid:
            return f"video:{vid}"
        if detected and corrected:
            return f"pair:{detected}->{corrected}"
        return ""

    def _merge_correction_history(self, current: list[dict], incoming: list[dict]) -> dict:
        merged = list(current)
        index = {}

        for i, item in enumerate(merged):
            key = self._correction_unique_key(item)
            if key:
                index[key] = i

        added = 0
        updated = 0
        skipped = 0

        for raw in incoming:
            if not isinstance(raw, dict):
                skipped += 1
                continue

            item = dict(raw)
            key = self._correction_unique_key(item)
            if not key:
                skipped += 1
                continue

            if key not in index:
                merged.append(item)
                index[key] = len(merged) - 1
                added += 1
                continue

            old = merged[index[key]]
            old_time = str(old.get("corrected_at", "") or "")
            new_time = str(item.get("corrected_at", "") or "")

            # Ưu tiên record mới hơn. Nếu không có timestamp thì ưu tiên dữ liệu nhập.
            if not old_time or not new_time or new_time >= old_time:
                merged[index[key]] = item
                updated += 1
            else:
                skipped += 1

        return {
            "merged": merged,
            "added": added,
            "updated": updated,
            "skipped": skipped,
        }

    def _song_unique_key(self, item: dict) -> str:
        vid = str(item.get("youtube_video_id", "") or "").strip()
        if vid:
            return f"video:{vid}"

        url = str(item.get("youtube_url", "") or "").strip()
        if url:
            return f"url:{url}"

        song_id = str(item.get("id", "") or "").strip()
        if song_id:
            return f"id:{song_id}"

        title = str(item.get("title", "") or "").strip().lower()
        if title:
            return f"title:{title}"

        return ""
