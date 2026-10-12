"""Pure formatting and pitch helpers shared by the tone UI.

These functions intentionally have no Qt or controller dependencies. Keeping
them outside ``MainWindow`` makes the window responsible for orchestration,
while preserving the existing method facade for older callers.
"""


def seconds_to_mmss(total_seconds: int) -> str:
    total_seconds = max(0, int(total_seconds))
    return f"{total_seconds // 60:02d}:{total_seconds % 60:02d}"


def parse_mmss(text: str, allow_hours: bool = False) -> int:
    value = str(text or "").strip()
    if not value:
        return 0

    try:
        parts = value.split(":")
        if len(parts) == 2:
            return max(0, int(parts[0]) * 60 + int(parts[1]))
        if allow_hours and len(parts) == 3:
            return max(0, int(parts[0]) * 3600 + int(parts[1]) * 60 + int(parts[2]))
        if not allow_hours and len(parts) != 2:
            return 0
        return max(0, int(float(value)))
    except (TypeError, ValueError):
        return 0


def pitch_class_for_key(key: str, key_to_pitch_class: dict[str, int]) -> int:
    return key_to_pitch_class.get(str(key).strip(), 0)


def key_for_pitch_class(pitch_class: int, scale: str,
                        major_keys: dict[int, str], minor_keys: dict[int, str]) -> str:
    keys = minor_keys if str(scale) == "Minor" else major_keys
    return keys[pitch_class % 12]


def coerce_tune_value(value) -> int:
    """Read legacy 0..100 values while exposing -50..+50 in the UI."""
    value = int(value)
    if value < -50 or value > 50:
        value -= 50
    return max(-50, min(50, value))
