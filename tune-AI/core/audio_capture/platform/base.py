"""Protocol that keeps OS capture details out of orchestration and UI."""

from __future__ import annotations

from typing import Callable, Protocol

from ..models import AudioFrame


class AudioCaptureBackend(Protocol):
    def start(self, on_frame: Callable[[AudioFrame], None], on_error: Callable[[Exception], None]) -> None: ...
    def stop(self) -> None: ...
    def is_available(self) -> bool: ...
