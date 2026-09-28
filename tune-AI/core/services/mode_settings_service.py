import json
from copy import deepcopy
from core.constants import DATA_DIR, MODE_PRESETS


class ModeSettingsService:
    def __init__(self):
        self.file_path = DATA_DIR / "mode_settings.json"
        self.file_path.parent.mkdir(parents=True, exist_ok=True)

        if not self.file_path.exists():
            self._write_json(deepcopy(MODE_PRESETS))

    def _read_json(self):
        try:
            with open(self.file_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return deepcopy(MODE_PRESETS)

    def _write_json(self, data):
        with open(self.file_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def get_all_modes(self):
        data = self._read_json()
        for mode_name, defaults in MODE_PRESETS.items():
            if mode_name not in data:
                data[mode_name] = deepcopy(defaults)
        return data

    def get_mode(self, mode_name: str):
        data = self.get_all_modes()
        if mode_name not in data:
            data[mode_name] = deepcopy(MODE_PRESETS.get(mode_name, {}))
            self._write_json(data)
        return data[mode_name]

    def update_mode_value(self, mode_name: str, field: str, value):
        data = self.get_all_modes()
        if mode_name not in data:
            data[mode_name] = deepcopy(MODE_PRESETS.get(mode_name, {}))
        data[mode_name][field] = value
        self._write_json(data)

    def set_mode_values(self, mode_name: str, values: dict):
        data = self.get_all_modes()
        data[mode_name] = values
        self._write_json(data)