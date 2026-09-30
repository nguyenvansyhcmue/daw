import json
import subprocess
import sys
from pathlib import Path

from core.services.midi_service import MidiService
from core.services.tone_service import ToneService
from core.services.song_service import SongService
from core.services.mode_settings_service import ModeSettingsService
from core.services.realtime_tone_service import RealtimeToneService
from core.services.settings_service import SettingsService
from core.services.autokey_service import AutoKeyService
from core.services.tone_cache_service import ToneCacheService
from core.services.browser_monitor_service import BrowserMonitorService
from core.services.browser_launcher_service import BrowserLauncherService
from core.services.data_transfer_service import DataTransferService
from core.services.end_modulation_service import EndModulationService
from core.constants import MIC1_CC, MIC2_CC, MODE_PRESETS, DEFAULT_UI_VALUES


class AppController:
    def __init__(self):
        self.midi = MidiService()
        self.tone_service = ToneService()
        self.song_service = SongService()
        self.mode_settings_service = ModeSettingsService()
        self.realtime_service = RealtimeToneService()

        self.settings_service = SettingsService()
        self.settings = self.settings_service.load()

        self.autokey_service = AutoKeyService()
        self.tone_cache_service = ToneCacheService()
        self.browser_launcher_service = BrowserLauncherService()
        self.browser_monitor_service = BrowserMonitorService()
        self.data_transfer_service = DataTransferService()
        self.end_modulation_service = EndModulationService()

        self.current_mic_name = "MIC 1"
        self.current_cc = MIC1_CC

        self.current_mode = None
        self.lofi_on = False
        self.global_pitch = 0

        # Lưu kết quả tự dò / fix gần nhất để ghi correction history an toàn.
        # Dữ liệu này KHÔNG can thiệp vào detect tự động lần 1.
        self.last_auto_detect_by_video_id = {}
        self.last_fix_tone_by_video_id = {}

        self.scale_options = [
            "Major",
            "Minor",
            "Chromatic",
            "Ling Lun",
            "Scholar's Lute",
            "Greek Diatonic",
            "Greek Chromatic",
            "Greek Enharmonic",
            "Pythagorean",
            "Just (Major)",
            "Just (Minor)",
            "Meantone Chromatic",
            "Werckmeister I (III)",
            "Vallotti & Young",
            "Barnes-Bach",
            "Indian",
            "Slendro",
            "Pelog",
            "Arabic 1",
            "Arabic 2",
            "19 Tone",
            "24 Tone",
            "31 Tone",
            "53 Tone",
            "Partch",
            "Carlos A",
            "Carlos B",
            "Carlos G",
            "Harmonic",
        ]

        self.major_note_names = ['C', 'Db', 'D', 'Eb', 'E', 'F',
                                 'F#', 'G', 'Ab', 'A', 'Bb', 'B']
        self.minor_note_names = ['C', 'C#', 'D', 'Eb', 'E', 'F',
                                 'F#', 'G', 'G#', 'A', 'Bb', 'B']

        self.note_to_index = {
            "C": 0,
            "C#": 1, "Db": 1,
            "D": 2,
            "D#": 3, "Eb": 3,
            "E": 4,
            "F": 5,
            "F#": 6, "Gb": 6,
            "G": 7,
            "G#": 8, "Ab": 8,
            "A": 9,
            "A#": 10, "Bb": 10,
            "B": 11,
        }

    # =========================================================
    # APP LIFECYCLE
    # =========================================================

    def startup(self):
        opened_name = self.midi.open()

        # Không tự mở Brave khi khởi động app nữa.
        # Brave sẽ được mở chủ động bằng nút KARAOKE để ổn định hơn trên máy khách.

        from core.services import tone_service as _tone_module
        _tone_module.set_ytdlp_cookies_file(self.settings.get("ytdlp_cookies_file", ""))

        return f"Control: {opened_name}"

    def shutdown(self):
        self.stop_realtime()
        self.midi.close()

        # Đóng tất cả cửa sổ Brave khi tắt app (theo yêu cầu user).
        # Không có flag/setting → luôn đóng.
        try:
            result = self.browser_launcher_service.close_all_brave()
            if result.get("closed"):
                print(f"[Shutdown] Đã đóng Brave ({result.get('method', '?')})")
            elif result.get("error"):
                print(f"[Shutdown] Không đóng được Brave: {result.get('error')}")
        except Exception as e:
            print(f"[Shutdown] Lỗi đóng Brave: {e}")

    # =========================================================
    # SETTINGS / AUTO KEY
    # =========================================================

    def get_settings(self):
        return self.settings

    def save_settings_payload(self, payload: dict):
        if not isinstance(payload, dict):
            return

        for key in ["ytdlp_cookies_file"]:
            if key in payload:
                self.settings[key] = payload[key]

        self.settings_service.settings = self.settings
        self.settings_service.save()

        if "ytdlp_cookies_file" in payload:
            from core.services import tone_service as _tone_module
            _tone_module.set_ytdlp_cookies_file(self.settings.get("ytdlp_cookies_file", ""))

        if "autokey" in payload and isinstance(payload["autokey"], dict):
            self.autokey_service.update_config(payload["autokey"])

    def get_autokey_config(self):
        return self.autokey_service.get_config()

    def capture_target_window_from_cursor(self):
        return self.autokey_service.capture_target_from_cursor()

    def capture_autokey_point(self, point_name: str):
        return self.autokey_service.capture_point_from_cursor(point_name)

    def run_autokey(self):
        self.autokey_service.run_cycle()

    # =========================================================
    # KEY DISPLAY HELPERS
    # =========================================================

    def get_key_options_for_scale(self, scale_name: str):
        scale_name = (scale_name or "").strip().lower()
        if scale_name == "minor":
            return list(self.minor_note_names)
        return list(self.major_note_names)

    def normalize_key_for_scale(self, key_name: str, scale_name: str) -> str:
        key_name = (key_name or "").strip()
        if key_name not in self.note_to_index:
            return key_name

        idx = self.note_to_index[key_name]
        scale_name = (scale_name or "").strip().lower()

        if scale_name == "minor":
            return self.minor_note_names[idx]
        return self.major_note_names[idx]

    def key_name_to_index(self, key_name: str) -> int:
        key_name = (key_name or "").strip()
        if key_name not in self.note_to_index:
            raise ValueError(f"Key không hợp lệ: {key_name}")
        return self.note_to_index[key_name]

    # =========================================================
    # MIC SELECT
    # =========================================================

    def set_active_mic(self, mic_name: str):
        self.current_mic_name = mic_name
        self.current_cc = MIC2_CC if mic_name == "MIC 2" else MIC1_CC
        self.current_mode = None
        self.lofi_on = False
        self.midi.send_cc(self.current_cc["lofi"], 0)

    def get_active_mic_name(self):
        return self.current_mic_name

    def is_lofi_on(self):
        return self.lofi_on

    # =========================================================
    # MONITOR
    # =========================================================

    def toggle_vang(self, is_on: bool):
        value = 127 if is_on else 0
        for cc in self.current_cc["vang_group"]:
            self.midi.send_cc(cc, value)

    def toggle_mic(self, is_on: bool):
        value = 127 if is_on else 0
        self.midi.send_cc(self.current_cc["mic_toggle"], value)

    # =========================================================
    # LOFI
    # =========================================================

    def toggle_lofi(self):
        self.lofi_on = not self.lofi_on
        self.midi.send_cc(self.current_cc["lofi"], 127 if self.lofi_on else 0)
        return self.lofi_on

    def set_lofi(self, is_on: bool):
        self.lofi_on = bool(is_on)
        self.midi.send_cc(self.current_cc["lofi"], 127 if self.lofi_on else 0)

    # =========================================================
    # EFFECT / VOLUME
    # =========================================================

    def _pct_to_cc(self, value_0_100: int) -> int:
        value_0_100 = max(0, min(100, int(value_0_100)))
        return int(round(value_0_100 * 127 / 100))

    def set_reverb_short(self, value: int):
        self.midi.send_cc(self.current_cc["reverb_short"], self._pct_to_cc(value))

    def set_reverb_long(self, value: int):
        self.midi.send_cc(self.current_cc["reverb_long"], self._pct_to_cc(value))

    def set_echo(self, value: int):
        self.midi.send_cc(self.current_cc["echo"], self._pct_to_cc(value))

    def set_mic_volume(self, value: int):
        self.midi.send_cc(self.current_cc["mic_vol"], self._pct_to_cc(value))

    def set_music_volume(self, value: int):
        self.midi.send_cc(self.current_cc["music_vol"], self._pct_to_cc(value))

    def set_tune_from_slider_position(self, raw_position_0_100: int):
        raw_position_0_100 = max(0, min(100, int(raw_position_0_100)))
        cc_value = int(round(raw_position_0_100 * 127 / 100))
        self.midi.send_cc(self.current_cc["tune"], cc_value)

    # =========================================================
    # GLOBAL PITCH
    # =========================================================

    def set_global_pitch(self, semitone: int):
        """
        Tăng / giảm tone nhạc trong trạng thái điều khiển nội bộ.
        """

        semitone = max(-12, min(12, int(semitone)))
        self.global_pitch = semitone

        cc_value = int(round((semitone + 12) * 127 / 24))
        cc_value = max(0, min(127, cc_value))

        self.midi.send_cc(36, cc_value)

        return {
            "semitone": semitone,
            "cc_value": cc_value,
            "control": "internal_pitch",
        }

    # =========================================================
    # MODE PRESET
    # =========================================================

    def apply_mode(self, mode_name: str):
        if mode_name not in MODE_PRESETS:
            raise ValueError(f"Mode không tồn tại hoặc không dùng preset: {mode_name}")

        self.current_mode = mode_name
        preset = self.mode_settings_service.get_mode(mode_name)

        self.set_reverb_short(preset["reverb_short"])
        self.set_reverb_long(preset["reverb_long"])
        self.set_echo(preset["echo"])
        self.set_mic_volume(preset["mic_vol"])
        self.set_music_volume(preset["music_vol"])

        return preset

    def remember_mode_slider_value(self, field: str, value):
        if self.current_mode in MODE_PRESETS:
            self.mode_settings_service.update_mode_value(self.current_mode, field, value)

    def get_default_ui_values(self):
        return DEFAULT_UI_VALUES.copy()

    # =========================================================
    # TONE DETECT FROM FILE
    # =========================================================

    def detect_tone_from_file(self, file_path: str) -> dict:
        result = self.tone_service.detect_tone_from_file(file_path)

        normalized_key = self.normalize_key_for_scale(result["key"], result["scale"])
        result["key"] = normalized_key
        result["label"] = f"{normalized_key} {result['scale']}"

        return result

    # =========================================================
    # TONE APPLY
    # =========================================================

    def _enum_index_to_cc(self, index: int, total_items: int) -> int:
        if total_items <= 1:
            return 0
        index = max(0, min(total_items - 1, index))
        return int(round(index * 127 / (total_items - 1)))

    def apply_detected_tone(self, tone_result: dict) -> dict:
        """
        Lưu key/scale đã dò vào trạng thái điều khiển nội bộ cho cả hai mic.
        Không gửi MIDI hay thao tác một DAW bên ngoài.
        """

        key_index = int(tone_result["key_index"])
        scale_name = str(tone_result["scale"]).strip().lower()

        key_cc_value = self._enum_index_to_cc(key_index, 12)
        scale_index = 1 if scale_name == "minor" else 0
        scale_cc_value = self._enum_index_to_cc(scale_index, len(self.scale_options))

        applied_targets = []

        for mic_name, cc_map in (("MIC 1", MIC1_CC), ("MIC 2", MIC2_CC)):
            self.midi.send_cc(cc_map["at_key"], key_cc_value)
            self.midi.send_cc(cc_map["at_scale"], scale_cc_value)

            applied_targets.append({
                "mic": mic_name,
                "key_cc": cc_map["at_key"],
                "scale_cc": cc_map["at_scale"],
            })

        return {
            "key_cc_value": key_cc_value,
            "scale_cc_value": scale_cc_value,
            "scale_index": scale_index,
            "targets": applied_targets,
        }

    def apply_manual_tone(self, key_name: str, scale_name: str):
        scale_name = scale_name.strip()
        key_index = self.key_name_to_index(key_name)
        display_key = self.normalize_key_for_scale(key_name, scale_name)

        tone_result = {
            "key": display_key,
            "key_index": key_index,
            "scale": scale_name,
            "label": f"{display_key} {scale_name}",
        }
        return self.apply_detected_tone(tone_result)

    def apply_realtime_tone(self, key_name: str, scale_name: str):
        display_key = self.normalize_key_for_scale(key_name, scale_name)
        key_index = self.key_name_to_index(display_key)

        tone_result = {
            "key": display_key,
            "key_index": key_index,
            "scale": scale_name,
            "label": f"{display_key} {scale_name}",
        }
        applied = self.apply_detected_tone(tone_result)
        tone_result["applied"] = applied
        return tone_result

    # =========================================================
    # SONG CRUD
    # =========================================================

    def list_songs(self, keyword: str = ""):
        if keyword.strip():
            return self.song_service.search_songs(keyword)
        return self.song_service.list_songs()

    def save_song(self, song_data: dict):
        if song_data.get("key") and song_data.get("scale"):
            song_data["key"] = self.normalize_key_for_scale(song_data["key"], song_data["scale"])
            song_data["tone_label"] = f"{song_data['key']} {song_data['scale']}"

        if song_data.get("end_key") and song_data.get("end_scale"):
            song_data["end_key"] = self.normalize_key_for_scale(song_data["end_key"], song_data["end_scale"])

        return self.song_service.save_song(song_data)

    def delete_song(self, song_id: str):
        self.song_service.delete_song(song_id)

    def get_song(self, song_id: str):
        song = self.song_service.get_song(song_id)
        if not song:
            return None

        if song.get("key") and song.get("scale"):
            song["key"] = self.normalize_key_for_scale(song["key"], song["scale"])
        if song.get("end_key") and song.get("end_scale"):
            song["end_key"] = self.normalize_key_for_scale(song["end_key"], song["end_scale"])
        return song

    def find_song_by_video_id(self, video_id: str):
        songs = self.song_service.list_songs()
        for song in songs:
            if str(song.get("youtube_video_id", "")).strip() == str(video_id).strip():
                return song
        return None

    # =========================================================
    # TONE CACHE
    # =========================================================

    def extract_video_id(self, youtube_url: str) -> str:
        return self.tone_cache_service.extract_video_id(youtube_url)

    def find_cached_tone_by_video_id(self, video_id: str):
        """
        Ưu tiên:
        1. songs.json: bài đã lưu chính thức.
        2. tone_cache.json: bài app từng tự dò.
        """

        video_id = str(video_id or "").strip()
        if not video_id:
            return None

        song = self.find_song_by_video_id(video_id)
        if song:
            key = self.normalize_key_for_scale(song.get("key", "C"), song.get("scale", "Major"))
            scale = str(song.get("scale", "Major")).strip()

            return {
                "source": "songs",
                "video_id": video_id,
                "title": song.get("title", ""),
                "key": key,
                "key_index": self.key_name_to_index(key),
                "scale": scale,
                "label": f"{key} {scale}",
                "confidence": 1.0,
                "manual_verified": True,
            }

        cached = self.tone_cache_service.find_by_video_id(video_id)
        if cached:
            key = self.normalize_key_for_scale(cached.get("key", "C"), cached.get("scale", "Major"))
            scale = str(cached.get("scale", "Major")).strip()

            self.tone_cache_service.mark_used(video_id)

            return {
                "source": "tone_cache",
                "video_id": video_id,
                "title": cached.get("title", ""),
                "key": key,
                "key_index": self.key_name_to_index(key),
                "scale": scale,
                "label": f"{key} {scale}",
                "confidence": float(cached.get("confidence", 0.0) or 0.0),
                "manual_verified": bool(cached.get("manual_verified", False)),
            }

        return None

    def find_cached_tone_by_url(self, youtube_url: str):
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            return None
        return self.find_cached_tone_by_video_id(video_id)

    def save_tone_cache_from_result(self, youtube_url: str, title: str, result: dict):
        return self.tone_cache_service.save_detect_result(
            youtube_url=youtube_url,
            title=title,
            result=result,
            algorithm_version="v4_safe",
            manual_verified=False,
        )

    def apply_cached_tone(self, cached: dict):
        key = cached["key"]
        scale = cached["scale"]

        applied = self.apply_manual_tone(key, scale)
        cached["applied"] = applied
        return applied

    # =========================================================
    # EXTERNAL BROWSER AUTO DETECT
    # =========================================================

    def get_current_external_youtube_video(self):
        return self.browser_monitor_service.get_current_youtube_video()

    def detect_tone_from_youtube_url(self, youtube_url: str) -> dict:
        result = self.tone_service.detect_tone_from_youtube_url(youtube_url, verbose=False)

        normalized_key = self.normalize_key_for_scale(result["key"], result["scale"])
        result["key"] = normalized_key
        result["label"] = f"{normalized_key} {result['scale']}"

        return result

    def auto_resolve_tone_for_youtube(self, youtube_url: str, title: str = "") -> dict:
        """
        Quy trình tự động:
        1. Tách video_id.
        2. Tìm trong songs.json.
        3. Tìm trong tone_cache.json.
        4. Nếu chưa có thì tự detect bằng ToneService V4 Safe.
        5. Lưu cache.
        6. Lưu key/scale vào trạng thái điều khiển nội bộ.
        """

        video_id = self.extract_video_id(youtube_url)

        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube.")

        cached = self.find_cached_tone_by_video_id(video_id)

        if cached:
            applied = self.apply_cached_tone(cached)

            return {
                "status": "cached",
                "source": cached.get("source", "cache"),
                "video_id": video_id,
                "url": youtube_url,
                "title": cached.get("title", title),
                "key": cached["key"],
                "key_index": cached["key_index"],
                "scale": cached["scale"],
                "label": cached["label"],
                "confidence": cached.get("confidence", 1.0),
                "manual_verified": cached.get("manual_verified", False),
                "applied": applied,
            }

        result = self.detect_tone_from_youtube_url(youtube_url)
        self.last_auto_detect_by_video_id[video_id] = dict(result)

        saved = self.save_tone_cache_from_result(
            youtube_url=youtube_url,
            title=title or result.get("title", ""),
            result=result,
        )

        applied = self.apply_detected_tone(result)

        return {
            "status": "detected",
            "source": "youtube_detect",
            "video_id": video_id,
            "url": youtube_url,
            "title": title or result.get("title", ""),
            "key": result["key"],
            "key_index": result["key_index"],
            "scale": result["scale"],
            "label": result["label"],
            "confidence": result.get("confidence", 0.0),
            "saved_cache": saved,
            "applied": applied,
        }



    def fix_tone_for_youtube_url(self, youtube_url: str, title: str = "", current_key: str = "", current_scale: str = "") -> dict:
        """
        FIX TONE V2 Balanced:
        - Phân tích sâu hơn detect thường bằng Deep Musician Mode.
        - Cân bằng intro, main segment, tonal center, cadence, bass/root, bậc và relative.
        - Chỉ apply thử Key/Scale mới, KHÔNG tự lưu cache.
        - Nếu đúng, người dùng bấm SỬA TONE để lưu manual_verified=True.
        """

        youtube_url = str(youtube_url or "").strip()
        title = str(title or "").strip()

        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube để FIX TONE.")

        current_scale = str(current_scale or "Major").strip()
        current_key = str(current_key or "").strip()

        current_result = {}
        if current_key and current_scale:
            normalized_key = self.normalize_key_for_scale(current_key, current_scale)
            current_result = {
                "key": normalized_key,
                "key_index": self.key_name_to_index(normalized_key),
                "scale": current_scale,
                "label": f"{normalized_key} {current_scale}",
            }

        correction_hint = None
        try:
            correction_hint = self.tone_cache_service.get_correction_hint_for_detected_label(
                current_result.get("label", ""),
                min_count=1,
            )
        except Exception:
            correction_hint = None

        result = self.tone_service.detect_tone_fix_mode(
            youtube_url,
            current_result=current_result,
            correction_hint=correction_hint,
            verbose=False,
        )

        normalized_key = self.normalize_key_for_scale(result["key"], result["scale"])
        result["key"] = normalized_key
        result["key_index"] = self.key_name_to_index(normalized_key)
        result["label"] = f"{normalized_key} {result['scale']}"
        result["video_id"] = video_id
        result["url"] = youtube_url
        result["title"] = title or result.get("title", "")
        result["app_controller_base_key"] = current_result.get("key", "")
        result["app_controller_base_scale"] = current_result.get("scale", "")
        result["status"] = "fix_tone"

        applied = self.apply_detected_tone(result)
        self.last_fix_tone_by_video_id[video_id] = dict(result)

        return {
            "status": "fix_tone",
            "source": result.get("source", "youtube_fix"),
            "video_id": video_id,
            "url": youtube_url,
            "title": title or result.get("title", ""),
            "key": result["key"],
            "key_index": result["key_index"],
            "scale": result["scale"],
            "label": result["label"],
            "confidence": result.get("confidence", 0.0),
            "fix_method": result.get("fix_method", ""),
            "previous_label": result.get("previous_label", current_result.get("label", "")),
            "correction_hint": result.get("correction_hint", correction_hint),
            "ranked": result.get("ranked", []),
            "applied": applied,
            "saved_cache": False,
        }

    def confirm_tone_cache_by_url(self, youtube_url: str, title: str, key_name: str, scale_name: str) -> dict:
        """
        Anh xác nhận/sửa tone đúng cho video YouTube hiện tại.
        Kết quả được lưu vào tone_cache.json với manual_verified=True.
        Lần sau gặp lại video này, app ưu tiên kết quả này tuyệt đối.
        """

        youtube_url = str(youtube_url or "").strip()
        title = str(title or "").strip()
        scale_name = str(scale_name or "Major").strip()
        key_name = str(key_name or "C").strip()

        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id từ URL YouTube để xác nhận tone.")

        key_name = self.normalize_key_for_scale(key_name, scale_name)
        key_index = self.key_name_to_index(key_name)

        result = {
            "video_id": video_id,
            "key": key_name,
            "key_index": key_index,
            "scale": scale_name,
            "label": f"{key_name} {scale_name}",
            "confidence": 1.0,
        }

        auto_detected = self.last_auto_detect_by_video_id.get(video_id, {})
        fix_result = self.last_fix_tone_by_video_id.get(video_id, {})

        # Nếu người dùng bấm SỬA TONE ngay sau FIX TONE, previous_label của fix chính là tone sai trước đó.
        if not auto_detected and fix_result.get("previous_label"):
            try:
                prev_label = str(fix_result.get("previous_label", "") or "")
                prev_parts = prev_label.split()
                if len(prev_parts) >= 2:
                    prev_scale = prev_parts[1].capitalize()
                    auto_detected = {
                        "key": prev_parts[0],
                        "scale": "Minor" if prev_scale.lower().startswith("min") else "Major",
                        "label": prev_label,
                        "confidence": 0.0,
                    }
            except Exception:
                auto_detected = {}

        saved = self.tone_cache_service.save_detect_result(
            youtube_url=youtube_url,
            title=title,
            result=result,
            algorithm_version="manual_verified",
            manual_verified=True,
        )

        correction_history = None
        try:
            correction_history = self.tone_cache_service.save_correction_history(
                youtube_url=youtube_url,
                title=title,
                auto_detected=auto_detected,
                corrected=result,
                fix_result=fix_result,
                source="manual_verified",
            )
        except Exception as e:
            correction_history = {"saved": False, "error": str(e)}

        applied = self.apply_manual_tone(key_name, scale_name)

        return {
            "status": "manual_verified",
            "source": "tone_cache",
            "video_id": video_id,
            "url": youtube_url,
            "title": title,
            "key": key_name,
            "key_index": key_index,
            "scale": scale_name,
            "label": f"{key_name} {scale_name}",
            "confidence": 1.0,
            "manual_verified": True,
            "saved_cache": saved,
            "correction_history": correction_history,
            "applied": applied,
        }



    # =========================================================
    # END MODULATION / LÊN TONE CUỐI BÀI
    # =========================================================

    def find_saved_end_modulation_by_url(self, youtube_url: str):
        try:
            return self.tone_cache_service.find_end_modulation_by_url(youtube_url)
        except Exception:
            return None

    def diagnose_brave_cdp(self) -> dict:
        """Trả về trạng thái CDP cho UI hiển thị diagnostic."""
        try:
            return self.browser_launcher_service.diagnose_cdp()
        except Exception as e:
            return {"is_brave_running": False, "cdp_port_open": False, "tabs_count": 0, "error": str(e)}

    def restart_brave_with_cdp(self, start_url: str = "https://www.youtube.com") -> dict:
        """Đóng Brave hiện tại và mở lại với cấu hình CDP đúng."""
        try:
            return self.browser_launcher_service.restart_brave_with_cdp(start_url=start_url)
        except Exception as e:
            return {"ok": False, "error": str(e)}

    def invalidate_end_modulation_audio_cache(self):
        """
        P1 — Xóa cache audio + processor đã tải.
        UI gọi khi user đổi bài để giải phóng RAM và tránh dùng cache cũ.
        """
        try:
            self.end_modulation_service.invalidate_cache()
        except Exception:
            pass

    def analyze_end_modulation_for_youtube(
        self,
        youtube_url: str,
        title: str = "",
        base_key: str = "",
        base_scale: str = "",
        progress_cb=None,
    ) -> dict:
        """
        Phân tích đoạn cuối bài có lên tone +1/+2 hay không.
        Chỉ phân tích dựa trên tone gốc đã đáng tin. Không tự lưu, không tự gửi MIDI.
        """
        youtube_url = str(youtube_url or "").strip()
        title = str(title or "").strip()
        base_key = str(base_key or "").strip()
        base_scale = str(base_scale or "").strip()

        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id để phân tích lên tone cuối bài.")

        if not base_key or not base_scale:
            # Ưu tiên cache nếu có.
            cached = self.find_cached_tone_by_video_id(video_id)
            if cached:
                base_key = str(cached.get("key", "") or "").strip()
                base_scale = str(cached.get("scale", "") or "").strip()

        if not base_key or not base_scale:
            raise ValueError("Thiếu tone gốc để phân tích lên tone cuối bài.")

        base_key = self.normalize_key_for_scale(base_key, base_scale)

        result = self.end_modulation_service.analyze_end_modulation(
            youtube_url=youtube_url,
            base_key=base_key,
            base_scale=base_scale,
            progress_cb=progress_cb,
        )

        result["video_id"] = video_id
        result["url"] = youtube_url
        result["title"] = title or result.get("title", "")
        result["base_key"] = self.normalize_key_for_scale(result.get("base_key", base_key), result.get("base_scale", base_scale))
        result["base_scale"] = str(result.get("base_scale", base_scale) or base_scale)
        result["base_label"] = f"{result['base_key']} {result['base_scale']}"

        if result.get("has_modulation") or result.get("candidate_found") or result.get("new_key"):
            new_scale = str(result.get("new_scale", result["base_scale"]) or result["base_scale"])
            new_key = self.normalize_key_for_scale(result.get("new_key", ""), new_scale)
            if new_key:
                result["new_key"] = new_key
                result["new_scale"] = new_scale
                result["new_key_index"] = self.key_name_to_index(new_key)
                result["new_label"] = f"{new_key} {new_scale}"

        return result

    def save_end_modulation_for_youtube(
        self,
        youtube_url: str,
        title: str,
        modulation_result: dict,
        base_result: dict | None = None,
    ) -> dict:
        """
        Lưu thông tin lên tone cuối bài sau khi người dùng xác nhận popup.
        """
        youtube_url = str(youtube_url or "").strip()
        video_id = self.extract_video_id(youtube_url)
        if not video_id:
            raise ValueError("Không tách được video_id để lưu lên tone cuối bài.")

        base_result = base_result or {}
        if not base_result:
            cached = self.find_cached_tone_by_video_id(video_id)
            if cached:
                base_result = {
                    "key": cached.get("key", ""),
                    "key_index": cached.get("key_index", 0),
                    "scale": cached.get("scale", ""),
                    "label": cached.get("label", cached.get("tone_label", "")),
                    "confidence": cached.get("confidence", 0.0),
                    "manual_verified": cached.get("manual_verified", False),
                }

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


    def get_external_youtube_playback_state(self, video_id: str = "") -> dict:
        """
        Đọc currentTime thật của YouTube đang phát trong Brave qua Chrome DevTools Protocol.
        Nếu máy chưa có websocket-client hoặc Brave không cho evaluate, trả về ok=False.
        App sẽ dùng fallback an toàn ở UI.
        """
        video_id = str(video_id or "").strip()
        try:
            tabs = self.browser_monitor_service.get_tabs()
        except Exception as e:
            return {"ok": False, "error": f"Không đọc được tab Brave: {e}"}

        # Strategy 2-tier search:
        #   1. Tìm tab match video_id chính xác (best case)
        #   2. Fallback: bất kỳ tab YouTube nào có vid (user đã chuyển bài
        #      qua radio auto-play → app cần biết video mới để invalidate cache)
        target_tab = None
        target_vid = ""
        for tab in tabs:
            try:
                url = str(tab.get("url", "") or "")
                tab_type = str(tab.get("type", "") or "")
                if tab_type and tab_type != "page":
                    continue
                vid = self.browser_monitor_service.extract_video_id(url)
                if video_id and vid == video_id:
                    target_tab = tab
                    target_vid = vid
                    break
            except Exception:
                pass

        # Tier 2: nếu không match exact, lấy YouTube tab đầu tiên (active tab)
        if not target_tab:
            for tab in tabs:
                try:
                    url = str(tab.get("url", "") or "")
                    tab_type = str(tab.get("type", "") or "")
                    if tab_type and tab_type != "page":
                        continue
                    vid = self.browser_monitor_service.extract_video_id(url)
                    if vid:
                        target_tab = tab
                        target_vid = vid
                        break
                except Exception:
                    pass

        if not target_tab:
            return {"ok": False, "error": "Không tìm thấy tab YouTube phù hợp trong Brave."}

        ws_url = str(target_tab.get("webSocketDebuggerUrl", "") or "").strip()
        if not ws_url:
            return {"ok": False, "error": "Tab Brave không có webSocketDebuggerUrl."}

        try:
            import websocket  # type: ignore
        except Exception:
            return {
                "ok": False,
                "error": "Thiếu thư viện websocket-client để đọc thời gian YouTube thật.",
                "missing_dependency": "websocket-client",
            }

        expression = """
        (() => {
            const v = document.querySelector('video');
            if (!v) return {ok:false, error:'Không tìm thấy thẻ video YouTube'};
            return {
                ok: true,
                currentTime: Number(v.currentTime || 0),
                duration: Number(v.duration || 0),
                paused: Boolean(v.paused),
                ended: Boolean(v.ended),
                playbackRate: Number(v.playbackRate || 1),
                url: String(location.href || ''),
                title: String(document.title || '')
            };
        })()
        """

        ws = None
        try:
            # Brave/Chromium v107+ requires origin header that matches the
            # debug server's allowed origin. Pass empty Origin to bypass
            # the same-origin check (works when --remote-allow-origins=* is set).
            try:
                ws = websocket.create_connection(
                    ws_url,
                    timeout=1.5,
                    origin="http://localhost",  # any allowed origin works
                )
            except Exception:
                # Fallback: try without origin (older Chromium versions)
                ws = websocket.create_connection(ws_url, timeout=1.5)
            payload = {
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {
                    "expression": expression,
                    "returnByValue": True,
                    "awaitPromise": True,
                },
            }
            ws.send(json.dumps(payload))
            raw = ws.recv()
            data = json.loads(raw)
            value = (((data or {}).get("result") or {}).get("result") or {}).get("value")
            if isinstance(value, dict):
                value["ok"] = bool(value.get("ok", False))
                # Bổ sung actual_video_id để UI biết Brave đang phát bài nào
                # → nếu khác requested video_id, UI có thể clear active_end_modulation
                value["actual_video_id"] = target_vid
                value["requested_video_id"] = video_id
                return value
            return {"ok": False, "error": "Không đọc được currentTime từ YouTube."}
        except Exception as e:
            return {"ok": False, "error": f"Lỗi đọc currentTime YouTube: {e}"}
        finally:
            try:
                if ws is not None:
                    ws.close()
            except Exception:
                pass

    # =========================================================
    # DATA TRANSFER / IMPORT EXPORT
    # =========================================================

    def build_default_export_filename(self) -> str:
        return self.data_transfer_service.build_default_export_filename()

    def export_tone_data(self, output_path: str) -> dict:
        return self.data_transfer_service.export_data(output_path)

    def import_tone_data(self, input_path: str) -> dict:
        return self.data_transfer_service.import_data(input_path)

    def clear_tone_cache_for_testing(self, make_backup: bool = True) -> dict:
        """
        Xóa cache tone để test lại sạch.
        Không xóa D.S BÀI HÁT / songs.json.
        """
        return self.tone_cache_service.clear_all_cache_for_testing(make_backup=make_backup)

    # =========================================================
    # MAINTENANCE / UPDATE
    # =========================================================

    def update_ytdlp(self) -> dict:
        """
        Cập nhật yt-dlp.
        - Frozen .exe: tải wheel từ PyPI, giải nén yt_dlp/ vào _sideload/ cạnh .exe.
        - Dev/source: dùng pip install -U như cũ.
        """
        if getattr(sys, "frozen", False):
            return self._update_ytdlp_sideload()

        cmd = [sys.executable, "-m", "pip", "install", "-U", "yt-dlp"]
        process = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        stdout = process.stdout or ""
        stderr = process.stderr or ""
        if process.returncode != 0:
            raise RuntimeError(
                "Cập nhật yt-dlp thất bại.\n\n"
                f"STDOUT:\n{stdout[-1600:]}\n\n"
                f"STDERR:\n{stderr[-1600:]}"
            )
        return {"ok": True, "command": " ".join(cmd), "stdout": stdout, "stderr": stderr}

    def _update_ytdlp_sideload(self) -> dict:
        """Tải wheel yt-dlp từ PyPI, giải nén yt_dlp/ vào _sideload/ cạnh .exe.
        Download vào %TEMP% trước để tránh AV lock file ngay trong thư mục app."""
        import urllib.request
        import json as _json
        import zipfile
        import shutil
        import tempfile

        # 1. Lấy version mới nhất + URL wheel từ PyPI
        req = urllib.request.Request(
            "https://pypi.org/pypi/yt-dlp/json",
            headers={"User-Agent": "THM-VocalPanel/1.0"},
        )
        with urllib.request.urlopen(req, timeout=30) as r:
            data = _json.loads(r.read().decode())

        version = data["info"]["version"]
        wheel_url = None
        for file_info in data["urls"]:
            if file_info["filename"].endswith("-py3-none-any.whl"):
                wheel_url = file_info["url"]
                break

        if not wheel_url:
            raise RuntimeError(f"Khong tim thay wheel yt-dlp {version} tren PyPI")

        sideload_dir = Path(sys.executable).parent / "_sideload"

        # Kiểm tra quyền ghi sớm, báo lỗi rõ ràng nếu bị chặn
        try:
            sideload_dir.mkdir(parents=True, exist_ok=True)
            _test = sideload_dir / "_write_test"
            _test.write_text("ok")
            _test.unlink()
        except PermissionError:
            raise RuntimeError(
                f"Khong co quyen ghi vao:\n{sideload_dir}\n\n"
                "Hay thu chay app voi quyen Administrator."
            )

        # 2. Download + giải nén trong %TEMP% (AV không lock thư mục app)
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_whl = Path(tmp_dir) / "yt_dlp.whl"
            urllib.request.urlretrieve(wheel_url, tmp_whl)

            with zipfile.ZipFile(tmp_whl, "r") as zf:
                for member in zf.namelist():
                    if member.startswith("yt_dlp/"):
                        zf.extract(member, tmp_dir)

            # 3. Thay thế nguyên tử vào sideload_dir
            src    = Path(tmp_dir) / "yt_dlp"
            target = sideload_dir / "yt_dlp"
            old    = sideload_dir / "_yt_dlp_old"

            if old.exists():
                shutil.rmtree(old, ignore_errors=True)
            if target.exists():
                shutil.move(str(target), str(old))
            shutil.move(str(src), str(target))
            if old.exists():
                shutil.rmtree(old, ignore_errors=True)

        return {"ok": True, "version": version}

    def get_ytdlp_version_info(self) -> dict:
        """So sánh phiên bản yt-dlp hiện tại với bản mới nhất trên PyPI.
        Dùng trong background thread — không block UI."""
        import urllib.request
        import json as _json

        result = {"current": "", "latest": "", "is_outdated": False, "error": None}

        try:
            import yt_dlp
            result["current"] = yt_dlp.version.__version__
        except Exception as e:
            result["error"] = f"Không đọc version hiện tại: {e}"
            return result

        try:
            req = urllib.request.Request(
                "https://pypi.org/pypi/yt-dlp/json",
                headers={"User-Agent": "THM-VocalPanel/1.0"},
            )
            with urllib.request.urlopen(req, timeout=8) as r:
                data = _json.loads(r.read().decode())
            result["latest"] = data["info"]["version"]

            def _ver_tuple(v: str):
                try:
                    return tuple(int(x) for x in v.split("."))
                except Exception:
                    return (0,)

            result["is_outdated"] = _ver_tuple(result["current"]) < _ver_tuple(result["latest"])
        except Exception as e:
            result["error"] = f"Không kiểm tra được version mới: {e}"

        return result

    # =========================================================
    # TIME
    # =========================================================

    def parse_time_to_seconds(self, mmss: str) -> int:
        mmss = (mmss or "").strip()
        parts = mmss.split(":")
        if len(parts) != 2:
            raise ValueError("Thời gian phải có dạng mm:ss")
        minutes = int(parts[0])
        seconds = int(parts[1])
        if minutes < 0 or seconds < 0 or seconds >= 60:
            raise ValueError("Thời gian mm:ss không hợp lệ")
        return minutes * 60 + seconds

    # =========================================================
    # REALTIME DETECT
    # =========================================================

    def list_realtime_candidates(self):
        return self.realtime_service.list_realtime_candidates()

    def start_realtime(self, device_index: int, device_mode: str, on_stable_tone, on_status=None):
        self.realtime_service.start(
            device_index=device_index,
            device_mode=device_mode,
            on_stable_tone=on_stable_tone,
            on_status=on_status,
        )

    def stop_realtime(self):
        self.realtime_service.stop()
