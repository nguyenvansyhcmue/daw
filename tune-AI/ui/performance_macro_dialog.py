"""Performer-first connection flow for StudioForge performance macros."""

from __future__ import annotations

from typing import Any

from PyQt6.QtWidgets import QComboBox, QFormLayout, QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget

from ui.analog_dialog_theme import ANALOG_DIALOG_QSS
from ui.window_chrome import AnalogDialog


class PerformanceMacroDialog(AnalogDialog):
    """Connect one physical knob without exposing DAW routing by default.

    StudioForge remains the authority for buses, plug-ins and parameter safety.
    This dialog translates that topology into a compact performer workflow:
    choose an intent, choose a discovered effect, connect the knob.
    """

    def __init__(self, macro_index: int, snapshot: dict[str, Any], surface, parent=None):
        super().__init__(parent)
        self.macro_index = macro_index
        self.snapshot = snapshot if isinstance(snapshot, dict) else {}
        self.surface = surface
        self._targets: list[tuple[dict[str, Any], dict[str, Any]]] = []
        self._details_visible = False

        self.setWindowTitle(f"CONNECT KNOB {macro_index + 1}")
        self.setStyleSheet(ANALOG_DIALOG_QSS)
        self.setMinimumWidth(460)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 48, 24, 20)
        layout.setSpacing(11)

        title = QLabel(f"Connect knob {macro_index + 1}", self)
        title.setObjectName("DialogTitle")
        layout.addWidget(title)
        self.caption = QLabel("Choose the sound you want. StudioForge safely connects the best control for you.", self)
        self.caption.setObjectName("DialogCaption")
        self.caption.setWordWrap(True)
        layout.addWidget(self.caption)

        plugin_label = QLabel("CHỌN HIỆU ỨNG ĐỂ ĐIỀU KHIỂN", self)
        plugin_label.setObjectName("SettingsValueLabel")
        self.plugin_label = plugin_label
        layout.addWidget(plugin_label)
        self.plugin_picker = QComboBox(self)
        self.plugin_picker.currentIndexChanged.connect(self._preview)
        layout.addWidget(self.plugin_picker)

        self.guidance = QLabel(self)
        self.guidance.setObjectName("SettingsValueLabel")
        self.guidance.setWordWrap(True)
        layout.addWidget(self.guidance)

        self.summary = QLabel(self)
        self.summary.setObjectName("DialogCaption")
        self.summary.setWordWrap(True)
        layout.addWidget(self.summary)

        self.refresh = QPushButton("KIỂM TRA LẠI STUDIOFORGE", self)
        self.refresh.clicked.connect(self._refresh_from_daw)
        layout.addWidget(self.refresh)

        self.details_toggle = QPushButton("HIỆN ROUTING NÂNG CAO", self)
        self.details_toggle.setFlat(True)
        self.details_toggle.clicked.connect(self._toggle_details)
        layout.addWidget(self.details_toggle)

        self.advanced = QWidget(self)
        advanced_form = QFormLayout(self.advanced)
        advanced_form.setContentsMargins(0, 0, 0, 0)
        advanced_form.setSpacing(7)
        self.bus_combo = QComboBox(self.advanced)
        self.plugin_combo = QComboBox(self.advanced)
        advanced_form.addRow("DAW BUS", self.bus_combo)
        advanced_form.addRow("PLUG-IN SLOT", self.plugin_combo)
        self.advanced.hide()
        layout.addWidget(self.advanced)

        buttons = QHBoxLayout()
        self.connect_button = QPushButton("KẾT NỐI NÚM", self)
        self.clear = QPushButton("BỎ KẾT NỐI", self)
        self.clear.setProperty("destructive", True)
        buttons.addWidget(self.connect_button, 1)
        buttons.addWidget(self.clear)
        layout.addLayout(buttons)

        self.bus_combo.currentIndexChanged.connect(self._rebuild_advanced_plugins)
        self.plugin_combo.currentIndexChanged.connect(self._sync_picker_from_advanced)
        self.connect_button.clicked.connect(self._apply)
        self.clear.clicked.connect(self._clear)
        self._populate_targets()

    def _available_buses(self) -> list[dict[str, Any]]:
        buses = self.snapshot.get("buses", [])
        return [bus for bus in buses if isinstance(bus, dict)] if isinstance(buses, list) else []

    def _populate_targets(self):
        self._targets = []
        selected_instance = str(self.snapshot.get("pluginInstanceId") or "")
        self.bus_combo.blockSignals(True)
        self.bus_combo.clear()
        for bus in self._available_buses():
            self.bus_combo.addItem(str(bus.get("name") or "Unnamed bus"), bus)
            for plugin in bus.get("plugins", []):
                if isinstance(plugin, dict):
                    self._targets.append((bus, plugin))
        self.bus_combo.blockSignals(False)
        self._rebuild_advanced_plugins()
        self._populate_picker()

        if selected_instance:
            for index in range(self.plugin_picker.count()):
                target = self.plugin_picker.itemData(index)
                plugin = target[1] if isinstance(target, tuple) and len(target) == 2 else {}
                if isinstance(plugin, dict) and str(plugin.get("instanceId") or "") == selected_instance:
                    self.plugin_picker.setCurrentIndex(index)
                    break

    def _populate_picker(self):
        self.plugin_picker.blockSignals(True)
        self.plugin_picker.clear()
        for bus, plugin in self._targets:
            state = " · Bypassed" if plugin.get("bypassed") else ""
            name = str(plugin.get("name") or "Unknown effect")
            bus_name = str(bus.get("name") or "StudioForge bus")
            self.plugin_picker.addItem(f"{name} — {bus_name}{state}", (bus, plugin))
        self.plugin_picker.blockSignals(False)

        has_target = self.plugin_picker.count() > 0
        self.plugin_label.setVisible(has_target)
        self.plugin_picker.setVisible(has_target)
        self.plugin_picker.setEnabled(has_target)
        self.connect_button.setVisible(has_target)
        self.connect_button.setEnabled(has_target)
        self.clear.setVisible(has_target or bool(self.snapshot.get("pluginInstanceId")))
        self.details_toggle.setVisible(has_target)
        if not has_target:
            self.advanced.hide()
            self._details_visible = False
        if not has_target:
            self.guidance.setText(
                "StudioForge đã kết nối, nhưng project này chưa có effect return nào để núm điều khiển."
            )
            self.summary.setText(
                "1. Trong StudioForge Mixer, ở track muốn dùng, bấm Sends + để tạo FX Bus.\n"
                "2. Trên FX Bus đó, bấm + Add Audio FX và thêm plug-in.\n"
                "3. Quay lại đây và bấm Kiểm tra lại StudioForge."
            )
        else:
            self.guidance.setText("StudioForge sẽ phân tích plug-in đã chọn và tự đề xuất control an toàn nhất.")
            self._preview()

    def _rebuild_advanced_plugins(self):
        self.plugin_combo.blockSignals(True)
        self.plugin_combo.clear()
        bus = self.bus_combo.currentData()
        plugins = bus.get("plugins", []) if isinstance(bus, dict) else []
        for plugin in plugins:
            if isinstance(plugin, dict):
                self.plugin_combo.addItem(str(plugin.get("name") or "Unknown effect"), plugin)
        self.plugin_combo.blockSignals(False)

    def _sync_picker_from_advanced(self):
        bus = self.bus_combo.currentData()
        plugin = self.plugin_combo.currentData()
        if not isinstance(bus, dict) or not isinstance(plugin, dict):
            return
        for index in range(self.plugin_picker.count()):
            target = self.plugin_picker.itemData(index)
            if isinstance(target, tuple) and target[0] is bus and target[1] is plugin:
                self.plugin_picker.setCurrentIndex(index)
                return

    def _selected_target(self):
        target = self.plugin_picker.currentData()
        if not isinstance(target, tuple) or len(target) != 2:
            return None
        bus, plugin = target
        return (bus, plugin) if isinstance(bus, dict) and isinstance(plugin, dict) else None

    def _preview(self):
        target = self._selected_target()
        if target is None:
            return
        bus, plugin = target
        reply = self.surface.preview_mapping(self.macro_index, str(bus["id"]), int(plugin["slot"]),
                                             str(plugin["instanceId"]))
        macro = reply.get("macro", {}) if isinstance(reply, dict) and reply.get("ok") else {}
        targets = macro.get("targets", []) if isinstance(macro, dict) else []
        names = " + ".join(str(item.get("name", "")) for item in targets if isinstance(item, dict))
        confidence = float(macro.get("confidence", 0.0) or 0.0) if isinstance(macro, dict) else 0.0
        if not macro:
            self.summary.setText("StudioForge không thể xác nhận effect này. Hãy kiểm tra lại StudioForge rồi chọn lại.")
            return
        safety = "Sẵn sàng kết nối" if confidence >= .70 else "Cần xác nhận mapping này"
        self.summary.setText(f"{safety} · {names or 'Control chính của plug-in'}")

    def _apply(self):
        target = self._selected_target()
        if target is None:
            return
        bus, plugin = target
        reply = self.surface.apply_mapping(self.macro_index, str(bus["id"]), int(plugin["slot"]),
                                           str(plugin["instanceId"]), True)
        if isinstance(reply, dict) and reply.get("ok"):
            self.accept()
        else:
            self.summary.setText("StudioForge không thể kết nối núm này. Hãy kiểm tra lại StudioForge và thử lại.")

    def _clear(self):
        reply = self.surface.clear_mapping(self.macro_index)
        if isinstance(reply, dict) and reply.get("ok"):
            self.accept()

    def _refresh_from_daw(self):
        state = self.surface.poll_state()
        if not isinstance(state, dict):
            self.guidance.setText("Không kết nối được StudioForge. Hãy mở project StudioForge rồi thử lại.")
            return
        macros = state.get("macros", [])
        macro = macros[self.macro_index] if isinstance(macros, list) and self.macro_index < len(macros) else {}
        self.snapshot = dict(macro) if isinstance(macro, dict) else {}
        self.snapshot["buses"] = state.get("buses", [])
        self._populate_targets()

    def _toggle_details(self):
        self._details_visible = not self._details_visible
        self.advanced.setVisible(self._details_visible)
        self.details_toggle.setText("ẨN ROUTING NÂNG CAO" if self._details_visible else "HIỆN ROUTING NÂNG CAO")
