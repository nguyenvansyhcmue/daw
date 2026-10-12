"""macOS system-audio adapter backed by the bundled ScreenCaptureKit helper."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import threading
from pathlib import Path
from typing import Callable

import numpy as np

from ..models import AudioFrame, AudioSource


class MacOSScreenCaptureKitBackend:
    """Read the helper's framed PCM stream without exposing Swift to the UI."""

    def __init__(self, helper_path: Path | None = None):
        root = Path(__file__).resolve().parents[3]
        self.helper_path = helper_path or root / "native" / "macos" / "system_audio_capture"
        self._process: subprocess.Popen | None = None
        self._reader: threading.Thread | None = None
        self._running = False
        self._sequence = 0

    def is_available(self) -> bool:
        return sys.platform == "darwin" and self.helper_path.is_file() and os.access(self.helper_path, os.X_OK)

    def start(self, on_frame: Callable[[AudioFrame], None], on_error: Callable[[Exception], None]) -> None:
        if not self.is_available():
            raise RuntimeError("System Audio chưa sẵn sàng; cần helper ScreenCaptureKit đã được bundle")
        self._process = subprocess.Popen(
            [str(self.helper_path)], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, bufsize=0,
        )
        self._running = True
        self._reader = threading.Thread(target=self._read_packets, args=(on_frame, on_error),
                                        name="ai-tune-system-audio", daemon=True)
        self._reader.start()

    @staticmethod
    def _read_exact(stream, size: int) -> bytes:
        chunks: list[bytes] = []
        remaining = size
        while remaining:
            chunk = stream.read(remaining)
            if not chunk:
                return b""
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _read_packets(self, on_frame: Callable[[AudioFrame], None], on_error: Callable[[Exception], None]) -> None:
        assert self._process is not None and self._process.stdout is not None
        try:
            while self._running:
                header = self._read_exact(self._process.stdout, 4)
                if not header:
                    break
                frame_count = struct.unpack("<I", header)[0]
                if frame_count == 0 or frame_count > 16_384:
                    raise RuntimeError("System Audio helper returned an invalid frame size")
                payload = self._read_exact(self._process.stdout, frame_count * 2 * 4)
                if not payload:
                    break
                samples = np.frombuffer(payload, dtype="<f4").reshape(frame_count, 2).copy()
                self._sequence += 1
                on_frame(AudioFrame(samples, AudioSource.SYSTEM, sequence_number=self._sequence))
            if self._running:
                detail = ""
                if self._process.stderr is not None:
                    detail = self._process.stderr.read().decode("utf-8", errors="replace").strip()
                raise RuntimeError(detail or "System Audio capture stopped")
        except Exception as exc:
            if self._running:
                on_error(exc)

    def stop(self) -> None:
        self._running = False
        if self._process is not None:
            try:
                self._process.terminate()
                self._process.wait(timeout=1.0)
            except Exception:
                try:
                    self._process.kill()
                except Exception:
                    pass
            finally:
                self._process = None
        if self._reader is not None and self._reader is not threading.current_thread():
            self._reader.join(timeout=1.0)
        self._reader = None
