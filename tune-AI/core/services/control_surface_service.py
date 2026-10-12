"""State and MIDI mapping for the physical vocal-panel controls."""

from core.constants import DEFAULT_UI_VALUES, MIC1_CC, MIC2_CC, MODE_PRESETS


class ControlSurfaceService:
    """Own the mutable control-surface state independently of UI workflows."""

    def __init__(self, midi, mode_settings_service):
        self.midi = midi
        self.mode_settings_service = mode_settings_service
        self.current_mic_name = "MIC 1"
        self.current_cc = MIC1_CC
        self.current_mode = None
        self.lofi_on = False
        self.global_pitch = 0

    @staticmethod
    def _pct_to_cc(value_0_100: int) -> int:
        value_0_100 = max(0, min(100, int(value_0_100)))
        return int(round(value_0_100 * 127 / 100))

    def set_active_mic(self, mic_name: str):
        self.current_mic_name = mic_name
        self.current_cc = MIC2_CC if mic_name == "MIC 2" else MIC1_CC
        self.current_mode = None
        self.lofi_on = False
        self.midi.send_cc(self.current_cc["lofi"], 0)

    def toggle_vang(self, is_on: bool):
        value = 127 if is_on else 0
        for cc in self.current_cc["vang_group"]:
            self.midi.send_cc(cc, value)

    def toggle_mic(self, is_on: bool):
        self.midi.send_cc(self.current_cc["mic_toggle"], 127 if is_on else 0)

    def toggle_lofi(self) -> bool:
        self.lofi_on = not self.lofi_on
        self.set_lofi(self.lofi_on)
        return self.lofi_on

    def set_lofi(self, is_on: bool):
        self.lofi_on = bool(is_on)
        self.midi.send_cc(self.current_cc["lofi"], 127 if self.lofi_on else 0)

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
        self.midi.send_cc(self.current_cc["tune"], self._pct_to_cc(raw_position_0_100))

    def set_tune(self, tune_value: int):
        self.set_tune_from_slider_position(max(-50, min(50, int(tune_value))) + 50)

    def set_global_pitch(self, semitone: int) -> dict:
        semitone = max(-12, min(12, int(semitone)))
        self.global_pitch = semitone
        cc_value = int(round((semitone + 12) * 127 / 24))
        self.midi.send_cc(36, max(0, min(127, cc_value)))
        return {"semitone": semitone, "cc_value": cc_value, "control": "internal_pitch"}

    def apply_mode(self, mode_name: str) -> dict:
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

    @staticmethod
    def default_ui_values() -> dict:
        return DEFAULT_UI_VALUES.copy()
