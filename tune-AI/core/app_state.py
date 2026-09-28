from dataclasses import dataclass
from typing import Optional


@dataclass
class AppState:
    current_mode: str = "nhac_tre"
    current_youtube_id: Optional[str] = None
    current_youtube_url: str = ""
    current_youtube_title: str = ""
    reverb_muted: bool = False
    lofi_on: bool = False
    mic_muted: bool = False
    global_pitch_semitone: int = 0
    last_pred_key_index: Optional[int] = None
    last_pred_mode_str: Optional[str] = None
    last_pred_genre: Optional[str] = None
    ml_ready: bool = False
    cubase_project_path: str = ""
    license_days_left: int = 0