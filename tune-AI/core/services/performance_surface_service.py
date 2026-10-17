"""Local-only client for StudioForge Adaptive Performance Macros.

The DAW owns every macro assignment and parameter value.  This service only
reads the short-lived Unix-domain socket configuration published by the DAW,
then sends semantic JSON commands.  It never opens a TCP port and never moves
audio through Python.
"""

from __future__ import annotations

import json
import os
import socket
import tempfile
from pathlib import Path
from typing import Any


class PerformanceSurfaceService:
    CONFIG_NAME = "studioforge-performance-surface.json"
    MAX_MESSAGE_BYTES = 64 * 1024

    def __init__(self) -> None:
        self._config: dict[str, Any] = {}
        self._project_revision = ""

    @property
    def connected(self) -> bool:
        return bool(self._config.get("socketPath") and self._config.get("token"))

    @property
    def project_revision(self) -> str:
        return self._project_revision

    def refresh_configuration(self) -> bool:
        path = Path(tempfile.gettempdir()) / self.CONFIG_NAME
        try:
            if path.stat().st_mode & 0o077:
                return False
            data = json.loads(path.read_text(encoding="utf-8"))
            if (data.get("schema") != 1 or data.get("transport") != "unix"
                    or not isinstance(data.get("socketPath"), str)
                    or not isinstance(data.get("token"), str)):
                return False
            self._config = data
            return True
        except (OSError, ValueError, TypeError):
            self._config = {}
            return False

    def poll_state(self) -> dict[str, Any] | None:
        if not self.connected and not self.refresh_configuration():
            return None
        response = self._request({"command": "getState"}, mutating=False)
        if response and response.get("type") == "performanceMacroState":
            self._project_revision = str(response.get("projectRevision", ""))
            return response
        return None

    def preview_mapping(self, macro_id: int, bus_id: str, plugin_slot: int,
                        plugin_instance_id: str) -> dict[str, Any] | None:
        return self._request({
            "command": "previewMacroMapping", "macroId": int(macro_id),
            "busId": str(bus_id), "pluginSlot": int(plugin_slot),
            "pluginInstanceId": str(plugin_instance_id),
        })

    def apply_mapping(self, macro_id: int, bus_id: str, plugin_slot: int,
                      plugin_instance_id: str, confirm_needs_review: bool) -> dict[str, Any] | None:
        return self._request({
            "command": "applyMacroMapping", "macroId": int(macro_id),
            "busId": str(bus_id), "pluginSlot": int(plugin_slot),
            "pluginInstanceId": str(plugin_instance_id),
            "confirmNeedsReview": bool(confirm_needs_review),
        })

    def clear_mapping(self, macro_id: int) -> dict[str, Any] | None:
        return self._request({"command": "clearMacroMapping", "macroId": int(macro_id)})

    def set_value(self, macro_id: int, value: float) -> dict[str, Any] | None:
        return self._request({"command": "setMacroValue", "macroId": int(macro_id),
                              "value": max(0.0, min(1.0, float(value)))})

    def _request(self, payload: dict[str, Any], mutating: bool = True) -> dict[str, Any] | None:
        if not self.connected and not self.refresh_configuration():
            return None
        if mutating and not self._project_revision:
            return None
        payload["token"] = self._config["token"]
        if mutating:
            payload["projectRevision"] = self._project_revision
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(0.35)
                connection.connect(self._config["socketPath"])
                connection.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))
                chunks: list[bytes] = []
                size = 0
                while size < self.MAX_MESSAGE_BYTES:
                    part = connection.recv(min(4096, self.MAX_MESSAGE_BYTES - size))
                    if not part:
                        break
                    chunks.append(part)
                    size += len(part)
                    if b"\n" in part:
                        break
            result = json.loads(b"".join(chunks).split(b"\n", 1)[0].decode("utf-8"))
            if isinstance(result, dict):
                revision = result.get("projectRevision")
                if revision is not None:
                    self._project_revision = str(revision)
                return result
        except (OSError, ValueError, TypeError, KeyError):
            self._config = {}
        return None
