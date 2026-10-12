"""Shared immutable-ish data contracts for audio capture."""

from dataclasses import dataclass
from enum import Enum
from time import monotonic

import numpy as np


class AudioSource(str, Enum):
    MICROPHONE = "microphone"
    SYSTEM = "system"


class CaptureState(str, Enum):
    IDLE = "idle"
    REQUESTING_PERMISSION = "requesting_permission"
    STARTING = "starting"
    RUNNING = "running"
    RECONNECTING = "reconnecting"
    DEGRADED_MIC_ONLY = "degraded_mic_only"
    PERMISSION_DENIED = "permission_denied"
    ERROR = "error"


@dataclass(frozen=True)
class AudioFrame:
    """One normalised PCM block. Samples are frames × channels float32."""

    samples: np.ndarray
    source: AudioSource
    sample_rate: int = 48_000
    timestamp: float = 0.0
    sequence_number: int = 0

    def __post_init__(self):
        if self.samples.ndim != 2:
            raise ValueError("AudioFrame.samples must have shape (frames, channels)")
        if self.samples.dtype != np.float32:
            raise ValueError("AudioFrame.samples must be float32")
        if self.samples.shape[1] not in (1, 2):
            raise ValueError("AudioFrame supports mono or stereo only")
        if self.sample_rate <= 0:
            raise ValueError("sample_rate must be positive")
        if not self.timestamp:
            object.__setattr__(self, "timestamp", monotonic())

    @property
    def channels(self) -> int:
        return int(self.samples.shape[1])


@dataclass(frozen=True)
class LevelSnapshot:
    """Decimated UI-safe signal information; no raw recording is retained."""

    rms_left: float
    rms_right: float
    peak_left: float
    peak_right: float
    waveform: np.ndarray
    timestamp: float
