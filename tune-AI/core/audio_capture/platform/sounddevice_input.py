"""Microphone capture adapter backed by sounddevice/PortAudio."""

from __future__ import annotations

from typing import Callable

import numpy as np
import sounddevice as sd

from ..models import AudioFrame, AudioSource


class SoundDeviceInputBackend:
    """Capture one microphone device and publish float32 frame blocks."""

    def __init__(self, device=None, sample_rate: int = 48_000, channels: int = 2):
        self.device = device
        self.sample_rate = sample_rate
        self.channels = channels
        self._stream = None
        self._sequence = 0

    def is_available(self) -> bool:
        try:
            return int(sd.query_devices(self.device, "input")["max_input_channels"]) > 0
        except Exception:
            return False

    def start(self, on_frame: Callable[[AudioFrame], None], on_error: Callable[[Exception], None]) -> None:
        device_info = sd.query_devices(self.device, "input")
        channels = min(self.channels, int(device_info["max_input_channels"]))
        if channels < 1:
            raise RuntimeError("Thiết bị được chọn không có ngõ vào âm thanh")
        sample_rate = int(round(float(device_info["default_samplerate"])))

        def callback(indata, frames, time_info, status):
            if status:
                on_error(RuntimeError(str(status)))
            try:
                self._sequence += 1
                # PortAudio owns indata after this callback. Copying is the
                # explicit hand-off boundary; processing happens elsewhere.
                samples = np.array(indata, dtype=np.float32, copy=True)
                on_frame(AudioFrame(samples, AudioSource.MICROPHONE, sample_rate, sequence_number=self._sequence))
            except Exception as exc:
                on_error(exc)

        self._stream = sd.InputStream(
            device=self.device, channels=channels, samplerate=sample_rate,
            dtype="float32", callback=callback,
        )
        self._stream.start()

    def stop(self) -> None:
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            finally:
                self._stream = None
