import queue
import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional

import librosa
import numpy as np
import sounddevice as sd


NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F',
              'F#', 'G', 'G#', 'A', 'A#', 'B']

MAJOR_PROFILE = np.array(
    [6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
     2.52, 5.19, 2.39, 3.66, 2.29, 2.88],
    dtype=float
)

MINOR_PROFILE = np.array(
    [6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
     2.54, 4.75, 3.98, 2.69, 3.34, 3.17],
    dtype=float
)


@dataclass
class ToneResult:
    key: str
    scale: str
    confidence: float
    label: str


class RealtimeToneService:
    def __init__(self):
        self.sample_rate = 22050
        self.channels = 1
        self.window_seconds = 3.0
        self.hop_seconds = 1.0
        self.min_rms = 0.005

        self.running = False
        self.audio_queue: queue.Queue = queue.Queue()
        self.stream = None
        self.worker_thread = None

        self.last_results: list[tuple[str, str]] = []
        self.last_emit_time = 0.0
        self.emit_cooldown_seconds = 2.0

    # =========================================================
    # DEVICE LIST
    # =========================================================

    def list_devices(self):
        devices = sd.query_devices()
        result = []

        for i, dev in enumerate(devices):
            result.append({
                "index": i,
                "name": str(dev["name"]),
                "max_input_channels": int(dev["max_input_channels"]),
                "max_output_channels": int(dev["max_output_channels"]),
                "default_samplerate": float(dev["default_samplerate"]),
                "hostapi": int(dev["hostapi"]),
            })

        return result

    def list_realtime_candidates(self):
        """
        Ưu tiên:
        - Stereo Mix / Mix 01 / Cable Output / What U Hear
        - sau đó đến output devices để thử WASAPI loopback
        """
        devices = self.list_devices()
        candidates = []

        for dev in devices:
            name_lower = dev["name"].lower()

            if dev["max_input_channels"] > 0:
                priority = 3
                if "mix 01" in name_lower:
                    priority = 0
                elif "stereo mix" in name_lower or "what u hear" in name_lower:
                    priority = 0
                elif "cable output" in name_lower:
                    priority = 0
                elif "mix" in name_lower:
                    priority = 1

                candidates.append({
                    "index": dev["index"],
                    "name": dev["name"],
                    "mode": "input",
                    "priority": priority,
                })

            if dev["max_output_channels"] > 0:
                priority = 4
                if "speaker 01" in name_lower:
                    priority = 2
                elif "speaker" in name_lower or "headphone" in name_lower or "realtek" in name_lower:
                    priority = 3

                candidates.append({
                    "index": dev["index"],
                    "name": dev["name"],
                    "mode": "loopback_output",
                    "priority": priority,
                })

        candidates.sort(key=lambda x: (x["priority"], x["name"].lower()))
        return candidates

    # =========================================================
    # START / STOP
    # =========================================================

    def start(
        self,
        device_index: int,
        device_mode: str,
        on_stable_tone: Callable[[ToneResult], None],
        on_status: Optional[Callable[[str], None]] = None,
    ):
        if self.running:
            self.stop()

        self.running = True
        self.audio_queue = queue.Queue()
        self.last_results = []
        self.last_emit_time = 0.0

        if on_status:
            on_status("Đang khởi động realtime detect...")

        def audio_callback(indata, frames, time_info, status):
            if status and on_status:
                on_status(f"Audio status: {status}")
            if self.running:
                self.audio_queue.put(indata.copy())

        try:
            if device_mode == "loopback_output":
                try:
                    wasapi_settings = sd.WasapiSettings(loopback=True)
                    self.stream = sd.InputStream(
                        device=device_index,
                        channels=self.channels,
                        samplerate=self.sample_rate,
                        dtype="float32",
                        callback=audio_callback,
                        extra_settings=wasapi_settings,
                    )
                except Exception:
                    self.stream = sd.InputStream(
                        device=device_index,
                        channels=self.channels,
                        samplerate=self.sample_rate,
                        dtype="float32",
                        callback=audio_callback,
                    )
            else:
                self.stream = sd.InputStream(
                    device=device_index,
                    channels=self.channels,
                    samplerate=self.sample_rate,
                    dtype="float32",
                    callback=audio_callback,
                )

            self.stream.start()

            self.worker_thread = threading.Thread(
                target=self._process_loop,
                args=(on_stable_tone, on_status),
                daemon=True
            )
            self.worker_thread.start()

            if on_status:
                on_status("Realtime detect đã chạy.")
        except Exception as e:
            self.running = False
            raise RuntimeError(f"Không mở được audio device: {e}") from e

    def stop(self):
        self.running = False

        try:
            if self.stream is not None:
                self.stream.stop()
                self.stream.close()
        except Exception:
            pass

        self.stream = None
        self.worker_thread = None
        self.last_results = []

    # =========================================================
    # PROCESS LOOP
    # =========================================================

    def _process_loop(
        self,
        on_stable_tone: Callable[[ToneResult], None],
        on_status: Optional[Callable[[str], None]] = None,
    ):
        buffer = np.zeros(0, dtype=np.float32)
        target_len = int(self.sample_rate * self.window_seconds)
        hop_len = int(self.sample_rate * self.hop_seconds)

        while self.running:
            try:
                chunk = self.audio_queue.get(timeout=1.0).flatten().astype(np.float32)
                buffer = np.concatenate([buffer, chunk])

                while len(buffer) >= target_len:
                    analysis_chunk = buffer[:target_len]
                    buffer = buffer[hop_len:]

                    result = self._detect_chunk(analysis_chunk)
                    if result is None:
                        continue

                    self.last_results.append((result.key, result.scale))
                    if len(self.last_results) > 5:
                        self.last_results.pop(0)

                    stable = self._get_stable_result(result)
                    if stable is not None:
                        now = time.time()
                        if now - self.last_emit_time >= self.emit_cooldown_seconds:
                            self.last_emit_time = now
                            on_stable_tone(stable)

            except queue.Empty:
                continue
            except Exception as e:
                if on_status:
                    on_status(f"Lỗi realtime detect: {e}")

    # =========================================================
    # DETECT
    # =========================================================

    def _detect_chunk(self, y: np.ndarray) -> Optional[ToneResult]:
        if len(y) == 0:
            return None

        rms = float(np.sqrt(np.mean(np.square(y))))
        if rms < self.min_rms:
            return None

        y_harmonic = librosa.effects.harmonic(y)
        chroma = librosa.feature.chroma_cqt(y=y_harmonic, sr=self.sample_rate)
        chroma_avg = np.mean(chroma, axis=1)

        best_key = None
        best_scale = None
        best_score = -999.0

        for i in range(12):
            major_score = np.corrcoef(chroma_avg, np.roll(MAJOR_PROFILE, i))[0, 1]
            minor_score = np.corrcoef(chroma_avg, np.roll(MINOR_PROFILE, i))[0, 1]

            if major_score > best_score:
                best_score = major_score
                best_key = NOTE_NAMES[i]
                best_scale = "Major"

            if minor_score > best_score:
                best_score = minor_score
                best_key = NOTE_NAMES[i]
                best_scale = "Minor"

        if best_key is None or best_scale is None:
            return None

        return ToneResult(
            key=best_key,
            scale=best_scale,
            confidence=float(best_score),
            label=f"{best_key} {best_scale}"
        )

    def _get_stable_result(self, latest_result: ToneResult) -> Optional[ToneResult]:
        if len(self.last_results) < 3:
            return None

        latest_pair = self.last_results[-1]
        count_same = self.last_results.count(latest_pair)

        if count_same >= 3:
            return latest_result

        return None