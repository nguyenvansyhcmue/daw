"""Platform-neutral audio capture primitives used by AI-Tune.

Backends may differ, but consumers only receive normalised ``AudioFrame``
objects.  Keeping this boundary explicit prevents platform code leaking into
the UI or tone-analysis services.
"""

from .models import AudioFrame, AudioSource, CaptureState, LevelSnapshot
from .audio_analysis import AudioAnalysis
from .audio_mixer import AudioMixer

__all__ = [
    "AudioAnalysis",
    "AudioFrame",
    "AudioMixer",
    "AudioSource",
    "CaptureState",
    "LevelSnapshot",
]
