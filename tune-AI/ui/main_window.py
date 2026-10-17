import json
import os
import tempfile
import threading
import time

from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QEvent, QSize, QRect
from PyQt6.QtGui import QGuiApplication, QIcon
from PyQt6.QtWidgets import (
    QComboBox,
    QDialog,
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from core.constants import APP_TITLE, ASSETS_DIR, BASE_DIR
from core.services.performance_surface_service import PerformanceSurfaceService
from ui.song_manager_dialog import SongEditDialog, SongManagerDialog
from ui.youtube_panel import YouTubeBrowserDialog
from ui.settings_dialog import SettingsDialog
from ui.analog_dialog_theme import ANALOG_DIALOG_QSS, apply_analog_message_theme
from ui.audio_permissions_dialog import AudioPermissionsDialog
from ui.performance_macro_dialog import PerformanceMacroDialog
from ui.premium_widgets import RotaryKnob, ToneShiftDisplay
from ui.analog_components import AnalogKnobPanel, AutoKeyProgressStrip, AnalogChassis, AnalogPressButton, KnobValueReadout, MacroRoutingCaption, ModeCardButton, ReactiveAudioWaveform, ReferenceOverlay, ToneDetectionReadout
from ui.analog_theme import REFERENCE_LAYOUT
from ui.tone_value_utils import (
    coerce_tune_value,
    key_for_pitch_class,
    parse_mmss,
    pitch_class_for_key,
    seconds_to_mmss,
)


class MainWindow(QMainWindow):
    MODE_BTN_W = 142
    MODE_BTN_H = 84
    DEFAULT_WINDOW_SIZE = QSize(1080, 684)
    MINIMUM_WINDOW_SIZE = QSize(900, 570)

    @staticmethod
    def _publish_studioforge_key(key: str, scale: str, confidence: float, title: str = "") -> None:
        """Publish the confirmed Auto-Key result to the paired StudioForge host.

        This is a deliberately small, atomic file bridge: it works for the
        separately packaged Tune-AI app without coupling the UI to a DAW
        process or requiring a network permission. StudioForge consumes and
        validates the record before touching any plug-in parameters.
        """
        try:
            target = os.path.join(tempfile.gettempdir(), "studioforge-autokey-result.json")
            payload = {
                "schema": 1,
                "key": str(key or "").strip(),
                "scale": str(scale or "").strip(),
                "confidence": float(confidence or 0.0),
                "title": str(title or "").strip(),
                "published_at": time.time(),
            }
            temporary = target + ".tmp"
            with open(temporary, "w", encoding="utf-8") as stream:
                json.dump(payload, stream, ensure_ascii=False)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, target)
        except (OSError, ValueError):
            # The DAW bridge is optional; never interrupt Auto-Key UI/audio
            # handling when the host is unavailable or the temp folder is not
            # writable.
            pass

    realtime_status_signal = pyqtSignal(str)
    realtime_tone_signal = pyqtSignal(object)
    realtime_level_signal = pyqtSignal(float)
    audio_snapshot_signal = pyqtSignal(object)
    audio_capture_state_signal = pyqtSignal(object, str)

    external_browser_status_signal = pyqtSignal(str)
    external_browser_tone_signal = pyqtSignal(object)

    autokey_finished_signal = pyqtSignal(object)
    fix_tone_result_signal = pyqtSignal(object)
    end_modulation_result_signal = pyqtSignal(object)

    ytdlp_version_signal = pyqtSignal(object)

    PITCH_CLASS_TO_MAJOR = {
        0: "C",
        1: "Db",
        2: "D",
        3: "Eb",
        4: "E",
        5: "F",
        6: "F#",
        7: "G",
        8: "Ab",
        9: "A",
        10: "Bb",
        11: "B",
    }

    PITCH_CLASS_TO_MINOR = {
        0: "C",
        1: "C#",
        2: "D",
        3: "Eb",
        4: "E",
        5: "F",
        6: "F#",
        7: "G",
        8: "G#",
        9: "A",
        10: "Bb",
        11: "B",
    }

    KEY_TO_PITCH_CLASS = {
        "C": 0,
        "C#": 1,
        "Db": 1,
        "D": 2,
        "D#": 3,
        "Eb": 3,
        "E": 4,
        "F": 5,
        "F#": 6,
        "Gb": 6,
        "G": 7,
        "G#": 8,
        "Ab": 8,
        "A": 9,
        "A#": 10,
        "Bb": 10,
        "B": 11,
    }

    def __init__(self, controller):
        super().__init__()
        self.controller = controller

        self.setWindowTitle(APP_TITLE)
        self.resize(self.DEFAULT_WINDOW_SIZE)
        self.setMinimumSize(self.MINIMUM_WINDOW_SIZE)
        # The hardware chassis owns the full visual surface. Exit is an
        # in-app control, and only empty header material can drag the window.
        self.setWindowFlags(Qt.WindowType.Window | Qt.WindowType.FramelessWindowHint)
        self._window_drag_offset = None
        self._studioforge_host_frame = None
        self._shutdown_started = False

        self.vang_on = True
        self.mic_on = True
        self.lofi_on = False
        self.tone_step_value = 0
        self.current_mode_button = None

        self.current_detected_tone = {
            "key": "F",
            "key_index": 5,
            "scale": "Minor",
            "label": "F Minor",
        }
        self.base_tone_for_transpose = {
            "key": "F",
            "scale": "Minor",
        }
        # F Minor is a safe DSP bootstrap only, never presented as a detected result.
        self.tone_is_resolved = False

        self.current_song_playing = None
        self.current_song_id = None
        self.song_raise_seconds = 0
        self.song_raise_done = False
        self.song_video_time_sec = 0.0

        self.realtime_running = False
        self.realtime_candidates = []
        self.realtime_locked_by_song = False

        self.youtube_browser = None

        self.external_auto_enabled = True
        self.external_last_video_id = ""
        self.external_detecting_video_id = ""
        self.external_detect_running = False
        self.external_current_video = {
            "video_id": "",
            "url": "",
            "title": "",
        }

        self.progress_timer = None
        self.progress_value = 0
        self.progress_target = 0
        self.progress_finish_pending = False
        self._pending_control_values = {}
        self._control_dispatch_timers = {}
        # The DAW remains the sole owner of macro assignments. This client
        # stores only the latest read-only snapshot for presentation.
        self.performance_surface = PerformanceSurfaceService()
        self._performance_surface_state = {}

        # Chế độ thu gọn Mini Bar.
        # Khi bấm nút minimize của cửa sổ chính, app sẽ chuyển sang thanh điều khiển nhỏ.
        self.compact_mode = False
        self.compact_window = None
        self.normal_geometry_before_compact = None

        self.autokey_running = False
        self.fix_tone_running = False

        # Phát hiện lên tone cuối bài.
        # Bản đầu: chỉ phân tích + popup xác nhận lưu, chưa tự đổi key/scale ngay lần đầu.
        self.end_modulation_running = False
        self.end_modulation_user_intervened = False
        self.end_modulation_checked_video_id = ""
        self.end_modulation_pending_context = {}
        self.end_modulation_last_result = None

        # Auto apply lên tone cuối bài đã lưu.
        # Không lưu mức +/- nhạc; luôn quy đổi realtime theo self.tone_step_value hiện tại.
        self.active_end_modulation = None
        self.end_modulation_applied_stage = "base"  # base | end
        self.external_video_seen_at = 0.0

        self.realtime_status_signal.connect(self._handle_realtime_status_on_ui)
        self.realtime_tone_signal.connect(self._handle_realtime_tone_on_ui)
        self.realtime_level_signal.connect(self._handle_realtime_level_on_ui)
        self.audio_snapshot_signal.connect(self._handle_audio_snapshot_on_ui)
        self.audio_capture_state_signal.connect(self._handle_audio_capture_state_on_ui)

        self.external_browser_status_signal.connect(self._handle_external_browser_status)
        self.external_browser_tone_signal.connect(self._handle_external_browser_tone_result)
        self.autokey_finished_signal.connect(self._handle_autokey_finished)
        self.fix_tone_result_signal.connect(self._handle_fix_tone_result)
        self.end_modulation_result_signal.connect(self._handle_end_modulation_result)
        self.ytdlp_version_signal.connect(self._on_ytdlp_version_result)

        logo_path = ASSETS_DIR / "logo.png"
        if logo_path.exists():
            self.setWindowIcon(QIcon(str(logo_path)))

        self._build_ui()
        self._apply_theme()
        self._startup()
        self._load_default_singing_mode_on_startup()
        self._update_transposed_tone_label()
        self._start_external_browser_auto_detect()
        QTimer.singleShot(350, self._start_live_audio_after_consent)

        self.performance_surface_timer = QTimer(self)
        self.performance_surface_timer.setInterval(250)  # UI control-rate, never audio-rate.
        self.performance_surface_timer.timeout.connect(self._poll_performance_surface)
        self.performance_surface_timer.start()

        self.end_modulation_timer = QTimer(self)
        self.end_modulation_timer.setSingleShot(True)
        self.end_modulation_timer.timeout.connect(self._run_end_modulation_check_if_ready)

        self.end_modulation_apply_timer = QTimer(self)
        self.end_modulation_apply_timer.timeout.connect(self._check_saved_end_modulation_auto_apply)
        # Realtime fix: 700ms → 200ms để giảm lag apply tại mốc raise_time.
        # Mỗi tick chỉ đọc cached value + so sánh, CPU cost negligible.
        # Started lazily when an end-modulation context is available.

        # Debounce cập nhật tone hiển thị sau khi anh bấm tăng/giảm tone nhạc.
        self.pitch_transpose_timer = QTimer(self)
        self.pitch_transpose_timer.setSingleShot(True)
        self.pitch_transpose_timer.timeout.connect(self._on_pitch_transpose_debounce)

        self.setStatusBar(None)

        self.show()
        QTimer.singleShot(0, self._fit_and_center_window)

    # =========================================================
    # HELPERS
    # =========================================================

    def _poll_performance_surface(self):
        """Project the latest DAW macro snapshot onto the analog controls."""
        snapshot = self.performance_surface.poll_state()
        if snapshot is None:
            return
        self._performance_surface_state = snapshot
        macros = snapshot.get("macros", [])
        if not isinstance(macros, list) or not hasattr(self, "effect_knob_panel"):
            return

        cards = []
        slider_names = ("slider_reverb_short", "slider_reverb_long", "slider_echo")
        for index in range(3):
            macro = macros[index] if index < len(macros) and isinstance(macros[index], dict) else {}
            state = int(macro.get("state", 0) or 0)
            name = str(macro.get("macroName", "") or "Unassigned")
            plugin_name = str(macro.get("pluginName", "") or "")
            bus_name = str(macro.get("busName", "") or "")
            state_key = "bypassed" if state == 3 else ("review" if state == 1 else
                        ("active" if state == 2 else "unassigned"))
            status = "BYPASSED" if state_key == "bypassed" else (
                "REVIEW" if state_key == "review" else name.upper()
            )
            cards.append((f"MACRO {index + 1} · {status}", 0, 100))
            if hasattr(self, "macro_routing_captions") and index < len(self.macro_routing_captions):
                if state_key == "active":
                    summary = plugin_name or bus_name or name
                elif state_key == "review":
                    summary = "REVIEW MAP"
                elif state_key == "bypassed":
                    summary = "PLUGIN BYPASSED"
                else:
                    summary = "UNASSIGNED"
                self.macro_routing_captions[index].set_mapping(summary, state_key)
            slider = getattr(self, slider_names[index], None)
            if slider is None:
                continue
            slider.blockSignals(True)
            slider.setValue(round(max(0.0, min(1.0, float(macro.get("value", 0.0) or 0.0))) * 100))
            slider.blockSignals(False)
            self._set_knob_readout_value(slider_names[index], slider.value())
        self.effect_knob_panel.set_controls(tuple(cards))

    def _set_performance_macro_value(self, macro_index: int, value: int) -> bool:
        """Send a normalised performer gesture only when DAW owns a mapping."""
        if not self.performance_surface.connected:
            return False
        result = self.performance_surface.set_value(macro_index, value / 100.0)
        if not result or not result.get("ok", False):
            return False
        return True

    def _fit_and_center_window(self):
        """Use a consistent 16:9 default while staying inside the active screen."""
        if self._studioforge_host_frame is not None:
            available = QRect(*self._studioforge_host_frame).adjusted(28, 28, -28, -28)
        else:
            screen = self.screen() or QGuiApplication.primaryScreen()
            if screen is None:
                return
            available = screen.availableGeometry()
        width = min(self.DEFAULT_WINDOW_SIZE.width(), max(self.MINIMUM_WINDOW_SIZE.width(), available.width() - 32))
        height = min(self.DEFAULT_WINDOW_SIZE.height(), max(self.MINIMUM_WINDOW_SIZE.height(), available.height() - 32))
        # Preserve the asset-pack reference aspect ratio.
        reference_ratio = self.DEFAULT_WINDOW_SIZE.width() / self.DEFAULT_WINDOW_SIZE.height()
        if width / max(1, height) > reference_ratio:
            width = min(width, round(height * reference_ratio))
        else:
            height = min(height, round(width / reference_ratio))
        self.resize(width, height)
        self.move(
            available.x() + (available.width() - width) // 2,
            available.y() + (available.height() - height) // 2,
        )

    def present_over_studioforge(self, host_frame: tuple[int, int, int, int]):
        """Show the real THM app as a borderless utility surface over the DAW."""
        self._studioforge_host_frame = host_frame
        self.setWindowFlags(Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint)
        self._fit_and_center_window()

    def eventFilter(self, watched, event):
        """Let only the non-interactive chassis header drag the frameless window."""
        is_drag_surface = watched is getattr(self, "_analog_chassis", None)
        if is_drag_surface:
            if event.type() == QEvent.Type.MouseButtonPress and event.button() == Qt.MouseButton.LeftButton:
                if self._is_window_drag_zone(event.position().toPoint()):
                    self._window_drag_offset = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
                    return True
            if event.type() == QEvent.Type.MouseMove and self._window_drag_offset is not None:
                if event.buttons() & Qt.MouseButton.LeftButton:
                    self.move(event.globalPosition().toPoint() - self._window_drag_offset)
                    return True
            if event.type() == QEvent.Type.MouseButtonRelease:
                self._window_drag_offset = None
                return True
        return super().eventFilter(watched, event)

    def _is_window_drag_zone(self, position) -> bool:
        """The top chassis nameplate is decorative; controls begin below it."""
        chassis = getattr(self, "_analog_chassis", None)
        if not isinstance(chassis, AnalogChassis):
            return False
        reference = chassis.reference_rect()
        if reference.isNull():
            return False
        header_bottom = reference.top() + reference.height() * 0.145
        return reference.left() <= position.x() <= reference.right() and position.y() <= header_bottom

    def _seconds_to_mmss(self, total_seconds: int) -> str:
        return seconds_to_mmss(total_seconds)

    def _parse_raise_time_mmss(self, text: str) -> int:
        return parse_mmss(text)

    def _key_to_pitch_class(self, key: str) -> int:
        return pitch_class_for_key(key, self.KEY_TO_PITCH_CLASS)

    def _pitch_class_to_key(self, pitch_class: int, scale: str) -> str:
        return key_for_pitch_class(pitch_class, scale,
                                   self.PITCH_CLASS_TO_MAJOR,
                                   self.PITCH_CLASS_TO_MINOR)

    def _transpose_key(self, key: str, scale: str, semitones: int) -> str:
        pc = self._key_to_pitch_class(key)
        new_pc = (pc + semitones) % 12
        return self._pitch_class_to_key(new_pc, scale)

    def _get_display_base_tone_for_current_stage(self):
        """
        Trả về tone gốc dùng để HIỂN THỊ hiện tại.

        Nguyên tắc chống double-transpose:
        - self.base_tone_for_transpose luôn là tone gốc thật của bài, không được ghi đè bằng tone đã +/- nhạc.
        - Nếu chưa tới đoạn lên tone: hiển thị base_key + tone_step.
        - Nếu đã tới đoạn lên tone cuối bài: hiển thị end_key + tone_step.
        """
        try:
            if getattr(self, "end_modulation_applied_stage", "base") == "end":
                end_mod = getattr(self, "active_end_modulation", None)
                if isinstance(end_mod, dict):
                    end_key = str(end_mod.get("end_key", "") or end_mod.get("new_key", "") or "").strip()
                    end_scale = str(end_mod.get("end_scale", "") or end_mod.get("new_scale", "") or end_mod.get("base_scale", "Major") or "Major").strip()
                    if end_key and end_scale:
                        return {"key": end_key, "scale": end_scale}
        except Exception:
            pass

        return {
            "key": self.base_tone_for_transpose.get("key", "C"),
            "scale": self.base_tone_for_transpose.get("scale", "Major"),
        }

    def _update_transposed_tone_label(self):
        display_base = self._get_display_base_tone_for_current_stage()
        base_key = display_base.get("key", "C")
        base_scale = display_base.get("scale", "Major")

        transposed_key = self._transpose_key(base_key, base_scale, self.tone_step_value)

        self.current_detected_tone["key"] = transposed_key
        self.current_detected_tone["key_index"] = self.controller.key_name_to_index(transposed_key)
        self.current_detected_tone["scale"] = base_scale
        self.current_detected_tone["label"] = f"{transposed_key} {base_scale}"

        self.lbl_tone.setText(self._tone_display_text(f"{transposed_key} {base_scale}"))
        self._sync_compact_controls()

    def _tone_display_text(self, label: str | None = None) -> str:
        """Return one end-user-safe representation for every tone surface."""
        if not getattr(self, "tone_is_resolved", False):
            return "Chờ tín hiệu"
        return str(label or self.current_detected_tone.get("label", "") or "Chờ tín hiệu")

    def _set_pitch_step_value(self, semitone: int, send_midi: bool = True):
        """Đổi mức tăng/giảm tone nhạc trong trạng thái điều khiển nội bộ."""
        try:
            semitone = max(-12, min(12, int(semitone)))
        except Exception:
            semitone = 0

        self.tone_step_value = semitone

        try:
            if hasattr(self, "lbl_tone_step_value"):
                self.lbl_tone_step_value.setText(f"{self.tone_step_value:+d}")
        except Exception:
            pass

        if send_midi:
            try:
                self.controller.set_global_pitch(self.tone_step_value)
            except Exception as e:
                print("Không gửi được CC 36 khi đổi pitch:", e)

        try:
            self._sync_compact_controls()
        except Exception:
            pass

        try:
            self._update_end_modulation_status_line()
        except Exception:
            pass

    def _schedule_pitch_transpose_refresh(self):
        """Sau khi anh dừng bấm +/- khoảng 3 giây, cập nhật lại preview tone theo mức transpose."""
        try:
            if hasattr(self, "pitch_transpose_timer"):
                self.pitch_transpose_timer.start(3000)
        except Exception:
            pass

    def _on_pitch_transpose_debounce(self):
        """Cập nhật lại tone đang hiển thị sau khi tăng/hạ tone ổn định."""
        try:
            self._update_transposed_tone_label()
            self._update_end_modulation_status_line()
            self._reapply_current_end_modulation_stage_after_pitch_change()
        except Exception:
            pass

    def _ensure_progress_timer(self):
        if self.progress_timer is None:
            self.progress_timer = QTimer(self)
            self.progress_timer.timeout.connect(self._on_progress_timer_tick)

    def _on_progress_timer_tick(self):
        current = int(self.tone_progress.value())

        if self.progress_target >= 100:
            if current < 100:
                step = 2 if current < 90 else 1
                self.tone_progress.setValue(min(100, current + step))
                self._sync_compact_progress()
                return

            self.progress_timer.stop()
            self.progress_value = 100
            self._sync_compact_progress()
            self.progress_finish_pending = False
            return

        # Khi đang xử lý thật, progress chạy mềm từ 0 tới 92%,
        # không đứng 12% rồi nhảy thẳng 100%.
        if current < self.progress_target:
            self.tone_progress.setValue(current + 1)
            self._sync_compact_progress()
            return

        if self.progress_target < 92:
            self.progress_target += 1
            self.tone_progress.setValue(min(self.progress_target, 92))
            self._sync_compact_progress()
            return

        # Giữ ở 92% khi thuật toán còn đang xử lý, chờ kết quả xong mới chạy 100%.
        self.tone_progress.setValue(92)
        self._sync_compact_progress()

    def _set_progress_working(self, message: str = "Đang xử lý..."):
        self.tone_progress.setRange(0, 100)
        self.tone_progress.setFormat("%p%")
        self.progress_value = 0
        self.progress_target = 1
        self.progress_finish_pending = False
        self.tone_progress.setValue(0)
        self._sync_compact_progress()
        self._update_end_modulation_status_line()

        self._ensure_progress_timer()
        if not self.progress_timer.isActive():
            self.progress_timer.start(120)

    def _set_progress_success(self, message: str = "Hoàn tất"):
        self.tone_progress.setRange(0, 100)
        self.tone_progress.setFormat("%p%")
        self.progress_target = 100
        self.progress_finish_pending = True
        self._update_end_modulation_status_line()

        self._ensure_progress_timer()
        if not self.progress_timer.isActive():
            self.progress_timer.start(25)
        else:
            self.progress_timer.setInterval(25)

    def _reset_progress_idle(self):
        self.tone_progress.setRange(0, 100)
        self.tone_progress.setFormat("%p%")
        self.tone_progress.setValue(0)
        self._sync_compact_progress()
        self.progress_value = 0
        self.progress_target = 0
        self.progress_finish_pending = False
        if self.progress_timer is not None and self.progress_timer.isActive():
            self.progress_timer.stop()
        self._update_end_modulation_status_line()

    def _refresh_mode_button_colors(self):
        """Repaint cards after a state change without replacing their artwork."""
        for btn in self.mode_buttons:
            btn.update()

    def _update_song_status_line(self):
        if not self.current_song_playing:
            self._update_end_modulation_status_line()
            return

        raise_time = str(self.current_song_playing.get("raise_time", "") or "").strip()
        if not raise_time or raise_time == "00:00":
            self._update_end_modulation_status_line()
            return

        end_key_raw = self.current_song_playing.get("end_key", "--")
        end_scale = self.current_song_playing.get("end_scale", "--")

        end_desc = ""
        if end_key_raw not in ("", "--") and end_scale not in ("", "--"):
            end_key = self.controller.normalize_key_for_scale(end_key_raw, end_scale)
            end_desc = f" | Cuối bài: {end_key} {end_scale}"

        current_mmss = self._seconds_to_mmss(int(self.song_video_time_sec))
        self.lbl_tone_status.setText(f"Time: {current_mmss} / {raise_time}{end_desc}")

    # =========================================================
    # UI
    # =========================================================

    def _build_ui(self):
        root = AnalogChassis()
        self.setCentralWidget(root)
        self._analog_chassis = root
        root.installEventFilter(self)
        overlay = ReferenceOverlay(root)
        self._reference_overlay = overlay
        overlay.setGeometry(root.rect())
        overlay.set_reference_rect(root.reference_rect())
        self._build_live_reference_controls(overlay)
        # The alpha knob frames need an exact opaque panel snapshot beneath
        # them. Do this after the window enters the backing store.
        QTimer.singleShot(0, self._refresh_knob_backdrops)
        self.setStatusBar(None)

    def _build_live_reference_controls(self, overlay):
        """Compose the live controls in calibrated reference order."""
        self._build_tone_reference_controls(overlay)
        self._build_knob_reference_controls(overlay)
        self._build_mode_and_footer_controls(overlay)

    @staticmethod
    def _place_reference_bounds(overlay, widget, bounds):
        return overlay.place(widget, bounds.x, bounds.y, bounds.width, bounds.height)

    def _create_live_button(self, overlay, x, y, width, height, callback, text=""):
        button = AnalogPressButton(overlay)
        button.setText(text)
        button.clicked.connect(callback)
        return overlay.place(button, x, y, width, height)

    def _build_tone_reference_controls(self, overlay):
        """Build dynamic tone/meter controls; chassis artwork owns its shell."""

        # The centre panel is for detected key/chord.  +/- remains exclusively
        # in the separate Tone Shift panel, where transposition is meaningful.
        self.lbl_tone = ToneDetectionReadout("Chờ tín hiệu", overlay)
        self._place_reference_bounds(overlay, self.lbl_tone, REFERENCE_LAYOUT.tone_readout)
        self.header_waveform = ReactiveAudioWaveform(overlay)
        self._place_reference_bounds(overlay, self.header_waveform, REFERENCE_LAYOUT.header_waveform)
        self.autokey_progress_strip = AutoKeyProgressStrip(overlay)
        self._place_reference_bounds(overlay, self.autokey_progress_strip, REFERENCE_LAYOUT.signal_rail)
        self.lbl_tone_status = QLabel("", overlay)
        self.lbl_tone_status.hide()
        # Non-visual compatibility handles retained for the existing tone
        # workflow; the production reference owns their visible surface.
        self.btn_detect_tone = QPushButton(overlay)
        self.btn_detect_tone.hide()
        self.lbl_end_mod_info = QLabel("", overlay)
        self.lbl_end_mod_info.hide()
        self.tone_progress = QProgressBar(overlay)
        self.tone_progress.setRange(0, 100)
        self.tone_progress.setValue(0)
        self.tone_progress.hide()

        self.btn_auto_detect = AnalogPressButton(overlay)
        self.btn_auto_detect.setCheckable(True)
        self.btn_auto_detect.setChecked(True)
        self.btn_auto_detect.clicked.connect(self.on_toggle_auto_detect_clicked)
        self.btn_fix_tone = AnalogPressButton(overlay)
        self.btn_fix_tone.clicked.connect(self.on_fix_tone_clicked)
        self.btn_verify_tone = AnalogPressButton(overlay)
        self.btn_verify_tone.clicked.connect(self.on_confirm_tone_clicked)
        action_buttons = (self.btn_auto_detect, self.btn_fix_tone, self.btn_verify_tone)
        for button, x in zip(action_buttons, REFERENCE_LAYOUT.tone_actions):
            overlay.place(button, x, REFERENCE_LAYOUT.tone_action_y,
                          REFERENCE_LAYOUT.tone_action_size, REFERENCE_LAYOUT.tone_action_size)
        self._performance_action_buttons = action_buttons

        # The chassis already owns this module's outer frame and screws.  Do
        # not add another panel over it; only the live hit areas and display
        # are placed inside its original, calibrated bounds.
        minus = REFERENCE_LAYOUT.tone_shift_minus
        plus = REFERENCE_LAYOUT.tone_shift_plus
        self.btn_tone_minus = self._create_live_button(
            overlay, minus.x, minus.y, minus.width, minus.height, self.on_tone_minus_clicked,
        )
        self.btn_tone_plus = self._create_live_button(
            overlay, plus.x, plus.y, plus.width, plus.height, self.on_tone_plus_clicked,
        )
        self.lbl_tone_step_value = ToneShiftDisplay("+0", overlay)
        self.lbl_tone_step_value.step_requested.connect(self._adjust_tone_step_from_display)
        self.lbl_tone_step_value.reset_requested.connect(self._reset_tone_step_from_display)
        self._place_reference_bounds(overlay, self.lbl_tone_step_value, REFERENCE_LAYOUT.tone_shift_display)

    def _build_knob_reference_controls(self, overlay):
        """Build static lower panels and their independently live knobs."""

        # These panels intentionally replace (not overlay) the two baked lower
        # sections of the reference. Every visible knob inside is a live Blender
        # frame; no static knob remains underneath it.
        self.effect_knob_panel = AnalogKnobPanel(
            "HIỆU ỨNG", (("MACRO 1 · UNASSIGNED", 0, 100),
                            ("MACRO 2 · UNASSIGNED", 0, 100),
                            ("MACRO 3 · UNASSIGNED", 0, 100)), overlay
        )
        self.volume_knob_panel = AnalogKnobPanel(
            "ÂM LƯỢNG", (("Mic Vol", 0, 100), ("Music Vol", 0, 100), ("Tune", -50, 50)), overlay
        )
        self._place_reference_bounds(overlay, self.effect_knob_panel, REFERENCE_LAYOUT.effect_panel)
        self._place_reference_bounds(overlay, self.volume_knob_panel, REFERENCE_LAYOUT.volume_panel)

        knob_specs = (
            ("lbl_reverb_short", "slider_reverb_short", self.on_reverb_short_changed, 50, 0, 100),
            ("lbl_reverb_long", "slider_reverb_long", self.on_reverb_long_changed, 50, 0, 100),
            ("lbl_echo", "slider_echo", self.on_echo_changed, 50, 0, 100),
            ("lbl_mic_vol", "slider_mic_vol", self.on_mic_vol_changed, 0, 0, 100),
            ("lbl_music_vol", "slider_music_vol", self.on_music_vol_changed, 79, 0, 100),
            ("lbl_tune", "slider_tune", self.on_tune_changed, 41, -50, 50),
        )
        self._knob_value_readouts = {}
        self.macro_map_buttons = []
        self.macro_routing_captions = []
        for (label_name, slider_name, callback, default, minimum, maximum), center_x in zip(
            knob_specs, REFERENCE_LAYOUT.knob_centers
        ):
            label = QLabel(str(default), overlay)
            label.hide()
            setattr(self, label_name, label)
            slider = self._slider(callback, "#B8864B", minimum, maximum, default)
            slider.setProperty("reference_overlay", True)
            slider.set_frame_directory(BASE_DIR / "assets" / "analog" / "02_KNOBS" / "frames" / "main")
            # RotaryKnob defaults to a standalone fixed size. The reference
            # overlay owns its geometry, so release those legacy constraints.
            slider.setMinimumSize(1, 1)
            slider.setMaximumSize(16777215, 16777215)
            setattr(self, slider_name, slider)
            knob_size = REFERENCE_LAYOUT.knob_size
            overlay.place(slider, center_x - knob_size // 2, REFERENCE_LAYOUT.knob_y, knob_size, knob_size)
            readout = KnobValueReadout(default, overlay)
            readout_width = REFERENCE_LAYOUT.knob_readout_width
            overlay.place(
                readout,
                center_x - readout_width // 2,
                REFERENCE_LAYOUT.knob_readout_y,
                readout_width,
                REFERENCE_LAYOUT.knob_readout_height,
            )
            self._knob_value_readouts[slider_name] = readout

            if len(self.macro_map_buttons) < 3:
                map_button = QPushButton("MAP", overlay)
                map_button.setFixedSize(28, 17)
                map_button.setStyleSheet(
                    "QPushButton { background: #211b14; color: #e4c18a; border: 1px solid #80603b;"
                    " border-radius: 3px; font: 700 7px 'Segoe UI'; padding: 0; }"
                    "QPushButton:hover { background: #4a321b; border-color: #d2a060; }"
                )
                macro_index = len(self.macro_map_buttons)
                map_button.clicked.connect(lambda _checked=False, index=macro_index: self._open_macro_mapping(index))
                overlay.place(map_button, center_x + 52, REFERENCE_LAYOUT.effect_panel.y + 28, 28, 17)
                self.macro_map_buttons.append(map_button)

                caption = MacroRoutingCaption(macro_index + 1, overlay)
                # This small live surface deliberately sits over the printed
                # effect name in the reference artwork.  The app must report
                # the real DAW assignment, never a hard-coded karaoke label.
                overlay.place(caption, center_x - 58, REFERENCE_LAYOUT.effect_panel.y + 28, 105, 17)
                caption.raise_()
                map_button.raise_()
                self.macro_routing_captions.append(caption)

    def _open_macro_mapping(self, macro_index: int):
        if not self.performance_surface.connected:
            self.performance_surface.refresh_configuration()
            self._poll_performance_surface()
        # The surface response contains project-wide buses plus one snapshot
        # per macro.  Give the dialog the selected macro and the authoritative
        # bus inventory together; previously it received only the outer state,
        # which made a valid DAW look like "No Bus Available".
        state = self._performance_surface_state
        macros = state.get("macros", []) if isinstance(state, dict) else []
        selected_macro = (
            dict(macros[macro_index])
            if isinstance(macros, list) and macro_index < len(macros) and isinstance(macros[macro_index], dict)
            else {}
        )
        selected_macro["buses"] = state.get("buses", []) if isinstance(state, dict) else []
        dialog = PerformanceMacroDialog(macro_index, selected_macro,
                                        self.performance_surface, self)
        dialog.exec()
        self._poll_performance_surface()

    def _build_mode_and_footer_controls(self, overlay):
        """Build mode and footer hit areas over their fixed chassis artwork."""

        mode_button_specs = (
            ("btn_mode_lofi", "LOFI", REFERENCE_LAYOUT.lofi_tile, None),
            ("btn_mode_tre", "nhac_tre", REFERENCE_LAYOUT.nhac_tre_tile,
             ("nhac_tre_background.png", "NHẠC TRẺ", "music", None)),
            ("btn_mode_bolero", "bolero", REFERENCE_LAYOUT.bolero_tile, None),
            ("btn_mode_remix", "remix", REFERENCE_LAYOUT.remix_tile,
             ("remix_background.png", "REMIX", "remix", "remix_icon.png")),
        )
        for attribute, mode_name, bounds, asset_spec in mode_button_specs:
            callback = (
                self.on_lofi_clicked if mode_name == "LOFI"
                else lambda checked=False, n=mode_name, a=attribute: self.on_mode_clicked(n, getattr(self, a))
            )
            if asset_spec:
                artwork_directory = ASSETS_DIR / "analog" / "05_MODE_TILES"
                background_name, label, icon_kind, icon_name = asset_spec
                button = ModeCardButton(
                    artwork_directory / background_name,
                    label,
                    icon_kind,
                    artwork_directory / icon_name if icon_name else None,
                    overlay,
                )
                button.clicked.connect(callback)
                overlay.place(button, bounds.x, bounds.y, bounds.width, bounds.height)
            else:
                button = self._create_live_button(
                    overlay, bounds.x, bounds.y, bounds.width, bounds.height, callback,
                )
            # The four mode tiles share one physical press-surface profile,
            # even though Lofi/Bolero artwork is baked into the chassis.
            button.setProperty("mode_tile", True)
            button.setCheckable(True)
            setattr(self, attribute, button)
        self.mode_buttons = [self.btn_mode_lofi, self.btn_mode_tre, self.btn_mode_bolero, self.btn_mode_remix]
        self.current_mode_button = self.btn_mode_tre

        bottom_specs = (("btn_vang", 38, self.on_toggle_vang), ("btn_mic", 157, self.on_toggle_mic),
                        ("btn_save_song", 276, self.on_save_song_clicked), ("btn_song_list", 395, self.on_song_list_clicked),
                        ("btn_karaoke", 578, self.on_open_karaoke_clicked), ("btn_contact", 697, self.on_contact_clicked),
                        ("btn_settings", 816, self.on_settings_clicked), ("btn_exit", 935, self.request_application_exit))
        for attribute, x, callback in bottom_specs:
            button = self._create_live_button(overlay, x, 598, 108, 70, callback)
            setattr(self, attribute, button)
        self._footer_action_buttons = tuple(getattr(self, name) for name, *_ in bottom_specs)

    def changeEvent(self, event):
        """
        Bắt sự kiện bấm nút thu nhỏ của cửa sổ Windows.
        Thay vì minimize thật xuống taskbar, app sẽ chuyển sang Mini Bar.
        """
        super().changeEvent(event)

        try:
            if event.type() == QEvent.Type.WindowStateChange:
                if self.isMinimized() and not self.compact_mode:
                    QTimer.singleShot(0, self.enter_compact_mode)
        except Exception:
            pass

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, "_reference_overlay") and self.centralWidget() is not None:
            self._reference_overlay.setGeometry(self.centralWidget().rect())
            chassis = self.centralWidget()
            if isinstance(chassis, AnalogChassis):
                self._reference_overlay.set_reference_rect(chassis.reference_rect())
        self._refresh_knob_backdrops()

    def _refresh_knob_backdrops(self):
        """Cache static panel pixels under alpha knob artwork after layout."""
        if not hasattr(self, "effect_knob_panel"):
            return
        panel_groups = (
            (self.effect_knob_panel, (
                "slider_reverb_short", "slider_reverb_long", "slider_echo",
            )),
            (self.volume_knob_panel, (
                "slider_mic_vol", "slider_music_vol", "slider_tune",
            )),
        )
        for panel, slider_names in panel_groups:
            for slider_name in slider_names:
                slider = getattr(self, slider_name, None)
                if slider is None or slider.size().isEmpty():
                    continue
                source = slider.geometry().translated(-panel.x(), -panel.y()).intersected(panel.rect())
                if source.size() == slider.size():
                    slider.set_background_frame(panel.grab(source))

    def _ensure_compact_window(self):
        if self.compact_window is not None:
            return self.compact_window

        win = QWidget()
        win.setWindowTitle("THM Vocal Mini Bar")
        win.setWindowFlag(Qt.WindowType.Tool, True)
        win.setFixedSize(760, 68)

        logo_path = ASSETS_DIR / "logo.png"
        if logo_path.exists():
            win.setWindowIcon(QIcon(str(logo_path)))

        root = QHBoxLayout(win)
        root.setContentsMargins(5, 3, 5, 3)
        root.setSpacing(5)

        tone_box = QFrame()
        tone_box.setObjectName("CompactToneBox")
        tone_box.setFixedSize(160, 48)
        tone_box_layout = QVBoxLayout(tone_box)
        tone_box_layout.setContentsMargins(4, 2, 4, 2)
        tone_box_layout.setSpacing(1)

        self.compact_lbl_tone = QLabel("TONE: Chờ tín hiệu")
        self.compact_lbl_tone.setObjectName("CompactToneLabel")
        self.compact_lbl_tone.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.compact_lbl_tone.setFixedHeight(27)
        tone_box_layout.addWidget(self.compact_lbl_tone)

        self.compact_tone_progress = QProgressBar()
        self.compact_tone_progress.setObjectName("CompactToneProgress")
        self.compact_tone_progress.setRange(0, 100)
        self.compact_tone_progress.setValue(0)
        self.compact_tone_progress.setTextVisible(True)
        self.compact_tone_progress.setFormat("%p%")
        self.compact_tone_progress.setFixedHeight(10)
        tone_box_layout.addWidget(self.compact_tone_progress)

        root.addWidget(tone_box)

        self.compact_btn_minus = QPushButton("-")
        self.compact_btn_minus.setObjectName("ToneControlButton")
        self.compact_btn_minus.setFixedSize(40, 34)
        self.compact_btn_minus.clicked.connect(self.on_tone_minus_clicked)
        root.addWidget(self.compact_btn_minus)

        self.compact_lbl_step = QLabel("+0")
        self.compact_lbl_step.setObjectName("ToneStepValue")
        self.compact_lbl_step.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.compact_lbl_step.setFixedSize(28, 34)
        root.addWidget(self.compact_lbl_step)

        self.compact_btn_plus = QPushButton("+")
        self.compact_btn_plus.setObjectName("ToneControlButton")
        self.compact_btn_plus.setFixedSize(40, 34)
        self.compact_btn_plus.clicked.connect(self.on_tone_plus_clicked)
        root.addWidget(self.compact_btn_plus)

        self.compact_btn_fix_tone = QPushButton("SỬA\nTONE")
        self.compact_btn_fix_tone.setObjectName("SystemSecondaryButton")
        self.compact_btn_fix_tone.setFixedSize(52, 34)
        self.compact_btn_fix_tone.clicked.connect(self.on_confirm_tone_clicked)
        root.addWidget(self.compact_btn_fix_tone)

        music_box = QFrame()
        music_box.setObjectName("CompactMusicBox")
        music_box.setFixedSize(185, 48)
        music_layout = QVBoxLayout(music_box)
        music_layout.setContentsMargins(6, 2, 6, 2)
        music_layout.setSpacing(1)

        self.compact_lbl_music = QLabel("Music Vol: 0")
        self.compact_lbl_music.setObjectName("CompactSmallLabel")
        music_layout.addWidget(self.compact_lbl_music)

        self.compact_music_slider = QSlider(Qt.Orientation.Horizontal)
        self.compact_music_slider.setRange(0, 100)
        self.compact_music_slider.setFixedWidth(145)
        self.compact_music_slider.valueChanged.connect(self.on_compact_music_changed)
        music_layout.addWidget(self.compact_music_slider)
        root.addWidget(music_box)

        self.compact_btn_vang = QPushButton("VANG\nON")
        self.compact_btn_vang.setObjectName("MonitorPrimaryButton")
        self.compact_btn_vang.setFixedSize(58, 36)
        self.compact_btn_vang.clicked.connect(self.on_toggle_vang)
        root.addWidget(self.compact_btn_vang)

        self.compact_btn_mic = QPushButton("MIC\nON")
        self.compact_btn_mic.setObjectName("MonitorPrimaryButton")
        self.compact_btn_mic.setFixedSize(58, 36)
        self.compact_btn_mic.clicked.connect(self.on_toggle_mic)
        root.addWidget(self.compact_btn_mic)

        self.compact_btn_expand = QPushButton("MỞ\nRỘNG")
        self.compact_btn_expand.setObjectName("SystemSecondaryButton")
        self.compact_btn_expand.setFixedSize(62, 36)
        self.compact_btn_expand.clicked.connect(self.exit_compact_mode)
        root.addWidget(self.compact_btn_expand)

        win.setStyleSheet(self.styleSheet() + """
            QWidget {
                background-color: #0b0c0b;
            }
            QFrame#CompactToneBox {
                background-color: #141310;
                border-radius: 8px;
                border: 1px solid #8c765e;
            }
            QLabel#CompactToneLabel {
                background: transparent;
                color: #e5ca9e;
                font-size: 14px;
                font-weight: 900;
                border: none;
                padding: 0px;
            }
            QProgressBar#CompactToneProgress {
                border: none;
                border-radius: 4px;
                background-color: #070807;
                color: #ead8b8;
                font-size: 8px;
                font-weight: 800;
                text-align: center;
            }
            QProgressBar#CompactToneProgress::chunk {
                border-radius: 4px;
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 #8d5c2c,
                    stop:1 #deb16f
                );
            }
            QFrame#CompactMusicBox {
                background-color: #11110f;
                border-radius: 8px;
                border: 1px solid #6f5b43;
            }
            QLabel#CompactSmallLabel {
                color: #e8d9c2;
                font-size: 10px;
                font-weight: 800;
                background: transparent;
            }
        """)

        def _compact_close_event(event):
            # A compact bar normally expands back into the main window when
            # its close control is pressed.  During an application quit it
            # must be allowed to close, otherwise it keeps the Qt app alive.
            if self._shutdown_started:
                event.accept()
                return
            event.ignore()
            self.exit_compact_mode()

        win.closeEvent = _compact_close_event
        self.compact_window = win
        self._sync_compact_controls()
        return win

    def enter_compact_mode(self):
        """Chuyển từ giao diện full sang Mini Bar."""
        if self.compact_mode:
            return

        self.compact_mode = True
        self.normal_geometry_before_compact = self.geometry()

        try:
            self.showNormal()
        except Exception:
            pass

        compact = self._ensure_compact_window()
        self._sync_compact_controls()

        # Đặt mini bar gần vị trí cửa sổ hiện tại để anh dễ thao tác.
        try:
            geo = self.normal_geometry_before_compact
            compact.move(max(0, geo.x()), max(0, geo.y()))
        except Exception:
            pass

        compact.show()
        compact.raise_()
        compact.activateWindow()
        self.hide()

    def exit_compact_mode(self):
        """Mở rộng từ Mini Bar về giao diện full."""
        self.compact_mode = False

        if self.compact_window is not None:
            self.compact_window.hide()

        self.showNormal()
        if self.normal_geometry_before_compact is not None:
            try:
                self.setGeometry(self.normal_geometry_before_compact)
            except Exception:
                pass
        self.raise_()
        self.activateWindow()
        self._sync_compact_controls()

    def _adjust_tone_step_from_display(self, delta: int):
        """Shared LCD interaction path; mirrors the existing +/- button behavior."""
        self._set_pitch_step_value(self.tone_step_value + int(delta), send_midi=True)
        self._update_transposed_tone_label()
        try:
            self.controller.apply_manual_tone(
                self.current_detected_tone.get("key", "C"),
                self.current_detected_tone.get("scale", "Major"),
            )
        except Exception:
            pass
        self._schedule_pitch_transpose_refresh()

    def _reset_tone_step_from_display(self):
        self._adjust_tone_step_from_display(-self.tone_step_value)

    def _sync_compact_controls(self):
        """Đồng bộ dữ liệu từ giao diện full sang Mini Bar."""
        try:
            if not hasattr(self, "compact_lbl_tone"):
                return

            label = self._tone_display_text()
            self.compact_lbl_tone.setText(f"TONE: {label}")
            self.compact_lbl_step.setText(f"{self.tone_step_value:+d}")

            music_value = int(self.slider_music_vol.value()) if hasattr(self, "slider_music_vol") else 0
            self.compact_lbl_music.setText(f"Music Vol: {music_value}")

            self.compact_music_slider.blockSignals(True)
            self.compact_music_slider.setValue(music_value)
            self.compact_music_slider.blockSignals(False)

            self._sync_compact_progress()

            if self.vang_on:
                self.compact_btn_vang.setText("VANG\nON")
                self.compact_btn_vang.setObjectName("MonitorPrimaryButton")
            else:
                self.compact_btn_vang.setText("VANG\nOFF")
                self.compact_btn_vang.setObjectName("SystemDangerButton")

            self.compact_btn_vang.style().unpolish(self.compact_btn_vang)
            self.compact_btn_vang.style().polish(self.compact_btn_vang)
            self.compact_btn_vang.update()

            if hasattr(self, "compact_btn_mic"):
                if self.mic_on:
                    self.compact_btn_mic.setText("MIC\nON")
                    self.compact_btn_mic.setObjectName("MonitorPrimaryButton")
                else:
                    self.compact_btn_mic.setText("MIC\nOFF")
                    self.compact_btn_mic.setObjectName("SystemDangerButton")

                self.compact_btn_mic.style().unpolish(self.compact_btn_mic)
                self.compact_btn_mic.style().polish(self.compact_btn_mic)
                self.compact_btn_mic.update()
        except Exception:
            pass

    def _sync_compact_progress(self):
        """Đồng bộ tiến trình Auto-Key sang mọi bề mặt hiển thị."""
        try:
            progress = int(self.tone_progress.value())
            if hasattr(self, "autokey_progress_strip"):
                self.autokey_progress_strip.set_progress(progress)
            if hasattr(self, "compact_tone_progress"):
                self.compact_tone_progress.setRange(0, 100)
                self.compact_tone_progress.setFormat("%p%")
                self.compact_tone_progress.setValue(progress)
        except Exception:
            pass

    def on_compact_music_changed(self, value: int):
        """Kéo Music Vol trên Mini Bar sẽ điều khiển slider Music Vol chính."""
        try:
            if hasattr(self, "slider_music_vol"):
                self.slider_music_vol.setValue(int(value))
            else:
                self.on_music_vol_changed(int(value))
            self._sync_compact_controls()
        except Exception:
            pass

    def _slider(self, callback, accent, minimum=0, maximum=100, value=50):
        slider = RotaryKnob(accent, value)
        # A new physical drag starts a fresh realtime transaction. Clear an
        # earlier 16 ms cooldown so the first changed integer is dispatched
        # immediately instead of waiting for stale coalescing state.
        slider.dragStarted.connect(self._flush_all_control_backends)
        if callback.__name__ in {"on_tune_changed", "on_mic_vol_changed", "on_music_vol_changed"}:
            slider.dragStarted.connect(self._pause_external_polling_for_drag)
            slider.dragFinished.connect(self._resume_external_polling_after_drag)
        slider.setRange(minimum, maximum)
        slider.setValue(value)
        slider.valueChanged.connect(callback)
        slider.valueCommitted.connect(
            lambda committed, callback_name=callback.__name__: self._remember_slider_value(
                callback_name, committed
            )
        )
        slider.valueCommitted.connect(
            lambda _committed, callback_name=callback.__name__: self._flush_control_backend(callback_name)
        )
        return slider

    _CONTROL_BACKEND_METHODS = {
        "on_reverb_short_changed": "set_reverb_short",
        "on_reverb_long_changed": "set_reverb_long",
        "on_echo_changed": "set_echo",
        "on_mic_vol_changed": "set_mic_volume",
        "on_music_vol_changed": "set_music_volume",
        "on_tune_changed": "set_tune",
    }

    def _queue_control_backend(self, callback_name: str, value: int):
        """Deliver the first drag value immediately, then coalesce at 60 Hz.

        Mouse movement never performs drawing or blocking work here.  It only
        updates a latest-value buffer.  The independent timer drains that
        buffer, while the first value starts the audible/control response with
        no mouse-release dependency.
        """
        if callback_name not in self._CONTROL_BACKEND_METHODS:
            return
        timer = self._control_dispatch_timers.get(callback_name)
        if timer is None:
            timer = QTimer(self)
            timer.setSingleShot(True)
            timer.setTimerType(Qt.TimerType.PreciseTimer)
            timer.timeout.connect(lambda name=callback_name: self._flush_control_backend(name))
            self._control_dispatch_timers[callback_name] = timer
        if timer.isActive():
            self._pending_control_values[callback_name] = int(value)
            return

        # Fast path: effect/MIDI state changes as soon as a rendered integer
        # crosses a step. The timer only batches later movement.
        getattr(self.controller, self._CONTROL_BACKEND_METHODS[callback_name])(int(value))
        timer.start(16)

    def _flush_control_backend(self, callback_name: str):
        """Immediately deliver the latest queued value, including mouse release."""
        value = self._pending_control_values.pop(callback_name, None)
        timer = self._control_dispatch_timers.get(callback_name)
        if timer is not None:
            timer.stop()
        if value is None:
            return
        getattr(self.controller, self._CONTROL_BACKEND_METHODS[callback_name])(value)

    def _flush_all_control_backends(self):
        for callback_name in tuple(self._pending_control_values):
            self._flush_control_backend(callback_name)

    def _pause_external_polling_for_drag(self):
        self._drag_paused_timers = []
        for name in ("external_browser_timer", "end_modulation_apply_timer"):
            timer = getattr(self, name, None)
            if timer is not None and timer.isActive():
                timer.stop()
                self._drag_paused_timers.append((timer, 2000 if name == "external_browser_timer" else 200))

    def _resume_external_polling_after_drag(self):
        for timer, interval in getattr(self, "_drag_paused_timers", []):
            if timer is not None and not timer.isActive():
                timer.start(interval)
        self._drag_paused_timers = []

    def _on_tune_display_changed(self, value: int):
        """Update the Tune readout without invoking backend control on drag."""
        if hasattr(self, "lbl_tune"):
            self.lbl_tune.setText(str(int(value)))

    def _remember_slider_value(self, callback_name: str, value: int):
        """Persist knob values only after dragging ends, not on every mouse move."""
        fields = {
            "on_reverb_short_changed": "reverb_short",
            "on_reverb_long_changed": "reverb_long",
            "on_echo_changed": "echo",
            "on_mic_vol_changed": "mic_vol",
            "on_music_vol_changed": "music_vol",
            "on_tune_changed": "tune",
        }
        field = fields.get(callback_name)
        if field:
            self.controller.remember_mode_slider_value(field, int(value))

    def _silent_message(self, title: str, text: str):
        """Popup không phát âm thanh Windows."""
        box = QMessageBox(self)
        box.setWindowTitle(title)
        box.setProperty("analogTitle", title)
        box.setText(text)
        box.setIcon(QMessageBox.Icon.NoIcon)
        box.setStandardButtons(QMessageBox.StandardButton.Ok)
        apply_analog_message_theme(box)
        return box.exec()

    def _silent_question(self, title: str, text: str, yes_text: str = "LƯU", no_text: str = "BỎ QUA") -> bool:
        """Popup hỏi không âm thanh. Trả True nếu bấm nút yes_text."""
        box = QMessageBox(self)
        box.setWindowTitle(title)
        box.setProperty("analogTitle", title)
        box.setText(text)
        box.setIcon(QMessageBox.Icon.NoIcon)
        btn_yes = box.addButton(yes_text, QMessageBox.ButtonRole.AcceptRole)
        box.addButton(no_text, QMessageBox.ButtonRole.RejectRole)
        apply_analog_message_theme(box)
        box.exec()
        return box.clickedButton() == btn_yes

    def _mmss_to_seconds(self, text: str) -> int:
        return parse_mmss(text, allow_hours=True)


    def _tone_result_from_key_scale(self, key: str, scale: str, confidence: float = 1.0) -> dict:
        key = self.controller.normalize_key_for_scale(str(key or "C").strip(), str(scale or "Major").strip())
        scale = str(scale or "Major").strip()
        return {
            "key": key,
            "key_index": self.controller.key_name_to_index(key),
            "scale": scale,
            "label": f"{key} {scale}",
            "confidence": float(confidence or 1.0),
        }

    def _transpose_key_for_runtime(self, key: str, scale: str, semitone: int) -> str:
        try:
            base_pc = self._key_to_pitch_class(key)
            return self._pitch_class_to_key((base_pc + int(semitone)) % 12, scale)
        except Exception:
            return self.controller.normalize_key_for_scale(key, scale)

    def _runtime_tone_result(self, key: str, scale: str, semitone: int | None = None) -> dict:
        if semitone is None:
            semitone = int(getattr(self, "tone_step_value", 0) or 0)
        runtime_key = self._transpose_key_for_runtime(key, scale, int(semitone))
        return self._tone_result_from_key_scale(runtime_key, scale, confidence=1.0)

    def _apply_runtime_tone_to_autotune(self, key: str, scale: str, stage: str = "base"):
        """
        Gửi key/scale đã quy đổi theo +/- tone nhạc hiện tại sang Auto-Tune.
        Không đụng SoundShifter CC36 ở đây.

        Quan trọng: KHÔNG được ghi đè self.base_tone_for_transpose bằng tone đã quy đổi.
        Nếu ghi đè, app sẽ bị lỗi double-transpose:
            F# Minor -2 = E Minor, rồi E Minor lại -2 = D Minor.
        """
        tone_result = self._runtime_tone_result(key, scale)
        try:
            self.controller.apply_detected_tone(tone_result)
        except Exception as e:
            print("Không gửi được key/scale runtime:", e)
            return None

        self.end_modulation_applied_stage = str(stage or "base")
        # The DAW's Auto-Key link owns the song's base signature. End-section
        # modulation is an internal playback event and must not unexpectedly
        # rewrite the singer's global VibeAutotune key while a take is running.
        if self.end_modulation_applied_stage == "base":
            self._publish_studioforge_key(
                tone_result.get("key", key),
                tone_result.get("scale", scale),
                tone_result.get("confidence", 0.0),
                tone_result.get("title", ""),
            )
        self.current_detected_tone = dict(tone_result)
        self.tone_is_resolved = True
        self.lbl_tone.setText(self._tone_display_text(tone_result.get("label", "")))
        try:
            self._sync_compact_controls()
        except Exception:
            pass
        try:
            self._update_end_modulation_status_line()
        except Exception:
            pass
        return tone_result

    def _sync_end_modulation_base_key(self, key: str, scale: str):
        """Khi detect tone mới, cập nhật base_key trong active_end_modulation (in-memory).
        Ngăn RUNTIME timer đè lại tone cũ (C Minor) lên cả 2 mic mỗi 200ms."""
        if isinstance(self.active_end_modulation, dict):
            self.active_end_modulation["base_key"] = key
            self.active_end_modulation["base_scale"] = scale

    def _load_saved_end_modulation_for_current_video(self, youtube_url: str = ""):
        youtube_url = str(youtube_url or "").strip()
        if not youtube_url:
            ctx = self._get_active_youtube_context()
            youtube_url = str(ctx.get("url", "") or "").strip()
        if not youtube_url:
            self.active_end_modulation = None
            self._update_end_modulation_status_line()
            return None
        try:
            saved = self.controller.find_saved_end_modulation_by_url(youtube_url)
        except Exception:
            saved = None
        self.active_end_modulation = saved if isinstance(saved, dict) and saved.get("enabled", True) else None
        self.end_modulation_applied_stage = "base"
        self._update_end_modulation_status_line()
        return self.active_end_modulation

    def _runtime_label_for_saved_end_modulation(self, key: str, scale: str) -> str:
        """Trả về key/scale thực tế sau khi quy đổi theo mức tăng/hạ tone nhạc hiện tại."""
        try:
            tone = self._runtime_tone_result(key, scale)
            return str(tone.get("label", f"{key} {scale}") or f"{key} {scale}")
        except Exception:
            return f"{key} {scale}"

    def _update_end_modulation_status_line(self):
        """
        Dòng trạng thái dưới progress luôn hiện.
        Chỉ hiển thị: thời gian lên tone, tone sẽ lên đã quy đổi theo +/- nhạc, và trạng thái nhạc.
        Không hiển thị thêm "Tone hiện tại" để giao diện gọn hơn.
        """
        try:
            if not hasattr(self, "lbl_tone_status"):
                return

            # Hiện cảnh báo yt-dlp cũ trong 60 giây đầu sau khi check xong
            warn_msg = getattr(self, "_ytdlp_warn_msg", "")
            warn_until = getattr(self, "_ytdlp_warn_until", 0)
            if warn_msg and time.time() < warn_until:
                self.lbl_tone_status.setText(warn_msg)
                return

            try:
                step = int(getattr(self, "tone_step_value", 0) or 0)
            except Exception:
                step = 0

            if step == 0:
                music_text = "Nhạc: +0"
            elif step > 0:
                music_text = f"Nhạc: +{step}"
            else:
                music_text = f"Nhạc: {step}"

            # P0.6 — Visual feedback: nếu pipeline đang chạy, ưu tiên hiển thị
            # trạng thái đang phân tích để user biết app đang làm việc.
            if getattr(self, "end_modulation_running", False):
                self.lbl_tone_status.setText(f"🔄 Đang dò lên tone cuối bài... | {music_text}")
                # Skip section sync khi đang chạy (active_end_modulation chưa thay đổi)
                try:
                    self._update_end_mod_action_section()
                except Exception:
                    pass
                return

            # Sync indicator prefix (🟢 brave_cdp / 🔴 unavailable / ⚪ video_changed)
            sync_prefix = self._get_time_sync_prefix()

            end_mod = getattr(self, "active_end_modulation", None)
            if not isinstance(end_mod, dict) or not end_mod.get("enabled", True):
                self.lbl_tone_status.setText(f"Thời gian lên tone: Chưa lưu | Tone sẽ lên: -- | {music_text}")
                # Sync action section
                try:
                    self._update_end_mod_action_section()
                except Exception:
                    pass
                return

            raise_text = str(end_mod.get("raise_time_text", "") or "").strip()
            if not raise_text:
                try:
                    raise_sec = int(end_mod.get("raise_time_sec", 0) or 0)
                except Exception:
                    raise_sec = 0
                raise_text = self._seconds_to_mmss(raise_sec) if raise_sec > 0 else "--:--"

            end_key = str(end_mod.get("end_key", "") or end_mod.get("new_key", "") or "").strip()
            end_scale = str(
                end_mod.get("end_scale", "")
                or end_mod.get("new_scale", "")
                or end_mod.get("base_scale", "Major")
                or "Major"
            ).strip()

            if end_key:
                runtime_end_label = self._runtime_label_for_saved_end_modulation(end_key, end_scale)
            else:
                runtime_end_label = "--"

            self.lbl_tone_status.setText(
                f"{sync_prefix}Thời gian lên tone: {raise_text} | Tone sẽ lên: {runtime_end_label} | {music_text}"
            )
        except Exception as e:
            print("Không cập nhật được dòng trạng thái lên tone:", e)

        # P0 — Đồng bộ luôn end_mod_action_section. Mọi caller đã call
        # _update_end_modulation_status_line tự động được hưởng cập nhật UI.
        try:
            self._update_end_mod_action_section()
        except Exception:
            pass

    def _get_current_youtube_time_seconds(self):
        """
        Đọc currentTime thật từ YouTube nội bộ hoặc Brave external.

        Nguyên tắc an toàn: KHÔNG dùng fallback_elapsed (đếm wallclock không
        sync với YouTube → lệch khi pause/seek/buffer). Nếu không đọc được
        time thật → return None → app KHÔNG gửi MIDI sai.

        Realtime extrapolation: giữa các poll, tính
            current_time = last_current_time + (now - last_poll_wallclock)
        nếu is_playing → giảm lag từ 1000ms → ~50ms.

        Side effect: nếu Brave đã chuyển sang bài KHÁC (auto-play radio),
        clear active_end_modulation để app không apply nhầm tone của bài cũ.
        """
        ctx = self._get_active_youtube_context()
        video_id = str(ctx.get("video_id", "") or "").strip()

        # YouTube nội bộ có current_time sẵn.
        try:
            if self.youtube_browser and video_id and self.youtube_browser.current_video_id == video_id:
                base_time = float(getattr(self.youtube_browser, "last_current_time", 0.0) or 0.0)
                try:
                    is_playing = bool(getattr(self.youtube_browser, "last_is_playing", False))
                    last_poll = float(getattr(self.youtube_browser, "last_poll_wallclock", 0.0) or 0.0)
                    if is_playing and last_poll > 0:
                        elapsed = max(0.0, time.time() - last_poll)
                        if elapsed <= 1.5:
                            base_time += elapsed
                except Exception:
                    pass
                return base_time, "internal_youtube"
        except Exception:
            pass

        # Brave external qua CDP.
        try:
            state = self.controller.get_external_youtube_playback_state(video_id=video_id)
            if isinstance(state, dict) and state.get("ok"):
                # Detect video change: nếu Brave đang phát bài KHÁC với
                # bài đang track → clear active_end_modulation để tránh apply nhầm.
                actual_vid = str(state.get("actual_video_id", "") or "")
                if (
                    actual_vid
                    and video_id
                    and actual_vid != video_id
                    and isinstance(self.active_end_modulation, dict)
                ):
                    print(
                        f"[Video change detected] Brave chuyển từ {video_id} sang {actual_vid}"
                        f" → clear active_end_modulation"
                    )
                    self.active_end_modulation = None
                    self.end_modulation_applied_stage = "base"
                    try:
                        self._update_end_modulation_status_line()
                    except Exception:
                        pass
                    # Invalidate audio cache cho video cũ
                    try:
                        self.controller.invalidate_end_modulation_audio_cache()
                    except Exception:
                        pass
                    return None, "video_changed"
                return float(state.get("currentTime", 0.0) or 0.0), "brave_cdp"
        except Exception:
            pass

        # KHÔNG fallback_elapsed nữa — return None để app KHÔNG apply MIDI sai.
        # User sẽ thấy cảnh báo qua _check_saved_end_modulation_auto_apply.
        return None, "unavailable"


    def _reapply_current_end_modulation_stage_after_pitch_change(self):
        end_mod = self.active_end_modulation
        if not isinstance(end_mod, dict):
            return
        try:
            if self.end_modulation_applied_stage == "end":
                self._apply_runtime_tone_to_autotune(end_mod.get("end_key", ""), end_mod.get("end_scale", end_mod.get("base_scale", "Major")), stage="end")
            else:
                self._apply_runtime_tone_to_autotune(end_mod.get("base_key", ""), end_mod.get("base_scale", "Major"), stage="base")
        except Exception as e:
            print("Không cập nhật lại key/scale sau khi tăng/hạ tone:", e)

    def _offer_brave_auto_fix(self):
        """
        Khi app phát hiện không đọc được time YouTube (CDP fail), chẩn đoán
        nguyên nhân và đề xuất 1 nút FIX TỰ ĐỘNG cho user.

        Diagnose:
            - Brave đang chạy nhưng port 9222 không mở → cần restart Brave
              với flag CDP đúng → offer auto-restart
            - Brave không chạy → hướng dẫn bấm KARAOKE
            - Port 9222 mở nhưng không có tab YouTube → hướng dẫn paste URL
        """
        try:
            diag = self.controller.diagnose_brave_cdp()
        except Exception:
            diag = {"is_brave_running": False, "cdp_port_open": False, "tabs_count": 0}

        is_running = bool(diag.get("is_brave_running", False))
        cdp_open = bool(diag.get("cdp_port_open", False))
        tabs = int(diag.get("tabs_count", 0) or 0)

        if is_running and not cdp_open:
            # Case 1: Brave chạy nhưng KHÔNG có CDP — log silent, không popup
            print(f"[CDP diagnose] Brave đang chạy nhưng port {self.controller.browser_launcher_service.debug_port} đóng. Bấm KARAOKE để restart Brave với CDP.")
            self.lbl_tone_status.setText("Brave chạy nhưng thiếu CDP — bấm KARAOKE để restart")

        elif not is_running:
            # Case 2: Brave không chạy — log silent, không popup
            print("[CDP diagnose] Brave chưa chạy. Bấm KARAOKE để mở.")
            self.lbl_tone_status.setText("Chưa mở Brave — bấm KARAOKE")

        elif cdp_open and tabs > 0:
            # Case 3: CDP OK nhưng không có tab YouTube — log silent, không popup
            print(f"[CDP diagnose] CDP OK ({tabs} tab) nhưng không tìm thấy tab YouTube. Paste URL vào Brave.")
            self.lbl_tone_status.setText("Brave đang mở — paste URL YouTube vào Brave")

        else:
            # Case 4: edge case — log silent
            print(f"[CDP diagnose] Không kết nối được: running={is_running}, cdp={cdp_open}, tabs={tabs}")

    def _update_time_sync_indicator(self, time_source: str, current_time):
        """
        Lưu trạng thái sync vào attribute. _update_end_modulation_status_line
        sẽ đọc và prefix vào dòng status mỗi khi cập nhật.

        Indicator:
            🟢 brave_cdp / internal_youtube  → đồng bộ tốt
            🔴 unavailable                    → không đọc được time → app KHÔNG apply
            ⚪ video_changed                  → đang chuyển bài
        """
        self._last_time_source = str(time_source or "unavailable")
        self._last_time_source_value = current_time
        # Trigger UI refresh
        try:
            self._update_end_modulation_status_line()
        except Exception:
            pass

    def _get_time_sync_prefix(self) -> str:
        """Trả về emoji prefix tương ứng trạng thái sync hiện tại."""
        try:
            ts = str(getattr(self, "_last_time_source", "") or "")
            ct = getattr(self, "_last_time_source_value", None)
            if ts in ("brave_cdp", "internal_youtube") and ct is not None:
                return "🟢 "
            if ts == "video_changed":
                return "⚪ "
            if ts:
                return "🔴 "
        except Exception:
            pass
        return ""

    def _check_saved_end_modulation_auto_apply(self):
        """
        Nếu bài đã lưu end_modulation, tự đổi key/scale theo currentTime YouTube.

        Bản FIX TRIỆT ĐỂ:
        - Cache chỉ lưu tone gốc và tone cuối gốc.
        - Khi gửi MIDI luôn tính runtime_key = key_gốc + self.tone_step_value hiện tại.
        - Không tin vào current_detected_tone để quyết định có gửi lại hay không, vì có thể
          một luồng khác đã gửi key gốc vào Auto-Tune nhưng không cập nhật UI.
        - Vì vậy timer sẽ re-apply runtime tone định kỳ theo stage hiện tại để sửa ngay
          nếu có luồng nào gửi nhầm A Minor thay vì G Minor khi Nhạc = -2.
        """
        end_mod = self.active_end_modulation
        if not isinstance(end_mod, dict) or not end_mod.get("enabled", True):
            return

        try:
            apply_time = int(end_mod.get("apply_time_sec", end_mod.get("raise_time_sec", 0)) or 0)
            if apply_time <= 0:
                return

            current_time, time_source = self._get_current_youtube_time_seconds()

            # Throttled log mỗi ~5s để user thấy app đang dùng time source nào.
            # Giúp debug khi không sync (tránh "không biết app đang làm gì").
            now_wall = time.time()
            last_src_log = float(getattr(self, "_last_time_source_log_at", 0.0) or 0.0)
            if now_wall - last_src_log >= 5.0:
                self._last_time_source_log_at = now_wall
                ct_text = f"{current_time:.1f}s" if current_time is not None else "None"
                print(f"[End mod sync] time_source={time_source} | current_time={ct_text}")

            # Cập nhật UI status indicator
            try:
                self._update_time_sync_indicator(time_source, current_time)
            except Exception:
                pass

            # Nếu video đã đổi → active_end_modulation đã được clear bên trong
            # _get_current_youtube_time_seconds → không apply gì thêm.
            if time_source == "video_changed":
                return

            if current_time is None:
                # Cảnh báo + AUTO-FIX option khi không đọc được time.
                # Cảnh báo 1 lần/phiên để không spam.
                if not getattr(self, "_warned_no_time_source", False):
                    self._warned_no_time_source = True
                    try:
                        self._offer_brave_auto_fix()
                    except Exception:
                        pass
                return

            base_key = str(end_mod.get("base_key", "") or "").strip()
            base_scale = str(end_mod.get("base_scale", "") or "").strip()
            end_key = str(end_mod.get("end_key", "") or end_mod.get("new_key", "") or "").strip()
            end_scale = str(end_mod.get("end_scale", "") or end_mod.get("new_scale", "") or base_scale).strip()

            if not base_key or not base_scale or not end_key or not end_scale:
                return

            # Hysteresis nhẹ để tránh nhấp nháy khi currentTime sát mốc.
            if current_time >= apply_time:
                applied = self._apply_runtime_tone_to_autotune(end_key, end_scale, stage="end")
                if applied:
                    print(
                        f"Auto lên tone cuối bài RUNTIME: {applied.get('label')} "
                        f"tại {self._seconds_to_mmss(current_time)} ({time_source}) | Nhạc {self.tone_step_value:+d}"
                    )
                    self._update_end_modulation_status_line()

            elif current_time < max(0, apply_time - 2):
                applied = self._apply_runtime_tone_to_autotune(base_key, base_scale, stage="base")
                if applied:
                    print(
                        f"Auto giữ/trả tone đầu RUNTIME: {applied.get('label')} "
                        f"tại {self._seconds_to_mmss(current_time)} ({time_source}) | Nhạc {self.tone_step_value:+d}"
                    )
                    self._update_end_modulation_status_line()

        except Exception as e:
            print("Lỗi auto apply end modulation:", e)


    def _edit_end_modulation_result_dialog(self, result: dict):
        """Cho người dùng sửa thời gian/tone cuối trước khi lưu cache."""
        dlg = QDialog(self)
        dlg.setWindowTitle("Sửa thông tin lên tone cuối bài")
        dlg.setModal(True)
        dlg.setStyleSheet(ANALOG_DIALOG_QSS)
        dlg.resize(420, 260)

        root = QVBoxLayout(dlg)
        form = QFormLayout()

        edt_time = QLineEdit(str(result.get("raise_time_text", "00:00") or "00:00"))
        edt_time.setPlaceholderText("mm:ss, ví dụ 04:21")

        cbo_scale = QComboBox()
        cbo_scale.addItems(["Major", "Minor"])
        current_scale = str(result.get("new_scale", result.get("base_scale", "Major")) or "Major")
        if current_scale in ["Major", "Minor"]:
            cbo_scale.setCurrentText(current_scale)

        cbo_key = QComboBox()

        def reload_keys(selected_key: str = ""):
            scale_name = cbo_scale.currentText()
            try:
                opts = self.controller.get_key_options_for_scale(scale_name)
            except Exception:
                opts = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]
            cbo_key.blockSignals(True)
            cbo_key.clear()
            cbo_key.addItems(opts)
            try:
                normalized = self.controller.normalize_key_for_scale(selected_key or result.get("new_key", "C"), scale_name)
            except Exception:
                normalized = selected_key or result.get("new_key", "C")
            if normalized in opts:
                cbo_key.setCurrentText(normalized)
            cbo_key.blockSignals(False)

        reload_keys(str(result.get("new_key", "C") or "C"))
        cbo_scale.currentTextChanged.connect(lambda _s: reload_keys(cbo_key.currentText()))

        form.addRow("Thời điểm lên tone:", edt_time)
        form.addRow("Key cuối:", cbo_key)
        form.addRow("Scale cuối:", cbo_scale)

        note = QLabel("Gợi ý: nhập đúng thời điểm theo YouTube. App sẽ apply tone ngay tại thời điểm này.")
        note.setWordWrap(True)
        form.addRow("", note)

        root.addLayout(form)

        row = QHBoxLayout()
        btn_ok = QPushButton("OK")
        btn_cancel = QPushButton("HỦY")
        btn_ok.clicked.connect(dlg.accept)
        btn_cancel.clicked.connect(dlg.reject)
        row.addStretch()
        row.addWidget(btn_ok)
        row.addWidget(btn_cancel)
        root.addLayout(row)

        if dlg.exec() != QDialog.DialogCode.Accepted:
            return None

        fixed = dict(result)
        raise_sec = self._mmss_to_seconds(edt_time.text())
        # APPLY_LEAD_SEC = 0 (V7.14): apply ngay tại raise_time, không sớm 3s nữa
        apply_sec = max(0, raise_sec)
        end_key = cbo_key.currentText().strip()
        end_scale = cbo_scale.currentText().strip()

        fixed["raise_time_sec"] = int(raise_sec)
        fixed["raise_time_text"] = self._seconds_to_mmss(raise_sec)
        fixed["apply_time_sec"] = int(apply_sec)
        fixed["apply_time_text"] = self._seconds_to_mmss(apply_sec)
        fixed["new_key"] = end_key
        fixed["new_scale"] = end_scale
        try:
            fixed["new_key_index"] = self.controller.key_name_to_index(end_key)
        except Exception:
            pass
        fixed["new_label"] = f"{end_key} {end_scale}"
        fixed["manual_edited"] = True
        fixed["debug_reason"] = str(fixed.get("debug_reason", "")) + "|manual_edit_popup"
        return fixed

    def _update_end_mod_action_section(self):
        """
        Show/hide divider + info + 2 buttons trong mode card dựa vào
        self.active_end_modulation. Cập nhật text info "🎵 +N → KEY @ MM:SS".

        Gọi sau mỗi lần active_end_modulation thay đổi:
            - Sau khi save end mod (popup hoặc edit)
            - Sau khi load saved (mở bài đã có cache)
            - Sau khi clear (HỦY LẦN NÀY hoặc đổi bài)
        """
        try:
            if not hasattr(self, "btn_edit_end_mod"):
                return  # mode card chưa build xong

            end_mod = getattr(self, "active_end_modulation", None)
            has_active = isinstance(end_mod, dict) and end_mod.get("enabled", True)

            self.end_mod_divider.setVisible(has_active)
            self.lbl_end_mod_info.setVisible(has_active)
            self.end_mod_action_widget.setVisible(has_active)

            if has_active:
                semi = int(end_mod.get("semitone_up", 0) or 0)
                end_key = str(end_mod.get("end_key", "") or end_mod.get("new_key", "") or "").strip()
                end_scale = str(
                    end_mod.get("end_scale", "")
                    or end_mod.get("new_scale", "")
                    or end_mod.get("base_scale", "Major")
                    or "Major"
                ).strip()
                raise_text = str(end_mod.get("raise_time_text", "") or "").strip()
                if not raise_text:
                    try:
                        sec = int(end_mod.get("raise_time_sec", 0) or 0)
                        raise_text = self._seconds_to_mmss(sec) if sec > 0 else "--:--"
                    except Exception:
                        raise_text = "--:--"

                # Hiển thị nhãn key sau khi quy đổi theo +/- nhạc hiện tại
                if end_key:
                    runtime_label = self._runtime_label_for_saved_end_modulation(end_key, end_scale)
                else:
                    runtime_label = f"{end_key} {end_scale}".strip() or "--"

                self.lbl_end_mod_info.setText(f"+{semi} → {runtime_label} @ {raise_text}")
        except Exception as e:
            print("Không cập nhật được end_mod_action_section:", e)

    def on_edit_end_mod_clicked(self):
        """
        Mở dialog SỬA thời điểm/key/scale của lên tone cuối bài, lưu lại,
        cập nhật active_end_modulation ngay → timer 700ms apply tức thì.
        """
        try:
            end_mod = getattr(self, "active_end_modulation", None)
            if not isinstance(end_mod, dict):
                self._silent_message("Chưa có dữ liệu", "Bài hát hiện tại chưa có dữ liệu lên tone cuối bài để sửa.")
                return

            # Build result dict từ active_end_modulation để truyền vào dialog
            result_for_dialog = {
                "raise_time_sec": int(end_mod.get("raise_time_sec", 0) or 0),
                "raise_time_text": str(end_mod.get("raise_time_text", "") or ""),
                "new_key": str(end_mod.get("end_key", "") or end_mod.get("new_key", "") or ""),
                "new_scale": str(end_mod.get("end_scale", "") or end_mod.get("new_scale", "Major") or "Major"),
                "base_key": str(end_mod.get("base_key", "") or ""),
                "base_scale": str(end_mod.get("base_scale", "Major") or "Major"),
                "base_label": str(end_mod.get("base_label", "") or ""),
                "semitone_up": int(end_mod.get("semitone_up", 0) or 0),
                "url": "",  # sẽ tự fill từ current_song_playing
                "title": "",
            }

            # Mở dialog hiện có
            edited = self._edit_end_modulation_result_dialog(result_for_dialog)
            if edited is None:
                return  # user hủy

            # Tính lại key_index nếu key đổi
            try:
                edited["new_key_index"] = self.controller.key_name_to_index(edited.get("new_key", "C"))
            except Exception:
                pass
            edited["new_label"] = f"{edited.get('new_key', '')} {edited.get('new_scale', '')}".strip()

            # Lấy URL/title từ nhiều nguồn theo thứ tự ưu tiên
            url = ""
            title = ""

            # 1. Từ active_end_modulation (đã save từ trước)
            try:
                if isinstance(end_mod, dict):
                    url = str(end_mod.get("canonical_url", "") or end_mod.get("url", "") or "").strip()
                    title = str(end_mod.get("title", "") or "").strip()
            except Exception:
                pass

            # 2. Từ active YouTube context (Brave/in-app)
            if not url:
                try:
                    ctx = self._get_active_youtube_context()
                    url = str(ctx.get("url", "") or "").strip()
                    title = title or str(ctx.get("title", "") or "").strip()
                except Exception:
                    pass

            # 3. Từ current_song_playing (nếu user pick từ song list)
            if not url:
                try:
                    current = getattr(self, "current_song_playing", {}) or {}
                    url = str(current.get("youtube_url", "") or current.get("url", "") or "").strip()
                    title = title or str(current.get("title", "") or "").strip()
                except Exception:
                    pass

            # 4. Từ youtube_browser internal
            if not url:
                try:
                    if self.youtube_browser:
                        video_id = str(getattr(self.youtube_browser, "current_video_id", "") or "").strip()
                        if video_id:
                            url = f"https://www.youtube.com/watch?v={video_id}"
                            title = title or str(getattr(self.youtube_browser, "current_title", "") or "")
                except Exception:
                    pass

            if not url:
                self._silent_message(
                    "Lỗi",
                    "Không xác định được URL bài hát hiện tại.\n"
                    "Vui lòng đảm bảo YouTube đang phát hoặc paste URL vào panel YouTube."
                )
                return

            # Lưu vào cache + update active
            try:
                saved = self.controller.save_end_modulation_for_youtube(
                    youtube_url=url,
                    title=title,
                    modulation_result=edited,
                    base_result=self._build_base_result_for_end_modulation(),
                )
                end_mod_new = saved.get("end_modulation", {}) if isinstance(saved, dict) else {}
                if isinstance(end_mod_new, dict) and end_mod_new:
                    # User đã verify qua dialog → MARK manual_confirmed=True
                    # → pipeline sẽ skip re-analyze sau này (P2.2 correction learning)
                    end_mod_new["manual_confirmed"] = True
                    end_mod_new["auto_saved"] = False
                    self.active_end_modulation = end_mod_new
                    self.end_modulation_applied_stage = "base"
                    self._update_end_modulation_status_line()
                    self._update_end_mod_action_section()
                self._silent_message(
                    "Đã sửa",
                    "Đã cập nhật thời điểm/key của lên tone cuối bài.\n"
                    f"Mới: {edited.get('new_label', '')} @ {edited.get('raise_time_text', '')}\n"
                    "App sẽ apply ngay khi YouTube chạm mốc mới."
                )
            except Exception as e:
                self._silent_message("Lỗi lưu", f"Không lưu được sửa đổi:\n{e}")
        except Exception as e:
            print("Lỗi on_edit_end_mod_clicked:", e)

    def on_skip_end_mod_clicked(self):
        """
        HỦY áp dụng lên tone trong PHIÊN HÁT HIỆN TẠI.
        - Set active_end_modulation = None (memory only)
        - KHÔNG xóa cache trên ổ cứng (tone_cache.json giữ nguyên)
        - Reset applied_stage về 'base' để timer 700ms revert tone gốc tức thời
        - Bài sau load lại cùng URL → cache còn → tự động re-active

        Mục đích: cứu vãn 1 click khi app apply nhầm modulation, không phải
        xóa vĩnh viễn dữ liệu đã lưu.
        """
        try:
            if not isinstance(getattr(self, "active_end_modulation", None), dict):
                return

            # Lấy thông tin để hiển thị confirm
            end_mod = self.active_end_modulation or {}
            end_label = str(end_mod.get("end_label", "") or end_mod.get("new_label", "") or "").strip()
            raise_time = str(end_mod.get("raise_time_text", "") or "").strip()

            reply = QMessageBox.question(
                self,
                "Hủy lên tone lần này",
                f"Hủy áp dụng lên tone {end_label} @ {raise_time} trong phiên hát hiện tại?\n\n"
                f"Cache sẽ được GIỮ — bài sau load lại sẽ tự động áp dụng.\n"
                f"Để xóa cache vĩnh viễn, dùng nút Xóa Cache trong Cài đặt.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            if reply != QMessageBox.StandardButton.Yes:
                return

            # Clear active (memory only, cache trên ổ cứng vẫn còn)
            self.active_end_modulation = None
            self.end_modulation_applied_stage = "base"

            # Force apply tone GỐC ngay tức thời (revert nếu đang ở tone CUỐI)
            try:
                base_result = self._build_base_result_for_end_modulation()
                if base_result.get("key") and base_result.get("scale"):
                    self._apply_runtime_tone_to_autotune(
                        base_result["key"], base_result["scale"], stage="base"
                    )
            except Exception:
                pass

            self._update_end_modulation_status_line()
            self._update_end_mod_action_section()

            self._silent_message(
                "Đã hủy lần này",
                "Đã hủy áp dụng lên tone trong phiên hát hiện tại.\n"
                "Cache vẫn được giữ — bài sau load lại sẽ tự động áp dụng."
            )
        except Exception as e:
            print("Lỗi on_skip_end_mod_clicked:", e)

    def _build_base_result_for_end_modulation(self) -> dict:
        key = str(self.base_tone_for_transpose.get("key", self.current_detected_tone.get("key", "C")) or "C")
        scale = str(self.base_tone_for_transpose.get("scale", self.current_detected_tone.get("scale", "Major")) or "Major")
        key = self.controller.normalize_key_for_scale(key, scale)
        return {
            "key": key,
            "key_index": self.controller.key_name_to_index(key),
            "scale": scale,
            "label": f"{key} {scale}",
            "confidence": float(self.current_detected_tone.get("confidence", 0.0) or 0.0),
        }

    def _cancel_end_modulation_check(self):
        try:
            if hasattr(self, "end_modulation_timer") and self.end_modulation_timer.isActive():
                self.end_modulation_timer.stop()
        except Exception:
            pass
        self.end_modulation_pending_context = {}

    def _schedule_end_modulation_check(self, result: dict, delay_ms: int = 20000, reason: str = "auto_detect"):
        """
        V6 Guaranteed Trigger:
        Sau khi có tone gốc mới, app bắt buộc lên lịch phân tích lên tone cuối bài.
        Auto Detect chạy sau khoảng 20 giây (V7.14 giảm từ 60s); FIX/SỬA TONE
        chạy sau khoảng 10 giây.
        Không chặn vì tone_step +/-; popup sẽ tự ghi rõ nhạc đang tăng/giảm.
        """
        try:
            if not isinstance(result, dict):
                return

            video_id = str(result.get("video_id", "") or "").strip()
            url = str(result.get("url", "") or "").strip()
            title = str(result.get("title", "") or "").strip()
            key = str(result.get("key", self.base_tone_for_transpose.get("key", "")) or "").strip()
            scale = str(result.get("scale", self.base_tone_for_transpose.get("scale", "")) or "").strip()

            if not video_id or not url or not key or not scale:
                return

            # V7.14: giảm delay auto detect 60s → 20s theo yêu cầu user
            # (tiết kiệm thời gian chờ phân tích).
            # Nếu trong thời gian đó anh bấm FIX TONE hoặc SỬA TONE,
            # các luồng đó sẽ schedule lại sau 10 giây như cũ.
            if str(reason or "").strip() == "auto_detect":
                delay_ms = 20000

            # P2.2 — CORRECTION LEARNING: nếu video đã có cache manual_confirmed
            # (user đã LƯU/SỬA tay rồi), KHÔNG re-analyze. Tin tuyệt đối vào
            # giá trị user đã verify, tránh pipeline override mỗi lần phát lại.
            try:
                saved = self.controller.find_saved_end_modulation_by_url(url)
                if (
                    isinstance(saved, dict)
                    and saved.get("enabled", True)
                    and bool(saved.get("manual_confirmed", False))
                ):
                    # Đã có manual cache → load thẳng, skip pipeline
                    self.active_end_modulation = saved
                    self.end_modulation_applied_stage = "base"
                    self._update_end_modulation_status_line()
                    print(f"P2.2 — Bài có manual_confirmed cache, skip re-analyze: {url}")
                    return
            except Exception:
                pass

            # V6: Trong giai đoạn test/tinh chỉnh, vẫn cho phép phân tích lại dù bài đã từng lưu.
            # Khi thuật toán ổn định, mình có thể bật lại điều kiện bỏ qua bài đã lưu.
            self.end_modulation_user_intervened = False
            self.end_modulation_pending_context = {
                "video_id": video_id,
                "url": url,
                "title": title,
                "key": key,
                "scale": scale,
                "reason": reason,
                "base_result": {
                    "key": key,
                    "key_index": self.controller.key_name_to_index(key),
                    "scale": scale,
                    "label": f"{key} {scale}",
                    "confidence": float(result.get("confidence", 0.0) or 0.0),
                    "manual_verified": bool(result.get("manual_verified", False)),
                },
            }

            self.end_modulation_timer.start(max(1000, int(delay_ms)))
        except Exception as e:
            print("Không schedule được phân tích lên tone cuối bài:", e)

    def _run_end_modulation_check_if_ready(self):
        """V6: đã schedule là phải chạy phân tích, trừ khi thiếu context thật sự."""
        ctx = dict(self.end_modulation_pending_context or {})
        if not ctx:
            print("End modulation V6: không có context để chạy.")
            return

        if self.end_modulation_running:
            print("End modulation V6: đang chạy, bỏ qua lần gọi trùng.")
            return

        video_id = str(ctx.get("video_id", "") or "").strip()
        url = str(ctx.get("url", "") or "").strip()
        key = str(ctx.get("key", "") or "").strip()
        scale = str(ctx.get("scale", "") or "").strip()

        if not video_id or not url or not key or not scale:
            print("End modulation V6: thiếu video/url/key/scale", ctx)
            return

        # V6: không chặn vì user_intervened, active tab mismatch, saved cache, hoặc tone_step +/-.
        # Mục tiêu hiện tại là bảo đảm ống tiến trình chạy và popup hiện khi có candidate_up.
        self.end_modulation_running = True
        self._set_progress_working("Đang phân tích lên tone cuối bài...")
        # P0.6 — Cập nhật status line để user thấy "🔄 Đang dò..." ngay
        try:
            self._update_end_modulation_status_line()
        except Exception:
            pass

        worker = threading.Thread(
            target=self._end_modulation_worker,
            args=(ctx,),
            daemon=True,
        )
        worker.start()

    def _end_modulation_worker(self, ctx: dict):
        try:
            result = self.controller.analyze_end_modulation_for_youtube(
                youtube_url=ctx.get("url", ""),
                title=ctx.get("title", ""),
                base_key=ctx.get("key", ""),
                base_scale=ctx.get("scale", ""),
            )
            result["base_result"] = ctx.get("base_result", {})
            self.end_modulation_result_signal.emit(result)
        except Exception as e:
            self.end_modulation_result_signal.emit({"error": str(e), "context": ctx})

    def _handle_end_modulation_result(self, result: dict):
        self.end_modulation_running = False
        # P0.6 — Refresh status line khi pipeline xong (loại bỏ "🔄 Đang dò...")
        try:
            self._update_end_modulation_status_line()
        except Exception:
            pass

        try:
            import json
            from pathlib import Path
            log_path = Path(BASE_DIR) / "data" / "end_modulation_last_result.json"
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with open(log_path, "w", encoding="utf-8") as f:
                json.dump(result, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

        if result.get("error"):
            self._reset_progress_idle()
            print("Lỗi phân tích lên tone cuối bài:", result.get("error"))
            return

        self.end_modulation_last_result = dict(result)
        self._set_progress_success("")

        has_mod = bool(result.get("has_modulation"))
        candidate_found = bool(result.get("candidate_found")) or bool(result.get("new_label"))

        if not has_mod and not candidate_found:
            print("End modulation: không có ứng viên đủ rõ.", result.get("message", ""))
            return

        # ===================================================================
        # V2 — AUTO SAVE workflow (NO popup):
        # User yêu cầu bỏ popup. Sau khi pipeline tìm thấy candidate:
        #   1. Tự động save vào cache (manual_confirmed=False vì chưa user verify)
        #   2. Set active_end_modulation → status line + 2 nút SỬA/HỦY xuất hiện
        #   3. Timer 700ms apply real-time tại raise_time_sec
        #   4. User có thể bấm SỬA MOD CUỐI hoặc HỦY LẦN NÀY bất kỳ lúc nào
        # Im lặng hoàn toàn — không popup, không silent toast.
        # ===================================================================
        save_result = result
        try:
            saved = self.controller.save_end_modulation_for_youtube(
                youtube_url=str(save_result.get("url", "") or ""),
                title=str(save_result.get("title", "") or ""),
                modulation_result=save_result,
                base_result=save_result.get("base_result", self._build_base_result_for_end_modulation()),
            )
            end_mod = saved.get("end_modulation", {}) if isinstance(saved, dict) else {}
            if isinstance(end_mod, dict) and end_mod:
                # IMPORTANT: tự động save thì manual_confirmed = False để pipeline
                # vẫn re-analyze nếu user FIX/SỬA tone gốc. Chỉ khi user bấm SỬA
                # MOD CUỐI và verify thì mới set manual_confirmed=True.
                end_mod["manual_confirmed"] = False
                end_mod["auto_saved"] = True
                self.active_end_modulation = end_mod
                self.end_modulation_applied_stage = "base"
                self._update_end_modulation_status_line()
                confidence_pct = int(round(float(result.get("confidence", 0.0) or 0.0) * 100))
                # Console log để dev theo dõi (không hiển thị popup cho user)
                print(
                    f"[Auto-save end mod] {end_mod.get('base_label', '')} → "
                    f"{end_mod.get('end_label', '')} @ {end_mod.get('raise_time_text', '')} "
                    f"| confidence {confidence_pct}% | algo {result.get('algorithm_version', '?')}"
                )
        except Exception as e:
            print("Lỗi auto-save end mod:", e)
        return

    # COMMON
    # =========================================================

    def _apply_theme(self):
        qss_path = BASE_DIR / "ui" / "theme.qss"
        if qss_path.exists():
            with open(qss_path, "r", encoding="utf-8") as f:
                self.setStyleSheet(f.read())

    def on_contact_clicked(self):
        QMessageBox.information(
            self,
            "LIÊN HỆ",
            "TRUYỀN HỮU MUSIC\n"
            "Hotline mua hàng : 0383457777\n"
            "Hỗ trợ kỹ thuật : 0372472870"
        )

    def _startup(self):
        try:
            self.controller.startup()
        except Exception as e:
            QMessageBox.critical(self, "Lỗi khởi động", str(e))

        def _check_version():
            try:
                info = self.controller.get_ytdlp_version_info()
            except Exception as e:
                info = {"error": str(e)}
            self.ytdlp_version_signal.emit(info)

        threading.Thread(target=_check_version, daemon=True).start()

    def _start_live_audio_after_consent(self):
        """Request live-audio consent once, then restore it on future runs."""
        if self._shutdown_started:
            return
        settings = self.controller.get_settings()
        if not settings.get("audio_onboarding_seen", False):
            dialog = AudioPermissionsDialog(self)
            if dialog.exec() != QDialog.DialogCode.Accepted:
                return
            settings["audio_onboarding_seen"] = True
            self.controller.settings_service.settings = settings
            self.controller.settings_service.save()
        self.controller.start_audio_visualizer(
            on_snapshot=lambda snapshot: self.audio_snapshot_signal.emit(snapshot),
            on_state=lambda state, message: self.audio_capture_state_signal.emit(state, message),
        )

    def _on_ytdlp_version_result(self, info: dict):
        if info.get("is_outdated"):
            cur = info.get("current", "?")
            lat = info.get("latest", "?")
            print(f"[yt-dlp] Phien ban cu: {cur} -> moi nhat: {lat}")
            self._ytdlp_warn_msg = f"yt-dlp cu ({cur} -> {lat}) — Settings > Bao tri de cap nhat"
            self._ytdlp_warn_until = time.time() + 60
        elif info.get("error"):
            print(f"[yt-dlp] Khong kiem tra duoc version: {info['error']}")

    def _load_default_singing_mode_on_startup(self):
        """
        Khi mở app luôn nạp chế độ NHẠC TRẺ thay vì để tất cả slider ở 50%.
        Hàm này chạy sau _startup(), nên trạng thái điều khiển đã sẵn sàng.
        """
        try:
            self.current_mode_button = self.btn_mode_tre
            self.lofi_on = False
            self.btn_mode_lofi.setChecked(False)

            for b in [self.btn_mode_tre, self.btn_mode_bolero, self.btn_mode_remix]:
                b.setChecked(b is self.btn_mode_tre)

            preset = self.controller.apply_mode("nhac_tre")

            # Set slider theo preset NHẠC TRẺ.
            # Các signal valueChanged vẫn chạy để label/UI và MIDI đồng bộ.
            self.slider_reverb_short.setValue(int(preset.get("reverb_short", 50)))
            self.slider_reverb_long.setValue(int(preset.get("reverb_long", 50)))
            self.slider_echo.setValue(int(preset.get("echo", 50)))
            self.slider_mic_vol.setValue(int(preset.get("mic_vol", 50)))
            self.slider_music_vol.setValue(int(preset.get("music_vol", 50)))

            if "tune" in preset:
                self.slider_tune.setValue(self._coerce_tune_value(preset.get("tune", 0)))

            self._refresh_mode_button_colors()

        except Exception as e:
            print("Không load được chế độ NHẠC TRẺ khi mở app:", e)

    def _ensure_youtube_browser(self):
        if self.youtube_browser is None:
            self.youtube_browser = YouTubeBrowserDialog(self.controller, self)
            self.youtube_browser.song_matched.connect(self.on_karaoke_song_matched)
            self.youtube_browser.video_changed.connect(self.on_karaoke_video_changed)
            self.youtube_browser.playback_state_changed.connect(self.on_karaoke_playback_state_changed)
            self.youtube_browser.show()
        return self.youtube_browser

    def _set_realtime_locked(self, locked: bool):
        self.realtime_locked_by_song = locked
        if locked:
            if self.realtime_running:
                try:
                    self.controller.stop_realtime()
                except Exception:
                    pass
                self.realtime_running = False
                self._analog_chassis.reset_input_level()
                self.header_waveform.reset()

    def start_realtime_input(self, device_index: int, device_mode: str):
        """Start one capture stream and route its measured RMS to the meter."""
        self.tone_is_resolved = False
        self.lbl_tone.setText("Đang dò tone")
        self.controller.start_realtime(
            device_index,
            device_mode,
            on_stable_tone=self.on_realtime_tone_detected,
            on_status=self.on_realtime_status,
            on_audio_level=self.on_realtime_level,
        )
        self.realtime_running = True

    def _reset_ui_mode_and_values(self):
        self.current_mode_button = None
        self.lofi_on = False
        self.btn_mode_lofi.setChecked(False)

        for b in [self.btn_mode_tre, self.btn_mode_bolero, self.btn_mode_remix]:
            b.setChecked(False)

        defaults = self.controller.get_default_ui_values()
        self.slider_reverb_short.setValue(defaults["reverb_short"])
        self.slider_reverb_long.setValue(defaults["reverb_long"])
        self.slider_echo.setValue(defaults["echo"])
        self.slider_mic_vol.setValue(defaults["mic_vol"])
        self.slider_music_vol.setValue(defaults["music_vol"])
        self.slider_tune.setValue(self._coerce_tune_value(defaults["tune"]))

        self._refresh_mode_button_colors()

    def _apply_song_to_ui(self, song: dict):
        key = song.get("key", "C")
        scale = song.get("scale", "Major")
        key = self.controller.normalize_key_for_scale(key, scale)

        # P1 — Invalidate audio cache khi đổi bài để tránh dùng cache cũ
        try:
            old_video_id = ""
            if isinstance(self.current_song_playing, dict):
                old_video_id = str(self.current_song_playing.get("video_id", "") or "").strip()
            new_video_id = str(song.get("video_id", "") or "").strip()
            if old_video_id and new_video_id and old_video_id != new_video_id:
                self.controller.invalidate_end_modulation_audio_cache()
        except Exception:
            pass

        self.current_song_playing = song
        self.current_song_id = str(song.get("id", "")).strip()

        self.base_tone_for_transpose = {
            "key": key,
            "scale": scale,
        }
        self._set_pitch_step_value(0, send_midi=True)

        self.current_detected_tone = {
            "key": key,
            "key_index": self.controller.key_name_to_index(key),
            "scale": scale,
            "label": f"{key} {scale}",
        }
        self.tone_is_resolved = True

        self._update_transposed_tone_label()
        self.controller.apply_manual_tone(key, scale)

        self.song_raise_done = False
        self.song_video_time_sec = 0.0
        self.song_raise_seconds = self._parse_raise_time_mmss(song.get("raise_time", "00:00"))
        self._update_song_status_line()

    def _start_song_workflow(self, song: dict):
        """
        Khi bấm HÁT trong danh sách bài hát:
        - Không mở YouTubeBrowserDialog trong app nữa.
        - Áp key/scale đã lưu vào trạng thái điều khiển StudioForge.
        - Mở video bằng Brave bên ngoài đúng chế độ auto detect.
        """

        self._set_progress_working("Đang mở bài hát bằng Brave...")
        self._apply_song_to_ui(song)
        self._set_realtime_locked(True)

        video_id = str(song.get("youtube_video_id", "")).strip()
        youtube_url = str(song.get("youtube_url", "")).strip()
        title = str(song.get("title", "") or "").strip()

        if video_id:
            youtube_url = f"https://www.youtube.com/watch?v={video_id}"
        elif youtube_url:
            try:
                parsed_id = self.controller.extract_video_id(youtube_url)
                if parsed_id:
                    video_id = parsed_id
                    youtube_url = f"https://www.youtube.com/watch?v={video_id}"
            except Exception:
                pass

        if not youtube_url:
            self._set_progress_success("Đã nạp key/scale.")
            self._update_end_modulation_status_line()
            return

        # Ghi nhận video hiện tại để nút SỬA TONE biết đang sửa bài nào.
        self.external_current_video = {
            "video_id": video_id,
            "url": youtube_url,
            "title": title,
        }

        # Vì bài trong danh sách đã có key/scale, không cần auto detect lại ngay video này.
        # Khi anh chuyển sang video khác trong Brave, auto detect vẫn chạy bình thường.
        if video_id:
            self.external_last_video_id = video_id
            self.external_detecting_video_id = ""

        try:
            self.controller.browser_launcher_service.open_brave_for_auto_detect(
                start_url=youtube_url
            )
            self._set_progress_success("Đã mở bằng Brave")
            self._update_end_modulation_status_line()
        except FileNotFoundError:
            self.controller.browser_launcher_service.open_brave_download_page()
            self._set_progress_success("Chưa cài Brave — đã mở trang tải chính thức")
        except Exception as e:
            self._reset_progress_idle()
            QMessageBox.critical(
                self,
                "Lỗi mở Brave",
                f"Không mở được bài hát bằng Brave:\n{e}"
            )

    # =========================================================
    # YOUTUBE
    # =========================================================

    def on_open_karaoke_clicked(self):
        """
        Nút KARAOKE dùng Brave làm trình duyệt hát chính.
        Web nội bộ YouTubeBrowserDialog được tạm khóa, không xóa code để sau này có thể khôi phục.
        """

        self.external_auto_enabled = True
        timer = getattr(self, "external_browser_timer", None)
        if timer is not None and not timer.isActive():
            timer.start(2000)
        try:
            if hasattr(self, "btn_auto_detect"):
                self.btn_auto_detect.setChecked(True)
                self.btn_auto_detect.setText("TỰ ĐỘNG\nON")
        except Exception:
            pass

        self._set_progress_working("Đang mở Karaoke bằng Brave...")

        try:
            self.controller.browser_launcher_service.open_brave_for_auto_detect(
                start_url="https://www.youtube.com"
            )
            self._set_progress_success("Đã mở Karaoke bằng Brave")
            self._update_end_modulation_status_line()
        except FileNotFoundError:
            self.controller.browser_launcher_service.open_brave_download_page()
            self._set_progress_success("Chưa cài Brave — đã mở trang tải chính thức")
        except Exception as e:
            self._reset_progress_idle()
            QMessageBox.critical(
                self,
                "Lỗi mở Brave",
                "Không mở được Brave Karaoke:\n" + str(e)
            )

        self.raise_()
        self.activateWindow()

    def on_karaoke_song_matched(self, song: dict):
        song_id = str(song.get("id", "")).strip()

        if self.current_song_id and song_id == self.current_song_id:
            return

        self._apply_song_to_ui(song)
        self._set_realtime_locked(True)
        self._set_progress_success(f"Tự nhận đúng bài đã lưu: {song.get('title', '')}")

    def on_karaoke_video_changed(self, video_id: str, title: str, url: str):
        if not video_id:
            return

        song = None
        if hasattr(self.controller, "find_song_by_video_id"):
            song = self.controller.find_song_by_video_id(video_id)

        if song:
            if not self.current_song_id or str(song.get("id", "")).strip() != self.current_song_id:
                self.on_karaoke_song_matched(song)
            return

        if self.realtime_locked_by_song:
            self._set_realtime_locked(False)
            self.current_song_playing = None
            self.current_song_id = None
            self._set_progress_success("")

        self._update_song_status_line()

    def on_karaoke_playback_state_changed(self, is_playing: bool, current_time_sec: float):
        """
        Luồng YouTube nội bộ cũ.

        Lỗi đã gặp: bài gốc G Minor, cuối bài A Minor, anh hạ nhạc -2.
        Dòng trạng thái tính đúng là cuối bài phải lên G Minor, nhưng hàm cũ lại gửi thẳng
        end_key_raw = A Minor và còn reset pitch về +0. Kết quả Auto-Tune nhảy sai A Minor.

        Quy tắc mới:
        - Không reset tone_step_value về 0 trong lúc bài đang phát.
        - Không gửi key/scale thô từ cache.
        - Luôn dùng _apply_runtime_tone_to_autotune(), tức là end_key/base_key + tone_step_value hiện tại.
        """
        if not self.current_song_playing:
            self._update_end_modulation_status_line()
            return

        self.song_video_time_sec = current_time_sec

        if is_playing:
            self._set_progress_success("")
        else:
            self.tone_progress.setRange(0, 100)
            self.tone_progress.setValue(0)
            self._sync_compact_progress()

        end_key_raw = self.current_song_playing.get("end_key", "--")
        end_scale = self.current_song_playing.get("end_scale", "--")

        if (
            self.song_raise_seconds > 0
            and not self.song_raise_done
            and current_time_sec >= self.song_raise_seconds
            and end_key_raw not in ("", "--")
            and end_scale not in ("", "--")
        ):
            try:
                # Use the same runtime transpose path as the saved end_modulation timer.
                # This prevents the old internal YouTube path from sending raw end_key.
                end_key = self.controller.normalize_key_for_scale(end_key_raw, end_scale)
                applied = self._apply_runtime_tone_to_autotune(end_key, end_scale, stage="end")
                if applied:
                    print(
                        f"YouTube nội bộ lên tone cuối bài RUNTIME: {applied.get('label')} "
                        f"tại {self._seconds_to_mmss(current_time_sec)} | Nhạc {self.tone_step_value:+d}"
                    )
                self.song_raise_done = True
                self._set_progress_success("")
            except Exception as e:
                print("Không tự đổi tone cuối bài trong YouTube nội bộ:", e)
                self.song_raise_done = True

        self._update_end_modulation_status_line()

    # =========================================================
    # REALTIME
    # =========================================================

    def on_realtime_status(self, text: str):
        self.realtime_status_signal.emit(text)

    def on_realtime_tone_detected(self, tone_result):
        self.realtime_tone_signal.emit(tone_result)

    def on_realtime_level(self, level: float):
        """Thread-safe callback supplied to the audio input service."""
        self.realtime_level_signal.emit(float(level))

    def _handle_realtime_status_on_ui(self, text: str):
        pass

    def _handle_realtime_level_on_ui(self, level: float):
        self._analog_chassis.set_input_level(level)
        if hasattr(self, "header_waveform"):
            self.header_waveform.set_level(level)

    def _handle_audio_snapshot_on_ui(self, snapshot):
        """Render the mixed mic/system-audio snapshot on the Qt thread."""
        import math
        rms = max(1e-6, (snapshot.rms_left + snapshot.rms_right) * .5)
        normalized = max(0.0, min(1.0, (20.0 * math.log10(rms) + 60.0) / 60.0))
        self._analog_chassis.set_input_level(normalized)
        if hasattr(self, "header_waveform"):
            self.header_waveform.set_level(normalized)
            self.header_waveform.set_waveform(snapshot.waveform)

    def _handle_audio_capture_state_on_ui(self, state, message: str):
        # Tone readout stays reserved for tone detection. The status is still
        # available to users and support without covering the performance UI.
        self._analog_chassis.setToolTip(message)
        self.setWindowTitle(f"THM Vocal Panel — {message}")

    @staticmethod
    def _coerce_tune_value(value) -> int:
        return coerce_tune_value(value)

    def _handle_realtime_tone_on_ui(self, tone_result):
        if self.realtime_locked_by_song:
            return

        try:
            self.controller.apply_realtime_tone(tone_result.key, tone_result.scale)

            display_key = self.controller.normalize_key_for_scale(
                tone_result.key,
                tone_result.scale,
            )

            self.base_tone_for_transpose = {
                "key": display_key,
                "scale": tone_result.scale,
            }
            self._set_pitch_step_value(0, send_midi=True)

            self.current_detected_tone = {
                "key": display_key,
                "key_index": self.controller.key_name_to_index(display_key),
                "scale": tone_result.scale,
                "label": f"{display_key} {tone_result.scale}",
            }
            self.tone_is_resolved = True

            self._update_transposed_tone_label()
        except Exception:
            pass

    # =========================================================
    # NORMAL ACTIONS
    # =========================================================

    def on_mic_selector_changed(self, mic_name: str):
        self.controller.set_active_mic(mic_name)
        self.lofi_on = False
        self._reset_ui_mode_and_values()

    def on_detect_tone_clicked(self):
        """
        Chạy Auto-Key ở thread nền để app không bị Not Responding.
        """
        if getattr(self, "autokey_running", False):
            return

        self.autokey_running = True
        self.tone_is_resolved = False
        self.lbl_tone.setText("Đang dò tone")
        try:
            self.btn_detect_tone.setEnabled(False)
            self.btn_detect_tone.setText("ĐANG\nDÒ")
        except Exception:
            pass

        self._set_progress_working("Đang chạy Auto-Key...")

        worker = threading.Thread(
            target=self._autokey_worker,
            daemon=True,
        )
        worker.start()

    def _autokey_worker(self):
        try:
            self.controller.run_autokey()
            self.autokey_finished_signal.emit({"ok": True})
        except Exception as e:
            self.autokey_finished_signal.emit({"ok": False, "error": str(e)})

    def _handle_autokey_finished(self, payload: dict):
        self.autokey_running = False
        self.fix_tone_running = False

        try:
            self.btn_detect_tone.setEnabled(True)
            self.btn_detect_tone.setText("DÒ\nAUTOKEY")
        except Exception:
            pass

        if payload.get("ok"):
            self._set_progress_success("")
            return

        self._reset_progress_idle()
        msg = str(payload.get("error", "") or "")
        if "SetCursorPos" in msg or "Access is denied" in msg or "Access denied" in msg:
            msg = (
                "Windows đang chặn Auto-Key điều khiển chuột.\n\n"
                "Cách xử lý:\n"
            "1. Tắt THM Vocal Panel và cửa sổ Auto-Key.\n"
            "2. Mở THM Vocal Panel bằng Run as administrator.\n"
            "3. Đảm bảo cửa sổ Auto-Key không bị minimize hoặc bị popup che."
            )

        QMessageBox.critical(self, "Lỗi Auto-Key", msg)

    def on_fix_tone_clicked(self):
        """
        FIX TONE: dò lại theo chế độ sửa sai, apply thử tone mới nhưng KHÔNG tự lưu cache.
        Nếu khách hát thấy đúng thì bấm SỬA TONE để lưu manual_verified=True.
        """
        if getattr(self, "fix_tone_running", False):
            return

        self.end_modulation_user_intervened = True
        self._cancel_end_modulation_check()

        ctx = self._get_active_youtube_context()
        youtube_url = ctx.get("url", "")
        youtube_title = ctx.get("title", "")

        if not youtube_url:
            QMessageBox.warning(
                self,
                "Chưa có bài YouTube",
                "App chưa nhận được video YouTube hiện tại từ Brave.\nAnh hãy mở một video YouTube trong Brave trước."
            )
            return

        self.fix_tone_running = True
        self.tone_is_resolved = False
        self.lbl_tone.setText("Đang dò tone")
        try:
            self.btn_fix_tone.setEnabled(False)
            self.btn_fix_tone.setText("ĐANG\nFIX")
        except Exception:
            pass

        self._set_progress_working("Đang FIX TONE...")

        current_key = self.base_tone_for_transpose.get("key", "")
        current_scale = self.base_tone_for_transpose.get("scale", "")

        worker = threading.Thread(
            target=self._fix_tone_worker,
            args=(youtube_url, youtube_title, current_key, current_scale),
            daemon=True,
        )
        worker.start()

    def _fix_tone_worker(self, youtube_url: str, youtube_title: str, current_key: str, current_scale: str):
        try:
            result = self.controller.fix_tone_for_youtube_url(
                youtube_url=youtube_url,
                title=youtube_title,
                current_key=current_key,
                current_scale=current_scale,
            )
            self.fix_tone_result_signal.emit(result)
        except Exception as e:
            self.fix_tone_result_signal.emit({"error": str(e)})

    def _handle_fix_tone_result(self, result: dict):
        self.fix_tone_running = False

        try:
            self.btn_fix_tone.setEnabled(True)
            self.btn_fix_tone.setText("FIX\nTONE")
        except Exception:
            pass

        if result.get("error"):
            self._reset_progress_idle()
            QMessageBox.critical(self, "Lỗi FIX TONE", str(result.get("error")))
            return

        key = str(result.get("key", "C"))
        scale = str(result.get("scale", "Major"))
        label = str(result.get("label", f"{key} {scale}"))

        self.base_tone_for_transpose = {
            "key": key,
            "scale": scale,
        }

        # Do NOT reset pitch here. It was reset when the new video was detected.
        # If the user already lowered/raised the song while detection was running,
        # preserving self.tone_step_value is required; otherwise auto-apply end tone will transpose wrongly.
        try:
            self._set_pitch_step_value(self.tone_step_value, send_midi=True)
        except Exception:
            pass

        self._sync_end_modulation_base_key(key, scale)
        self._apply_runtime_tone_to_autotune(key, scale, stage="base")
        self._set_progress_success("FIX TONE OK")
        self._sync_compact_controls()

        # FIX TONE ra tone mới: dùng tone này làm mốc đáng tin hơn để kiểm tra lên tone cuối bài.
        self._schedule_end_modulation_check(result, delay_ms=10000, reason="fix_tone")

        previous_label = str(result.get("previous_label", "") or "").strip()
        fix_method = str(result.get("fix_method", "") or "").strip()

        msg = f"Đã thử FIX TONE: {label}"
        if previous_label:
            msg += f"\nTone trước đó: {previous_label}"
        if fix_method:
            msg += f"\nCách fix: {fix_method}"
        msg += "\n\nNếu hát thử thấy đúng, hãy bấm SỬA TONE để lưu cache cho bài này."

        QMessageBox.information(self, "FIX TONE", msg)

    def on_settings_clicked(self):
        dlg = SettingsDialog(self.controller, self)
        dlg.exec()

    def on_save_song_clicked(self):
        """
        Lưu bài hát hiện tại.
        Bản sửa:
        - Luôn lấy đúng video YouTube hiện tại từ Brave, tránh lấy nhầm bài trước.
        - Prefill đầy đủ tiêu đề, link YouTube, key/scale hiện tại.
        - Nếu đã có dữ liệu lên tone cuối bài trong cache thì đưa vào popup.
        - Nếu anh nhập/sửa thời gian lên tone + key/scale cuối rồi bấm Lưu,
          app sẽ lưu đồng bộ vào songs.json + tone_cache/end_modulation,
          và cập nhật ngay dòng trạng thái dưới progress.
        """

        ctx = self._get_active_youtube_context()
        youtube_url = str(ctx.get("url", "") or "").strip()
        youtube_video_id = str(ctx.get("video_id", "") or "").strip()
        youtube_title = str(ctx.get("title", "") or "").strip()

        if not youtube_url or not youtube_video_id:
            self._silent_message(
                "Chưa có bài YouTube",
                "App chưa nhận được video YouTube hiện tại từ Brave.\n"
                "Anh hãy mở một video YouTube trong Brave trước khi bấm LƯU BÀI HÁT."
            )
            return

        # Luôn lấy dữ liệu end_modulation đúng video hiện tại để điền vào popup.
        saved_end = None
        try:
            saved_end = self.controller.find_saved_end_modulation_by_url(youtube_url)
        except Exception:
            saved_end = None

        if isinstance(saved_end, dict) and saved_end.get("enabled", True):
            self.active_end_modulation = saved_end

        # Key/scale gốc hiện tại của bài. Không lấy tone đã transpose.
        key = str(self.base_tone_for_transpose.get("key", "C") or "C").strip()
        scale = str(self.base_tone_for_transpose.get("scale", "Major") or "Major").strip()

        # Nếu cache tone của đúng video hiện tại có key/scale đã lưu, ưu tiên prefill bằng cache đó.
        try:
            cached = self.controller.find_cached_tone_by_video_id(youtube_video_id)
            if isinstance(cached, dict) and cached:
                key = str(cached.get("key", key) or key).strip()
                scale = str(cached.get("scale", scale) or scale).strip()
        except Exception:
            pass

        raise_time = "00:00"
        end_key = "--"
        end_scale = "--"

        if isinstance(saved_end, dict) and saved_end:
            raise_time = str(saved_end.get("raise_time_text", "") or "00:00").strip() or "00:00"
            end_key = str(saved_end.get("end_key", "") or saved_end.get("new_key", "") or "--").strip() or "--"
            end_scale = str(saved_end.get("end_scale", "") or saved_end.get("new_scale", "") or "--").strip() or "--"

        prefill = {
            "title": youtube_title or f"{key} {scale}",
            "key": key,
            "scale": scale,
            "tone_label": f"{key} {scale}",
            "raise_time": raise_time,
            "end_key": end_key,
            "end_scale": end_scale,
            "youtube_url": youtube_url,
            "youtube_video_id": youtube_video_id,
        }

        dlg = SongEditDialog(self.controller, self, prefill)
        if not dlg.exec():
            self._update_end_modulation_status_line()
            return

        data = dlg.get_data()

        # Ép lại đúng video hiện tại, không để popup giữ link cũ hoặc rỗng.
        data["youtube_video_id"] = youtube_video_id
        data["youtube_url"] = youtube_url
        data["title"] = str(data.get("title", "") or youtube_title or f"{key} {scale}").strip()

        self.controller.save_song(data)

        confirm_key = str(data.get("key", key) or key).strip()
        confirm_scale = str(data.get("scale", scale) or scale).strip()
        confirm_title = str(data.get("title", youtube_title) or youtube_title).strip()
        confirm_raise_time = str(data.get("raise_time", "00:00") or "00:00").strip() or "00:00"
        confirm_end_key = str(data.get("end_key", "--") or "--").strip()
        confirm_end_scale = str(data.get("end_scale", "--") or "--").strip()

        # Đồng bộ tone cache cho đúng video hiện tại.
        base_result = None
        try:
            base_result = self.controller.confirm_tone_cache_by_url(
                youtube_url=youtube_url,
                title=confirm_title,
                key_name=confirm_key,
                scale_name=confirm_scale,
            )
            fixed_key = str(base_result.get("key", confirm_key) or confirm_key).strip()
            fixed_scale = str(base_result.get("scale", confirm_scale) or confirm_scale).strip()
            fixed_label = str(base_result.get("label", f"{fixed_key} {fixed_scale}") or f"{fixed_key} {fixed_scale}").strip()

            self.base_tone_for_transpose = {
                "key": fixed_key,
                "scale": fixed_scale,
            }
            self._sync_end_modulation_base_key(fixed_key, fixed_scale)
            self._apply_runtime_tone_to_autotune(fixed_key, fixed_scale, stage="base")
        except Exception:
            base_result = {
                "key": confirm_key,
                "scale": confirm_scale,
                "label": f"{confirm_key} {confirm_scale}",
            }

        # Nếu có nhập thời gian lên tone + key/scale cuối thì lưu end_modulation vào cache.
        saved_end_mod = None
        has_end_time = self._mmss_to_seconds(confirm_raise_time) > 0
        has_end_tone = confirm_end_key not in ("", "--") and confirm_end_scale not in ("", "--")

        if has_end_time and has_end_tone:
            raise_sec = self._mmss_to_seconds(confirm_raise_time)
            apply_sec = max(0, raise_sec - 3)

            try:
                norm_end_key = self.controller.normalize_key_for_scale(confirm_end_key, confirm_end_scale)
            except Exception:
                norm_end_key = confirm_end_key

            fixed_key = str(base_result.get("key", confirm_key) or confirm_key).strip()
            fixed_scale = str(base_result.get("scale", confirm_scale) or confirm_scale).strip()
            fixed_label = str(base_result.get("label", f"{fixed_key} {fixed_scale}") or f"{fixed_key} {fixed_scale}").strip()

            modulation_result = {
                "ok": True,
                "manual_edited": True,
                "source": "manual_edit_from_save_song",
                "url": youtube_url,
                "title": confirm_title,
                "video_id": youtube_video_id,
                "base_key": fixed_key,
                "base_scale": fixed_scale,
                "base_label": fixed_label,
                "new_key": norm_end_key,
                "new_scale": confirm_end_scale,
                "new_label": f"{norm_end_key} {confirm_end_scale}",
                "end_key": norm_end_key,
                "end_scale": confirm_end_scale,
                "end_label": f"{norm_end_key} {confirm_end_scale}",
                "raise_time_sec": int(raise_sec),
                "raise_time_text": self._seconds_to_mmss(raise_sec),
                "apply_time_sec": int(apply_sec),
                "apply_time_text": self._seconds_to_mmss(apply_sec),
                "confidence": 1.0,
                "enabled": True,
            }

            try:
                saved = self.controller.save_end_modulation_for_youtube(
                    youtube_url=youtube_url,
                    title=confirm_title,
                    modulation_result=modulation_result,
                    base_result=base_result,
                )
                saved_end_mod = saved.get("end_modulation", {}) if isinstance(saved, dict) else {}
                if isinstance(saved_end_mod, dict) and saved_end_mod:
                    self.active_end_modulation = saved_end_mod
                    self.end_modulation_applied_stage = "base"
            except Exception as e:
                self._silent_message("Lỗi lưu lên tone cuối", str(e))

        else:
            # Không có dữ liệu lên tone cuối thì vẫn load lại cache hiện có nếu có.
            try:
                self._load_saved_end_modulation_for_current_video(youtube_url)
            except Exception:
                pass

        self._update_end_modulation_status_line()

        msg = "Đã lưu bài hát hiện tại."
        msg += f"\nTên bài: {data.get('title', '')}"
        msg += f"\nTone gốc: {base_result.get('label', f'{confirm_key} {confirm_scale}')}"
        if isinstance(saved_end_mod, dict) and saved_end_mod:
            msg += (
                "\n\nĐã cập nhật thông tin lên tone cuối bài:"
                f"\nThời gian lên tone: {saved_end_mod.get('raise_time_text', confirm_raise_time)}"
                f"\nTone sẽ lên: {saved_end_mod.get('end_label', saved_end_mod.get('new_label', ''))}"
            )
        else:
            msg += "\n\nChưa cập nhật thông tin lên tone cuối bài."

        self._silent_message("Đã lưu", msg)

    def _get_active_youtube_context(self):
        """
        Lấy thông tin YouTube đang hoạt động.
        Ưu tiên đọc trực tiếp từ Brave ở thời điểm hiện tại để tránh nút SỬA TONE lấy nhầm bài trước.
        Nếu Brave chưa trả dữ liệu mới thì mới fallback về context đã lưu.
        """

        # 1) Ưu tiên hỏi Brave trực tiếp ngay lúc bấm nút.
        try:
            live_video = self.controller.get_current_external_youtube_video()
            if isinstance(live_video, dict):
                live_video_id = str(live_video.get("video_id", "") or "").strip()
                live_url = str(live_video.get("url", "") or "").strip()
                live_title = str(live_video.get("title", "") or "").strip()
                if live_video_id and live_url:
                    self.external_current_video = {
                        "video_id": live_video_id,
                        "url": live_url,
                        "title": live_title,
                    }
                    return {
                        "video_id": live_video_id,
                        "url": live_url,
                        "title": live_title,
                    }
        except Exception:
            pass

        # 2) Fallback về video mà auto detect gần nhất đã ghi nhận.
        video_id = str(self.external_current_video.get("video_id", "") or "").strip()
        url = str(self.external_current_video.get("url", "") or "").strip()
        title = str(self.external_current_video.get("title", "") or "").strip()

        if video_id and url:
            return {
                "video_id": video_id,
                "url": url,
                "title": title,
            }

        # 3) Fallback cuối cho web nội bộ nếu còn dùng lại sau này.
        if self.youtube_browser:
            video_id = self.youtube_browser.current_video_id or ""
            title = self.youtube_browser.current_title or ""
            if video_id:
                url = self.youtube_browser.canonical_watch_url(video_id)
                return {
                    "video_id": video_id,
                    "url": url,
                    "title": title,
                }

        return {
            "video_id": "",
            "url": "",
            "title": "",
        }

    def on_toggle_auto_detect_clicked(self):
        self.external_auto_enabled = self.btn_auto_detect.isChecked()

        timer = getattr(self, "external_browser_timer", None)
        if timer is not None:
            if self.external_auto_enabled and not timer.isActive():
                timer.start(2000)
            elif not self.external_auto_enabled and timer.isActive():
                timer.stop()

        if self.external_auto_enabled:
            self.btn_auto_detect.setText("TỰ ĐỘNG\nON")
        else:
            self.btn_auto_detect.setText("TỰ ĐỘNG\nOFF")
        self.btn_auto_detect.setStyleSheet("")

        self._update_end_modulation_status_line()

    def on_confirm_tone_clicked(self):
        """
        Xác nhận/sửa tone đúng cho video YouTube hiện tại.
        Đồng thời cho phép cập nhật thời gian lên tone cuối bài trong cùng popup.
        """

        ctx = self._get_active_youtube_context()
        youtube_url = str(ctx.get("url", "") or "").strip()
        youtube_video_id = str(ctx.get("video_id", "") or "").strip()
        youtube_title = str(ctx.get("title", "") or "").strip()

        if not youtube_url or not youtube_video_id:
            self._silent_message(
                "Chưa có bài YouTube",
                "App chưa nhận được video YouTube hiện tại từ Brave.\nAnh hãy mở một video YouTube trong Brave trước."
            )
            return

        self.end_modulation_user_intervened = True
        self._cancel_end_modulation_check()

        # Luôn load dữ liệu lên tone cuối của đúng video hiện tại trước khi mở popup sửa.
        saved_end = None
        try:
            saved_end = self.controller.find_saved_end_modulation_by_url(youtube_url)
        except Exception:
            saved_end = None
        if isinstance(saved_end, dict) and saved_end.get("enabled", True):
            self.active_end_modulation = saved_end

        key = self.base_tone_for_transpose.get("key", "C")
        scale = self.base_tone_for_transpose.get("scale", "Major")

        # Nếu cache tone của video hiện tại có manual_verified, ưu tiên lấy key/scale đó làm prefill.
        try:
            cached = self.controller.find_cached_tone_by_video_id(youtube_video_id)
            if isinstance(cached, dict) and cached:
                key = str(cached.get("key", key) or key).strip()
                scale = str(cached.get("scale", scale) or scale).strip()
        except Exception:
            pass

        raise_time = "00:00"
        end_key = "--"
        end_scale = "--"
        if isinstance(saved_end, dict) and saved_end:
            raise_time = str(saved_end.get("raise_time_text", "") or "00:00").strip() or "00:00"
            end_key = str(saved_end.get("end_key", "") or saved_end.get("new_key", "") or "--").strip() or "--"
            end_scale = str(saved_end.get("end_scale", "") or saved_end.get("new_scale", "") or "--").strip() or "--"

        prefill = {
            "title": youtube_title or self.current_detected_tone.get("label", ""),
            "key": key,
            "scale": scale,
            "tone_label": f"{key} {scale}",
            "raise_time": raise_time,
            "end_key": end_key,
            "end_scale": end_scale,
            "youtube_url": youtube_url,
            "youtube_video_id": youtube_video_id,
        }

        dlg = SongEditDialog(self.controller, self, prefill)

        if not dlg.exec():
            self._update_end_modulation_status_line()
            return

        data = dlg.get_data()
        confirm_url = str(data.get("youtube_url", youtube_url) or youtube_url).strip()
        confirm_title = str(data.get("title", youtube_title) or youtube_title).strip()
        confirm_key = str(data.get("key", key) or key).strip()
        confirm_scale = str(data.get("scale", scale) or scale).strip()
        confirm_raise_time = str(data.get("raise_time", "00:00") or "00:00").strip() or "00:00"
        confirm_end_key = str(data.get("end_key", "--") or "--").strip()
        confirm_end_scale = str(data.get("end_scale", "--") or "--").strip()

        try:
            result = self.controller.confirm_tone_cache_by_url(
                youtube_url=confirm_url,
                title=confirm_title,
                key_name=confirm_key,
                scale_name=confirm_scale,
            )

            fixed_key = result["key"]
            fixed_scale = result["scale"]
            fixed_label = result["label"]

            self.base_tone_for_transpose = {
                "key": fixed_key,
                "scale": fixed_scale,
            }

            # Khi người dùng xác nhận tone gốc, đưa pitch nhạc nội bộ về 0.
            self._set_pitch_step_value(0, send_midi=True)

            self._sync_end_modulation_base_key(fixed_key, fixed_scale)
            self._apply_runtime_tone_to_autotune(fixed_key, fixed_scale, stage="base")
            self._set_progress_success("Đã xác nhận tone")

            # Nếu người dùng nhập thời gian lên tone + key/scale cuối, lưu ngay vào cache end_modulation.
            saved_end_mod = None
            has_end_time = self._mmss_to_seconds(confirm_raise_time) > 0
            has_end_tone = confirm_end_key not in ("", "--") and confirm_end_scale not in ("", "--")

            if has_end_time and has_end_tone:
                raise_sec = self._mmss_to_seconds(confirm_raise_time)
                apply_sec = max(0, raise_sec - 3)
                try:
                    norm_end_key = self.controller.normalize_key_for_scale(confirm_end_key, confirm_end_scale)
                except Exception:
                    norm_end_key = confirm_end_key

                modulation_result = {
                    "ok": True,
                    "manual_edited": True,
                    "source": "manual_edit_from_sua_tone",
                    "url": confirm_url,
                    "title": confirm_title,
                    "video_id": youtube_video_id,
                    "base_key": fixed_key,
                    "base_scale": fixed_scale,
                    "base_label": fixed_label,
                    "new_key": norm_end_key,
                    "new_scale": confirm_end_scale,
                    "new_label": f"{norm_end_key} {confirm_end_scale}",
                    "end_key": norm_end_key,
                    "end_scale": confirm_end_scale,
                    "end_label": f"{norm_end_key} {confirm_end_scale}",
                    "raise_time_sec": int(raise_sec),
                    "raise_time_text": self._seconds_to_mmss(raise_sec),
                    "apply_time_sec": int(apply_sec),
                    "apply_time_text": self._seconds_to_mmss(apply_sec),
                    "confidence": 1.0,
                    "enabled": True,
                }

                saved = self.controller.save_end_modulation_for_youtube(
                    youtube_url=confirm_url,
                    title=confirm_title,
                    modulation_result=modulation_result,
                    base_result=result,
                )
                saved_end_mod = saved.get("end_modulation", {}) if isinstance(saved, dict) else {}
                if isinstance(saved_end_mod, dict) and saved_end_mod:
                    self.active_end_modulation = saved_end_mod
                    self.end_modulation_applied_stage = "base"
            else:
                # Không có thời gian/tone cuối thì vẫn giữ dữ liệu end_modulation cũ nếu có.
                try:
                    self._load_saved_end_modulation_for_current_video(confirm_url)
                except Exception:
                    pass

            self._update_end_modulation_status_line()

            msg = f"Đã ghi nhớ tone đúng cho bài này:\n{fixed_label}\n"
            if isinstance(saved_end_mod, dict) and saved_end_mod:
                msg += (
                    "\nĐã cập nhật thông tin lên tone cuối bài:\n"
                    f"Thời gian lên tone: {saved_end_mod.get('raise_time_text', confirm_raise_time)}\n"
                    f"Tone sẽ lên: {saved_end_mod.get('end_label', saved_end_mod.get('new_label', ''))}"
                )
            else:
                msg += "\nChưa cập nhật thời gian lên tone cuối bài."

            self._silent_message("Đã lưu", msg)

            # Nếu chưa nhập thông tin lên tone cuối, vẫn cho app phân tích sau đó.
            if not (has_end_time and has_end_tone):
                self._schedule_end_modulation_check(result, delay_ms=10000, reason="manual_verified")

        except Exception as e:
            self._silent_message("Lỗi xác nhận tone", str(e))

    def on_song_list_clicked(self):
        dlg = SongManagerDialog(self.controller, self._start_song_workflow, self)
        dlg.exec()

    def on_toggle_vang(self):
        self.vang_on = not self.vang_on
        self.controller.toggle_vang(self.vang_on)

        if self.vang_on:
            self.btn_vang.setText("VANG\nON")
            self.btn_vang.setObjectName("MonitorPrimaryButton")
        else:
            self.btn_vang.setText("VANG\nOFF")
            self.btn_vang.setObjectName("SystemDangerButton")

        self.btn_vang.style().unpolish(self.btn_vang)
        self.btn_vang.style().polish(self.btn_vang)
        self.btn_vang.update()
        self._sync_compact_controls()

    def on_toggle_mic(self):
        self.mic_on = not self.mic_on
        self.controller.toggle_mic(self.mic_on)

        if self.mic_on:
            self.btn_mic.setText("MIC\nON")
            self.btn_mic.setObjectName("MonitorPrimaryButton")
        else:
            self.btn_mic.setText("MIC\nOFF")
            self.btn_mic.setObjectName("SystemDangerButton")

        self.btn_mic.style().unpolish(self.btn_mic)
        self.btn_mic.style().polish(self.btn_mic)
        self.btn_mic.update()
        self._sync_compact_controls()

    def on_lofi_clicked(self):
        self.lofi_on = self.controller.toggle_lofi()
        self.btn_mode_lofi.setChecked(self.lofi_on)
        self._refresh_mode_button_colors()

    def on_tone_minus_clicked(self):
        self._set_pitch_step_value(self.tone_step_value - 1, send_midi=True)
        self._update_transposed_tone_label()
        try:
            self.controller.apply_manual_tone(
                self.current_detected_tone.get("key", "C"),
                self.current_detected_tone.get("scale", "Major"),
            )
        except Exception as e:
            print("Không apply key/scale sau khi giảm tone:", e)
        self._schedule_pitch_transpose_refresh()

    def on_tone_plus_clicked(self):
        self._set_pitch_step_value(self.tone_step_value + 1, send_midi=True)
        self._update_transposed_tone_label()
        try:
            self.controller.apply_manual_tone(
                self.current_detected_tone.get("key", "C"),
                self.current_detected_tone.get("scale", "Major"),
            )
        except Exception as e:
            print("Không apply key/scale sau khi tăng tone:", e)
        self._schedule_pitch_transpose_refresh()

    def on_reverb_short_changed(self, value: int):
        self.lbl_reverb_short.setText(str(value))
        self._set_knob_readout_value("slider_reverb_short", value)
        if self._set_performance_macro_value(0, value):
            return
        self._queue_control_backend("on_reverb_short_changed", value)

    def on_reverb_long_changed(self, value: int):
        self.lbl_reverb_long.setText(str(value))
        self._set_knob_readout_value("slider_reverb_long", value)
        if self._set_performance_macro_value(1, value):
            return
        self._queue_control_backend("on_reverb_long_changed", value)

    def on_echo_changed(self, value: int):
        self.lbl_echo.setText(str(value))
        self._set_knob_readout_value("slider_echo", value)
        if self._set_performance_macro_value(2, value):
            return
        self._queue_control_backend("on_echo_changed", value)

    def on_mic_vol_changed(self, value: int):
        self.lbl_mic_vol.setText(str(value))
        self._set_knob_readout_value("slider_mic_vol", value)
        self._queue_control_backend("on_mic_vol_changed", value)

    def on_music_vol_changed(self, value: int):
        self.lbl_music_vol.setText(str(value))
        self._set_knob_readout_value("slider_music_vol", value)
        self._queue_control_backend("on_music_vol_changed", value)
        self._sync_compact_controls()

    def on_tune_changed(self, raw_slider_value: int):
        value = max(-50, min(50, int(raw_slider_value)))
        self.lbl_tune.setText(str(value))
        self._set_knob_readout_value("slider_tune", value)
        self._queue_control_backend("on_tune_changed", value)

    def _set_knob_readout_value(self, slider_name: str, value: int):
        """Update only the small value field; the panel itself is static."""
        readout = getattr(self, "_knob_value_readouts", {}).get(slider_name)
        if readout is not None:
            readout.set_value(value)

    def on_mode_clicked(self, mode_name: str, btn: QPushButton):
        self.current_mode_button = btn

        for b in [self.btn_mode_tre, self.btn_mode_bolero, self.btn_mode_remix]:
            b.setChecked(b is btn)

        preset = self.controller.apply_mode(mode_name)

        self.slider_reverb_short.setValue(preset["reverb_short"])
        self.slider_reverb_long.setValue(preset["reverb_long"])
        self.slider_echo.setValue(preset["echo"])
        self.slider_mic_vol.setValue(preset["mic_vol"])
        self.slider_music_vol.setValue(preset["music_vol"])
        self.slider_tune.setValue(self._coerce_tune_value(preset["tune"]))
        self._flush_all_control_backends()

        self._refresh_mode_button_colors()

    # =========================================================
    # EXTERNAL BROWSER AUTO DETECT
    # =========================================================

    def _start_external_browser_auto_detect(self):
        """
        Tự động đọc URL YouTube từ Brave bên ngoài.
        Brave phải được mở bởi app với --remote-debugging-port=9222.
        """

        self.external_browser_timer = QTimer(self)
        self.external_browser_timer.timeout.connect(self._check_external_browser_youtube)
        # Polling starts only after Karaoke/auto-detect is explicitly enabled.

        self.external_browser_status_signal.emit("Auto browser detect đã bật.")

    def _check_external_browser_youtube(self):
        if not self.external_auto_enabled:
            return

        if self.external_detect_running:
            return

        try:
            video = self.controller.get_current_external_youtube_video()
        except Exception:
            return

        if not video:
            return

        video_id = str(video.get("video_id", "") or "").strip()
        url = str(video.get("url", "") or "").strip()
        title = str(video.get("title", "") or "").strip()

        if not video_id or not url:
            return

        if video_id == self.external_last_video_id:
            return

        if video_id == self.external_detecting_video_id:
            return

        self.external_detecting_video_id = video_id
        self.external_detect_running = True
        self.end_modulation_user_intervened = False
        self._cancel_end_modulation_check()

        # New video: reset pitch immediately (UI + CC36) so old song shift is not carried over.
        # Important: do this at video-change time, NOT when detect result returns.
        # If the user presses +/- while detection is still running, the result handler must preserve it.
        try:
            self._set_pitch_step_value(0, send_midi=True)
        except Exception:
            pass

        self._set_progress_working("Đang xử lý bài mới từ Brave...")
        self.external_browser_status_signal.emit(f"Phát hiện bài mới: {title or video_id}")

        worker = threading.Thread(
            target=self._external_detect_worker,
            args=(video_id, url, title),
            daemon=True,
        )
        worker.start()

    def _external_detect_worker(self, video_id: str, url: str, title: str):
        try:
            result = self.controller.auto_resolve_tone_for_youtube(
                youtube_url=url,
                title=title,
            )

            result["video_id"] = video_id
            result["url"] = url
            result["title"] = title or result.get("title", "")

            self.external_browser_tone_signal.emit(result)

        except Exception as e:
            self.external_browser_tone_signal.emit({
                "error": str(e),
                "video_id": video_id,
                "url": url,
                "title": title,
            })

    def _handle_external_browser_status(self, message: str):
        # Không hiển thị dòng trạng thái dưới progress để giữ layout gọn và cố định.
        pass

    def _handle_external_browser_tone_result(self, result: dict):
        self.external_detect_running = False

        video_id = str(result.get("video_id", "") or "").strip()
        if video_id:
            self.external_last_video_id = video_id

        self.external_current_video = {
            "video_id": video_id,
            "url": str(result.get("url", "") or "").strip(),
            "title": str(result.get("title", "") or "").strip(),
        }
        self.external_video_seen_at = time.time()
        self._load_saved_end_modulation_for_current_video(self.external_current_video.get("url", ""))

        self.external_detecting_video_id = ""

        if result.get("error"):
            self._reset_progress_idle()
            self.lbl_tone_status.setText(f"Lỗi auto detect: {result.get('error')}")
            return

        key = str(result.get("key", "C"))
        scale = str(result.get("scale", "Major"))
        label = str(result.get("label", f"{key} {scale}"))
        status = str(result.get("status", ""))

        self.base_tone_for_transpose = {
            "key": key,
            "scale": scale,
        }

        self._set_pitch_step_value(0, send_midi=True)

        self._sync_end_modulation_base_key(key, scale)
        self._apply_runtime_tone_to_autotune(key, scale, stage="base")

        self._set_progress_success("Auto tone OK")

        self._update_end_modulation_status_line()

        # Nếu bài đã có dữ liệu lên tone cuối, không hiện popup nữa.
        # Chỉ hiển thị dòng trạng thái và để bộ auto-apply tự đổi key/scale đúng thời gian đã lưu.
        if isinstance(self.active_end_modulation, dict) and self.active_end_modulation.get("enabled", True):
            self._update_end_modulation_status_line()
        else:
            # Bài chưa có dữ liệu lên tone cuối thì mới phân tích và hỏi LƯU / SỬA / BỎ QUA.
            self._schedule_end_modulation_check(result, delay_ms=20000, reason="auto_detect")

    def shutdown_services(self):
        """Stop owned services exactly once before the Qt event loop exits."""
        if self._shutdown_started:
            return

        self._shutdown_started = True
        try:
            self.controller.shutdown()
        except Exception:
            pass

    def request_application_exit(self):
        """Quit Tune-AI, rather than merely hiding its window on macOS."""
        application = QApplication.instance()
        if application is not None:
            # QApplication owns closing all top-level windows. Calling both
            # self.close() and application.quit() used to re-enter closeEvent
            # during macOS termination and PyQt aborts on any callback error.
            application.quit()
        else:
            self.close()

    def closeEvent(self, event):
        self.shutdown_services()

        if self.compact_window is not None:
            try:
                self.compact_window.close()
            except RuntimeError:
                # A native close can delete the mini controller before its
                # Python wrapper observes the final event.
                pass
            self.compact_window = None

        event.accept()
