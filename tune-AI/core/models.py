from dataclasses import dataclass
from typing import Optional


@dataclass
class SongModel:
    name: str
    tone: str
    youtube: str = ""
    youtube_id: Optional[str] = None
    thumbnail: str = ""
    time: str = ""
    mod_delta: str = ""
    song_shift_tone: str = ""
    chorus_scale: str = ""
    tone_up_key_index: Optional[int] = None
    tone_up_mode: Optional[str] = None
    pred_key_index: Optional[int] = None
    pred_mode: Optional[str] = None
    pred_genre: Optional[str] = None


@dataclass
class ModePreset:
    reverb_short: int = 50
    reverb_long: int = 50
    echo: int = 50
    tune: int = 200


@dataclass
class ToneResult:
    key_index: int
    mode_str: str
    label: str
    cc_key: int
    cc_scale: int