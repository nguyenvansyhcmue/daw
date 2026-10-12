"""Coordinates optional mic/system sources and publishes UI-safe snapshots."""

from __future__ import annotations

import queue
import threading
from collections.abc import Callable

from .audio_analysis import AudioAnalysis
from .audio_mixer import AudioMixer
from .models import AudioFrame, AudioSource, CaptureState, LevelSnapshot


class CaptureCoordinator:
    """Lifecycle owner for capture backends.

    Backends call ``publish_frame`` from their capture thread. A worker owns
    mixing/analysis, so visual rendering and native callbacks never share a
    thread. The bounded queue intentionally favours current audio over stale
    data when the UI is under load.
    """

    def __init__(self, microphone_backend=None, system_backend=None, on_snapshot: Callable[[LevelSnapshot], None] | None = None,
                 on_state: Callable[[CaptureState, str], None] | None = None):
        self.microphone_backend = microphone_backend
        self.system_backend = system_backend
        self.on_snapshot = on_snapshot
        self.on_state = on_state
        self._analysis = AudioAnalysis()
        self._mixer = AudioMixer()
        self._queue: queue.Queue[AudioFrame] = queue.Queue(maxsize=24)
        self._running = False
        self._worker: threading.Thread | None = None
        self._latest: dict[AudioSource, AudioFrame] = {}
        self.dropped_frames = 0

    def _set_state(self, state: CaptureState, message: str) -> None:
        if self.on_state:
            self.on_state(state, message)

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        self._set_state(CaptureState.STARTING, "Đang kết nối âm thanh…")
        self._worker = threading.Thread(target=self._run, name="ai-tune-audio-analysis", daemon=True)
        self._worker.start()
        available_sources = 0
        for backend, source_name in ((self.microphone_backend, "Microphone"), (self.system_backend, "System Audio")):
            if backend is None or not backend.is_available():
                continue
            try:
                backend.start(self.publish_frame, self._on_backend_error)
                available_sources += 1
            except Exception as exc:
                self._on_backend_error(exc)
        if available_sources == 2:
            self._set_state(CaptureState.RUNNING, "Sẵn sàng — Mic + âm thanh máy")
        elif available_sources == 1:
            self._set_state(CaptureState.DEGRADED_MIC_ONLY, "Sẵn sàng — Chỉ Microphone")
        else:
            self._set_state(CaptureState.ERROR, "Không kết nối được nguồn âm thanh")

    def stop(self) -> None:
        self._running = False
        for backend in (self.microphone_backend, self.system_backend):
            if backend is not None:
                try:
                    backend.stop()
                except Exception:
                    pass
        if self._worker is not None:
            self._worker.join(timeout=1.0)
            self._worker = None
        self._latest.clear()
        self._set_state(CaptureState.IDLE, "Đã dừng âm thanh")

    def publish_frame(self, frame: AudioFrame) -> None:
        try:
            self._queue.put_nowait(frame)
        except queue.Full:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self._queue.put_nowait(frame)
            except queue.Full:
                pass
            self.dropped_frames += 1

    def _on_backend_error(self, error: Exception) -> None:
        self._set_state(CaptureState.RECONNECTING, f"Đang kết nối lại thiết bị âm thanh: {error}")

    def _run(self) -> None:
        while self._running:
            try:
                frame = self._queue.get(timeout=.15)
            except queue.Empty:
                continue
            self._latest[frame.source] = frame
            microphone = self._latest.get(AudioSource.MICROPHONE)
            system = self._latest.get(AudioSource.SYSTEM)
            try:
                mixed = self._mixer.mix(microphone, system)
                if mixed is not None and self.on_snapshot is not None:
                    self.on_snapshot(self._analysis.analyse(mixed))
            except Exception as exc:
                self._on_backend_error(exc)
