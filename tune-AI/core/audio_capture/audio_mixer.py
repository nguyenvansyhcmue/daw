"""Mix normalised microphone and system-audio frames outside callbacks."""

from __future__ import annotations

import numpy as np

from .models import AudioFrame, AudioSource


class AudioMixer:
    """Small deterministic mixer with soft protection against hard clipping."""

    def __init__(self, microphone_gain: float = 1.0, system_gain: float = 1.0):
        self.microphone_gain = float(microphone_gain)
        self.system_gain = float(system_gain)

    @staticmethod
    def _as_stereo(samples: np.ndarray) -> np.ndarray:
        if samples.shape[1] == 2:
            return samples
        return np.repeat(samples, 2, axis=1)

    def mix(self, microphone: AudioFrame | None, system: AudioFrame | None) -> AudioFrame | None:
        frames = [frame for frame in (microphone, system) if frame is not None]
        if not frames:
            return None
        sample_rate = frames[0].sample_rate
        if any(frame.sample_rate != sample_rate for frame in frames):
            raise ValueError("Frames must be resampled before mixing")
        count = min(frame.samples.shape[0] for frame in frames)
        output = np.zeros((count, 2), dtype=np.float32)
        if microphone is not None:
            output += self._as_stereo(microphone.samples[:count]) * self.microphone_gain
        if system is not None:
            output += self._as_stereo(system.samples[:count]) * self.system_gain
        # tanh avoids a sudden hard clip when the two live sources overlap.
        np.tanh(output, out=output)
        return AudioFrame(output, AudioSource.SYSTEM, sample_rate=sample_rate)
