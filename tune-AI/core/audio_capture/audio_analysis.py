"""Level and waveform extraction performed away from native audio callbacks."""

from __future__ import annotations

from time import monotonic

import numpy as np

from .models import AudioFrame, LevelSnapshot


class AudioAnalysis:
    """Creates small UI snapshots without storing or recording raw audio."""

    def __init__(self, waveform_points: int = 96):
        if waveform_points < 8:
            raise ValueError("waveform_points must be at least 8")
        self.waveform_points = int(waveform_points)

    def analyse(self, frame: AudioFrame) -> LevelSnapshot:
        samples = frame.samples
        stereo = samples if frame.channels == 2 else np.repeat(samples, 2, axis=1)
        rms = np.sqrt(np.mean(np.square(stereo, dtype=np.float64), axis=0))
        peaks = np.max(np.abs(stereo), axis=0)
        mono = np.mean(stereo, axis=1)
        # Peak envelope per bucket reads clearly at a small header size.
        edges = np.linspace(0, mono.size, self.waveform_points + 1, dtype=np.int32)
        waveform = np.empty(self.waveform_points, dtype=np.float32)
        for index in range(self.waveform_points):
            bucket = mono[edges[index]:edges[index + 1]]
            waveform[index] = float(np.max(np.abs(bucket))) if bucket.size else 0.0
        return LevelSnapshot(
            rms_left=float(rms[0]), rms_right=float(rms[1]),
            peak_left=float(peaks[0]), peak_right=float(peaks[1]),
            waveform=waveform, timestamp=monotonic(),
        )
