"""Windows system-mix capture through WASAPI loopback.

No Stereo Mix device is required. sounddevice maps the native WASAPI
loopback flag to the selected render endpoint on Windows.
"""

from __future__ import annotations

import sys
from typing import Callable

import numpy as np
import sounddevice as sd

from ..models import AudioFrame, AudioSource


class WindowsWasapiLoopbackBackend:
    def __init__(self, output_device=None, sample_rate: int = 48_000, channels: int = 2):
        self.output_device = output_device
        self.sample_rate = sample_rate
        self.channels = channels
        self._stream = None
        self._sequence = 0

    def is_available(self) -> bool:
        return sys.platform.startswith("win")

    def start(self, on_frame: Callable[[AudioFrame], None], on_error: Callable[[Exception], None]) -> None:
        if not self.is_available():
            raise RuntimeError("WASAPI loopback chỉ khả dụng trên Windows")

        def callback(indata, frames, time_info, status):
            if status:
                on_error(RuntimeError(str(status)))
            try:
                self._sequence += 1
                samples = np.array(indata, dtype=np.float32, copy=True)
                on_frame(AudioFrame(samples, AudioSource.SYSTEM, self.sample_rate, sequence_number=self._sequence))
            except Exception as exc:
                on_error(exc)

        self._stream = sd.InputStream(
            device=self.output_device, channels=self.channels, samplerate=self.sample_rate,
            dtype="float32", callback=callback,
            extra_settings=sd.WasapiSettings(loopback=True),
        )
        self._stream.start()

    def stop(self) -> None:
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            finally:
                self._stream = None
