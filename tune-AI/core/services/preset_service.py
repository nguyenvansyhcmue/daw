import json
from dataclasses import asdict
from core.constants import PRESETS_JSON_PATH
from core.models import ModePreset


class PresetService:
    DEFAULT_MODES = ("nhac_tre", "remix", "bolero")

    def __init__(self, path=PRESETS_JSON_PATH):
        self.path = path
        self.presets: dict[str, ModePreset] = {}

    def load(self):
        if self.path.exists():
            with open(self.path, "r", encoding="utf-8") as f:
                raw = json.load(f)
            self.presets = {k: ModePreset(**v) for k, v in raw.items()}

        for mode in self.DEFAULT_MODES:
            self.presets.setdefault(mode, ModePreset())

        return self.presets

    def save(self):
        self.path.parent.mkdir(exist_ok=True)
        data = {k: asdict(v) for k, v in self.presets.items()}
        with open(self.path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def get(self, mode_name: str) -> ModePreset:
        return self.presets.setdefault(mode_name, ModePreset())

    def update_field(self, mode_name: str, field: str, value: int):
        preset = self.get(mode_name)
        setattr(preset, field, int(value))
        self.save()