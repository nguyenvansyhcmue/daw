from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = BASE_DIR / "data"
ASSETS_DIR = BASE_DIR / "assets"

DATA_DIR.mkdir(exist_ok=True)
ASSETS_DIR.mkdir(exist_ok=True)

APP_TITLE = "THM Vocal Panel"
MIDI_PORT_HINT = "PythonToCubase"

# =========================================================
# CC MAP - MIC 1
# =========================================================
MIC1_CC = {
    "reverb_short": 85,
    "reverb_long": 32,
    "echo": 33,
    "mic_vol": 35,
    "music_vol": 34,
    "tune": 54,
    "pitch": 36,
    "vang_group": [51, 52, 53],
    "mic_toggle": 29,
    "lofi": 27,
    "at_key": 40,
    "at_scale": 41,
}

# =========================================================
# CC MAP - MIC 2
# =========================================================
MIC2_CC = {
    "reverb_short": 95,
    "reverb_long": 96,
    "echo": 97,
    "mic_vol": 98,
    "music_vol": 99,
    "tune": 100,
    "pitch": 101,
    "vang_group": [61, 62, 63],
    "mic_toggle": 64,
    "lofi": 65,
    "at_key": 66,
    "at_scale": 67,
}

# =========================================================
# MODE PRESETS
# LOFI không dùng preset
# Chỉ NHẠC TRẺ / BOLERO / REMIX dùng preset
# Slider thường: 0..100
# Tune: giá trị hiển thị theo thang Antares 400..0
# =========================================================
MODE_PRESETS = {
    "nhac_tre": {
        "reverb_short": 48,
        "reverb_long": 42,
        "echo": 40,
        "mic_vol": 82,
        "music_vol": 76,
        "tune": 180,
    },
    "bolero": {
        "reverb_short": 55,
        "reverb_long": 52,
        "echo": 26,
        "mic_vol": 84,
        "music_vol": 65,
        "tune": 160,
    },
    "remix": {
        "reverb_short": 44,
        "reverb_long": 30,
        "echo": 58,
        "mic_vol": 88,
        "music_vol": 85,
        "tune": 140,
    },
}

DEFAULT_UI_VALUES = {
    "reverb_short": 50,
    "reverb_long": 50,
    "echo": 50,
    "mic_vol": 50,
    "music_vol": 50,
    "tune": 50,
}