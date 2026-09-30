import json
from pathlib import Path


class SettingsService:
    def __init__(self, path=None):
        if path is None:
            # lưu cùng thư mục data của project
            self.path = Path(__file__).resolve().parents[2] / "data" / "app_settings.json"
        else:
            self.path = Path(path)

        self.settings = {
            "ytdlp_cookies_file": "",
        }

    def load(self):
        if self.path.exists():
            try:
                with open(self.path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, dict):
                    self.settings.update(data)
                    # Remove obsolete Cubase launch/close settings on first
                    # save so an old installation cannot retain that intent.
                    for key in ("cubase_project_path", "start_cubase_with_app", "close_cubase_on_exit"):
                        self.settings.pop(key, None)
            except Exception:
                pass
        return self.settings

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "w", encoding="utf-8") as f:
            json.dump(self.settings, f, ensure_ascii=False, indent=2)

    def get(self, key, default=None):
        return self.settings.get(key, default)

    def set(self, key, value):
        self.settings[key] = value
        self.save()
