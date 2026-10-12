
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
from core.services.control_surface_service import ControlSurfaceService
from core.services.ytdlp_maintenance_service import YtDlpMaintenanceService
from core.services.youtube_playback_service import YouTubePlaybackService
from core.services.key_signature_service import KeySignatureService
from core.services.tone_application_service import ToneApplicationService
from core.services.tone_workflow_service import ToneWorkflowService
from core.services.end_modulation_workflow_service import EndModulationWorkflowService
from core.audio_capture.capture_coordinator import CaptureCoordinator
from core.audio_capture.platform.sounddevice_input import SoundDeviceInputBackend


class AppController:
    def __init__(self):
        self.midi = MidiService()
        self.tone_service = ToneService()
        self.song_service = SongService()
        self.mode_settings_service = ModeSettingsService()
        self.control_surface = ControlSurfaceService(self.midi, self.mode_settings_service)
        self.ytdlp_maintenance_service = YtDlpMaintenanceService()
        self.key_signature_service = KeySignatureService()
        self.tone_application_service = ToneApplicationService(
            self.tone_service, self.midi, self.key_signature_service,
        )
        self.realtime_service = RealtimeToneService()
        self.audio_capture_coordinator = None

        self.settings_service = SettingsService()
        self.settings = self.settings_service.load()

        self.autokey_service = AutoKeyService()
        self.tone_cache_service = ToneCacheService()
        self.tone_workflow_service = ToneWorkflowService(
            self.tone_service,
            self.tone_cache_service,
            self.song_service,
            self.key_signature_service,
            self.tone_application_service,
        )
        self.browser_launcher_service = BrowserLauncherService()
        self.browser_monitor_service = BrowserMonitorService()
        self.youtube_playback_service = YouTubePlaybackService(self.browser_monitor_service)
        self.data_transfer_service = DataTransferService()
        self.end_modulation_service = EndModulationService()
        self.end_modulation_workflow_service = EndModulationWorkflowService(
            self.end_modulation_service,
            self.tone_cache_service,
            self.tone_workflow_service,
            self.key_signature_service,
        )


    @property
    def scale_options(self):
        """Compatibility facade for callers that read supported scales."""
        return self.key_signature_service.scale_options

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
        self.stop_audio_visualizer()
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
        return self.key_signature_service.key_options_for_scale(scale_name)

    def normalize_key_for_scale(self, key_name: str, scale_name: str) -> str:
        return self.key_signature_service.normalize(key_name, scale_name)

    def key_name_to_index(self, key_name: str) -> int:
        return self.key_signature_service.key_index(key_name)

    # =========================================================
    # MIC SELECT
    # =========================================================

    def set_active_mic(self, mic_name: str):
        self.control_surface.set_active_mic(mic_name)

    def get_active_mic_name(self):
        return self.control_surface.current_mic_name

    def is_lofi_on(self):
        return self.control_surface.lofi_on

    # =========================================================
    # MONITOR
    # =========================================================

    def toggle_vang(self, is_on: bool):
        self.control_surface.toggle_vang(is_on)

    def toggle_mic(self, is_on: bool):
        self.control_surface.toggle_mic(is_on)

    # =========================================================
    # LOFI
    # =========================================================

    def toggle_lofi(self):
        return self.control_surface.toggle_lofi()

    def set_lofi(self, is_on: bool):
        self.control_surface.set_lofi(is_on)

    # =========================================================
    # EFFECT / VOLUME
    # =========================================================

    def set_reverb_short(self, value: int):
        self.control_surface.set_reverb_short(value)

    def set_reverb_long(self, value: int):
        self.control_surface.set_reverb_long(value)

    def set_echo(self, value: int):
        self.control_surface.set_echo(value)

    def set_mic_volume(self, value: int):
        self.control_surface.set_mic_volume(value)

    def set_music_volume(self, value: int):
        self.control_surface.set_music_volume(value)

    def set_tune_from_slider_position(self, raw_position_0_100: int):
        self.control_surface.set_tune_from_slider_position(raw_position_0_100)

    def set_tune(self, tune_value: int):
        self.control_surface.set_tune(tune_value)

    # =========================================================
    # GLOBAL PITCH
    # =========================================================

    def set_global_pitch(self, semitone: int):
        return self.control_surface.set_global_pitch(semitone)

    # =========================================================
    # MODE PRESET
    # =========================================================

    def apply_mode(self, mode_name: str):
        return self.control_surface.apply_mode(mode_name)

    def remember_mode_slider_value(self, field: str, value):
        self.control_surface.remember_mode_slider_value(field, value)

    def get_default_ui_values(self):
        return self.control_surface.default_ui_values()

    # =========================================================
    # TONE DETECT FROM FILE
    # =========================================================

    def detect_tone_from_file(self, file_path: str) -> dict:
        return self.tone_application_service.detect_from_file(file_path)

    # =========================================================
    # TONE APPLY
    # =========================================================

    def apply_detected_tone(self, tone_result: dict) -> dict:
        return self.tone_application_service.apply_detected(tone_result)

    def apply_manual_tone(self, key_name: str, scale_name: str):
        return self.tone_application_service.apply_manual(key_name, scale_name)

    def apply_realtime_tone(self, key_name: str, scale_name: str):
        return self.tone_application_service.apply_realtime(key_name, scale_name)

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
        return self.tone_workflow_service.extract_video_id(youtube_url)

    def find_cached_tone_by_video_id(self, video_id: str):
        return self.tone_workflow_service.find_cached_tone_by_video_id(video_id)

    def find_cached_tone_by_url(self, youtube_url: str):
        return self.tone_workflow_service.find_cached_tone_by_url(youtube_url)

    def save_tone_cache_from_result(self, youtube_url: str, title: str, result: dict):
        return self.tone_workflow_service.save_tone_cache_from_result(youtube_url, title, result)

    def apply_cached_tone(self, cached: dict):
        return self.tone_workflow_service.apply_cached_tone(cached)

    # =========================================================
    # EXTERNAL BROWSER AUTO DETECT
    # =========================================================

    def get_current_external_youtube_video(self):
        return self.browser_monitor_service.get_current_youtube_video()

    def detect_tone_from_youtube_url(self, youtube_url: str) -> dict:
        return self.tone_workflow_service.detect_tone_from_youtube_url(youtube_url)

    def auto_resolve_tone_for_youtube(self, youtube_url: str, title: str = "") -> dict:
        return self.tone_workflow_service.auto_resolve_for_youtube(youtube_url, title)



    def fix_tone_for_youtube_url(self, youtube_url: str, title: str = "", current_key: str = "", current_scale: str = "") -> dict:
        return self.tone_workflow_service.fix_for_youtube(youtube_url, title, current_key, current_scale)

    def confirm_tone_cache_by_url(self, youtube_url: str, title: str, key_name: str, scale_name: str) -> dict:
        return self.tone_workflow_service.confirm_cache(youtube_url, title, key_name, scale_name)



    # =========================================================
    # END MODULATION / LÊN TONE CUỐI BÀI
    # =========================================================

    def find_saved_end_modulation_by_url(self, youtube_url: str):
        return self.end_modulation_workflow_service.find_saved_by_url(youtube_url)

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
        self.end_modulation_workflow_service.invalidate_audio_cache()

    def analyze_end_modulation_for_youtube(
        self,
        youtube_url: str,
        title: str = "",
        base_key: str = "",
        base_scale: str = "",
        progress_cb=None,
    ) -> dict:
        return self.end_modulation_workflow_service.analyze(
            youtube_url, title, base_key, base_scale, progress_cb,
        )

    def save_end_modulation_for_youtube(
        self,
        youtube_url: str,
        title: str,
        modulation_result: dict,
        base_result: dict | None = None,
    ) -> dict:
        return self.end_modulation_workflow_service.save(
            youtube_url, title, modulation_result, base_result,
        )


    def get_external_youtube_playback_state(self, video_id: str = "") -> dict:
        return self.youtube_playback_service.get_state(video_id)

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
        return self.ytdlp_maintenance_service.update()

    def get_ytdlp_version_info(self) -> dict:
        return self.ytdlp_maintenance_service.version_info()

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

    def start_realtime(self, device_index: int, device_mode: str, on_stable_tone, on_status=None, on_audio_level=None):
        self.realtime_service.start(
            device_index=device_index,
            device_mode=device_mode,
            on_stable_tone=on_stable_tone,
            on_status=on_status,
            on_audio_level=on_audio_level,
        )

    def stop_realtime(self):
        self.realtime_service.stop()

    # =========================================================
    # LIVE VISUAL AUDIO
    # =========================================================

    def start_audio_visualizer(self, on_snapshot, on_state=None):
        """Start the lightweight visual capture path after user consent.

        It deliberately does not reuse realtime tone detection: visual levels
        must keep running even when tone detection is paused for a saved song.
        """
        self.stop_audio_visualizer()
        system_backend = None
        try:
            import sys
            if sys.platform.startswith("win"):
                from core.audio_capture.platform.windows_wasapi import WindowsWasapiLoopbackBackend
                system_backend = WindowsWasapiLoopbackBackend()
            elif sys.platform == "darwin":
                from core.audio_capture.platform.macos_screencapturekit import MacOSScreenCaptureKitBackend
                system_backend = MacOSScreenCaptureKitBackend()
        except Exception:
            system_backend = None
        self.audio_capture_coordinator = CaptureCoordinator(
            microphone_backend=SoundDeviceInputBackend(),
            system_backend=system_backend,
            on_snapshot=on_snapshot,
            on_state=on_state,
        )
        self.audio_capture_coordinator.start()

    def stop_audio_visualizer(self):
        if self.audio_capture_coordinator is not None:
            self.audio_capture_coordinator.stop()
            self.audio_capture_coordinator = None
