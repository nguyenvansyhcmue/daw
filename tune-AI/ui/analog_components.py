"""Reusable presentation components for the Analog Future chassis."""

from math import exp

from PyQt6.QtCore import QElapsedTimer, QEasingCurve, QPropertyAnimation, QSequentialAnimationGroup, Qt, QPointF, QRectF, QSize, QTimer, pyqtProperty
from PyQt6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QRadialGradient
from PyQt6.QtWidgets import QFrame, QLabel, QPushButton, QWidget

from ui.analog_theme import REFERENCE_LAYOUT, TOKENS, asset


class AnalogChassis(QFrame):
    """Reference-base surface for the complete five-tier application UI."""

    REFERENCE_SIZE = QSize(TOKENS.reference_width, TOKENS.reference_height)

    def __init__(self, parent=None):
        super().__init__(parent)
        # This production reference deliberately contains no dynamic Tone
        # Shift digit; ToneShiftDisplay is its sole owner at runtime.
        self._reference = QPixmap(str(asset("THM_full_app_reference_meter_clean.png")))
        # Audio services provide a dB-normalised level (0 = -60 dB, 1 = 0 dB).
        # Keep the newest sample as a target, then advance the visual envelope
        # independently.  Tying rendering directly to input callbacks makes a
        # segmented meter look frozen whenever the capture buffer is large.
        self._input_target = 0.0
        self._input_level = 0.0
        self._input_peak = 0.0
        self._peak_hold_seconds = 0.0
        self._meter_clock = QElapsedTimer()
        self._meter_clock.start()
        self._meter_timer = QTimer(self)
        self._meter_timer.setTimerType(Qt.TimerType.PreciseTimer)
        self._meter_timer.setInterval(16)
        self._meter_timer.timeout.connect(self._advance_input_meter)
        self.setObjectName("AnalogChassis")

    def set_input_level(self, level: float):
        """Set the latest dB-normalised input level without quantising it."""
        self._input_target = max(0.0, min(1.0, float(level)))
        needs_animation = (
            abs(self._input_target - self._input_level) >= 0.0005
            or self._input_target > self._input_peak + 0.0005
        )
        if needs_animation and not self._meter_timer.isActive():
            self._meter_clock.restart()
            self._meter_timer.start()

    def _advance_input_meter(self):
        """Advance fast-attack/soft-release meter ballistics at display rate."""
        # A stalled event loop must not create one huge visual jump on resume.
        seconds = min(0.050, max(0.001, self._meter_clock.restart() / 1000.0))
        rising = self._input_target > self._input_level
        time_constant = 0.045 if rising else 0.200
        blend = 1.0 - exp(-seconds / time_constant)
        self._input_level += (self._input_target - self._input_level) * blend

        if self._input_level >= self._input_peak:
            self._input_peak = self._input_level
            self._peak_hold_seconds = 0.32
        elif self._peak_hold_seconds > 0.0:
            self._peak_hold_seconds = max(0.0, self._peak_hold_seconds - seconds)
        else:
            # The peak marker falls in a continuous motion instead of stepping
            # down a whole physical cell on every audio callback.
            self._input_peak = max(self._input_level, self._input_peak - seconds * 1.7)

        self.update()
        if (
            abs(self._input_target - self._input_level) < 0.0005
            and abs(self._input_peak - self._input_level) < 0.0005
            and self._peak_hold_seconds <= 0.0
        ):
            self._input_level = self._input_target
            self._input_peak = self._input_level
            self._meter_timer.stop()

    def reset_input_level(self):
        self._meter_timer.stop()
        self._input_target = self._input_level = self._input_peak = 0.0
        self._peak_hold_seconds = 0.0
        self.update()

    def reference_rect(self) -> QRectF:
        """Return the centred, non-distorted rectangle occupied by the artwork."""
        if self.width() <= 0 or self.height() <= 0:
            return QRectF()
        scale = min(self.width() / self.REFERENCE_SIZE.width(), self.height() / self.REFERENCE_SIZE.height())
        width = self.REFERENCE_SIZE.width() * scale
        height = self.REFERENCE_SIZE.height() * scale
        return QRectF((self.width() - width) / 2, (self.height() - height) / 2, width, height)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        painter.fillRect(self.rect(), QColor(TOKENS.chassis_black))
        reference = self.reference_rect()
        if not self._reference.isNull():
            painter.drawPixmap(reference.toRect(), self._reference)
        self._paint_input_meter(painter, reference)
        painter.end()

    def _paint_input_meter(self, painter: QPainter, reference: QRectF):
        """Render the physical grid and the live cells in one chassis pass."""
        if reference.isNull():
            return

        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.translate(reference.left(), reference.top())
        painter.scale(
            reference.width() / self.REFERENCE_SIZE.width(),
            reference.height() / self.REFERENCE_SIZE.height(),
        )
        bounds = REFERENCE_LAYOUT.input_meter
        rect = QRectF(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2)
        # More, narrower cells make the compact hardware meter feel refined
        # rather than like a stack of large pixels.  The generous gap keeps
        # each segment legible at the smallest supported window size.
        columns = ((.10, .23), (.67, .23))
        segments = 28
        cell_height = 2.5
        grid_top = rect.top() + 2.0
        grid_bottom = rect.bottom() - 3.0
        row_pitch = (grid_bottom - grid_top - cell_height) / (segments - 1)

        # _input_level is already mapped from -60 dB..0 dB to 0..1 in the
        # capture services.  Do not apply log10 a second time: that made quiet
        # signals leap close to the top of the meter and then stick there.
        active_cells = self._input_level * segments
        full_segments = min(segments, int(active_cells))
        edge_brightness = active_cells - full_segments
        peak_segment = min(segments - 1, max(0, int(self._input_peak * segments)))

        def mix(start: QColor, end: QColor, amount: float) -> QColor:
            amount = max(0.0, min(1.0, amount))
            return QColor(
                round(start.red() + (end.red() - start.red()) * amount),
                round(start.green() + (end.green() - start.green()) * amount),
                round(start.blue() + (end.blue() - start.blue()) * amount),
            )

        inactive = QColor("#383833")
        live = QColor("#C58B42")
        peak = QColor("#E9C681")
        for left_ratio, width_ratio in columns:
            x = rect.left() + rect.width() * left_ratio
            width = rect.width() * width_ratio
            for index in range(segments):
                y = grid_bottom - cell_height - index * row_pitch
                # A partially lit leading cell bridges the twenty physical
                # segments, so even small audio changes read as motion.
                brightness = 1.0 if index < full_segments else (
                    edge_brightness if index == full_segments else 0.0
                )
                color = mix(inactive, live, brightness)
                if self._input_peak > 0.0005 and index == peak_segment:
                    color = mix(color, peak, max(0.45, brightness))
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(color)
                painter.drawRoundedRect(QRectF(x, y, width, cell_height), .5, .5)
        self._draw_input_meter_scale(
            painter, grid_top + cell_height / 2, grid_bottom - cell_height / 2,
        )
        painter.restore()

    @staticmethod
    def _draw_input_meter_scale(painter: QPainter, top: float, bottom: float):
        """Draw a compact 0–60 attenuation scale beside the physical cells."""
        painter.setFont(QFont("Arial", 7, QFont.Weight.DemiBold))
        for index in range(21):
            value = index * 3
            y = top + (bottom - top) * index / 20
            major = value % 15 == 0
            tick = 5.0 if major else 2.5
            painter.setPen(QPen(QColor("#928A7A" if major else "#554F46"), .8))
            painter.drawLine(QPointF(211, y), QPointF(211 + tick, y))
            if major:
                painter.setPen(QColor("#C2B8A5"))
                painter.drawText(
                    QRectF(217, y - 5.5, 19, 11),
                    Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                    str(value),
                )


class ReferenceOverlay(QWidget):
    """Coordinate manager for direct children of the chassis.

    It deliberately is not a translucent visual layer. A full-size transparent
    QWidget forces Qt to repaint every ancestor whenever one live control
    changes; controls therefore live directly on the chassis while this object
    retains the reference-coordinate placement API.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self._placements = []
        self._reference_rect = QRectF()
        # It is a geometry manager, not a visual compositing surface.
        self.hide()

    def place(self, widget, x, y, width, height):
        host = self.parentWidget()
        if host is not None:
            widget.setParent(host)
        self._placements.append((widget, float(x), float(y), float(width), float(height)))
        self._apply_one(self._placements[-1])
        # Geometry must be established before the widget becomes visible.
        # Showing a child at Qt's temporary default size can trigger an
        # unnecessary paint/polish pass during application startup.
        widget.show()
        return widget

    def set_reference_rect(self, reference_rect: QRectF):
        """Use the same letterboxed reference area as the chassis artwork."""
        self._reference_rect = QRectF(reference_rect)
        for placement in self._placements:
            self._apply_one(placement)

    def _apply_one(self, placement):
        widget, x, y, width, height = placement
        reference = self._reference_rect if not self._reference_rect.isNull() else QRectF(self.rect())
        sx = reference.width() / TOKENS.reference_width if reference.width() else 1.0
        sy = reference.height() / TOKENS.reference_height if reference.height() else 1.0
        widget.setGeometry(
            round(reference.left() + x * sx),
            round(reference.top() + y * sy),
            round(width * sx),
            round(height * sy),
        )

    def resizeEvent(self, event):
        for placement in self._placements:
            self._apply_one(placement)
        super().resizeEvent(event)


class ToneDetectionReadout(QWidget):
    """Dedicated key/chord readout; transpose controls remain in Tone Shift."""

    def __init__(self, text: str = "Đang dò tone", parent=None):
        super().__init__(parent)
        self._text = text
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)

    def setText(self, text: str):
        self._text = str(text or "Đang dò tone")
        self.update()

    def text(self) -> str:
        return self._text

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        panel = QLinearGradient(rect.topLeft(), rect.bottomLeft())
        panel.setColorAt(0, QColor("#15110D"))
        panel.setColorAt(1, QColor("#080706"))
        painter.setPen(QPen(QColor("#8C765E"), 1.2))
        painter.setBrush(panel)
        painter.drawRoundedRect(rect, 7, 7)
        is_waiting = self._text == "Chờ tín hiệu"
        is_detecting = self._text == "Đang dò tone"
        caption = (
            "CHỜ TÍN HIỆU ÂM THANH" if is_waiting else
            "ĐANG PHÂN TÍCH KEY / CHORD" if is_detecting else
            "KEY / CHORD HIỆN TẠI"
        )
        caption_font = QFont("Segoe UI", 7)
        caption_font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 1.4)
        painter.setFont(caption_font)
        painter.setPen(QColor("#C7A97A"))
        painter.drawText(QRectF(rect.left(), rect.top() + 7, rect.width(), 16), Qt.AlignmentFlag.AlignCenter, caption)
        value_font = QFont("Consolas", 22)
        value_font.setWeight(QFont.Weight.DemiBold)
        painter.setFont(value_font)
        painter.setPen(
            QColor("#8E8375") if is_waiting else
            QColor("#E7B35E") if is_detecting else QColor("#F0D0A0")
        )
        painter.drawText(QRectF(rect.left(), rect.top() + 19, rect.width(), rect.height() - 23), Qt.AlignmentFlag.AlignCenter, self._text)
        painter.end()


class AutoKeyProgressStrip(QWidget):
    """Visual state of the current Auto-Key/Fix Tone analysis request."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._progress = 0
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)

    def set_progress(self, value: int):
        value = max(0, min(100, int(value)))
        if value != self._progress:
            self._progress = value
            self.update()

    def reset(self):
        self.set_progress(0)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        # This is intentionally driven only by the analysis state machine,
        # never by the live RMS callback. A percentage therefore always means
        # analysis completion, not microphone loudness.
        housing = QRectF(.5, 2, max(1, self.width() - 1), max(4, self.height() - 4))
        painter.setPen(QPen(QColor("#725236"), .8))
        painter.setBrush(QColor("#060707"))
        painter.drawRoundedRect(housing, housing.height() / 2, housing.height() / 2)
        track = housing.adjusted(5, 3, -5, -3)
        painter.setPen(QPen(QColor("#1D1711"), .7))
        painter.setBrush(QColor("#010202"))
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2)

        if self._progress > 0:
            fill = QRectF(track.left(), track.top(), track.width() * self._progress / 100.0, track.height())
            gradient = QLinearGradient(fill.topLeft(), fill.topRight())
            gradient.setColorAt(0, QColor("#B86D28"))
            gradient.setColorAt(1, QColor("#F0BD70"))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(gradient)
            painter.drawRoundedRect(fill, min(2.0, fill.height() / 2), min(2.0, fill.height() / 2))

        painter.setFont(QFont("Consolas", 7, QFont.Weight.DemiBold))
        painter.setPen(QColor("#F1DDBA"))
        label = f"{self._progress}%" if self._progress > 0 else "SẴN SÀNG AUTO-KEY"
        painter.drawText(track, Qt.AlignmentFlag.AlignCenter, label)
        painter.end()


class ReactiveAudioWaveform(QWidget):
    """A calm, live waveform for the header nameplate.

    This is intentionally an input-level visualiser, not a fake loading
    animation: the audio capture callback supplies its RMS level.  A UI timer
    applies a fast attack and a natural release, so speech and music feel
    alive without the display flickering at buffer rate.
    """

    _BAR_COUNT = 43

    def __init__(self, parent=None):
        super().__init__(parent)
        self._target = 0.0
        self._envelope = 0.0
        self._waveform = ()
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        self._timer = QTimer(self)
        self._timer.setInterval(33)
        self._timer.timeout.connect(self._advance)
        self._timer.start()

    def set_level(self, level: float):
        self._target = max(0.0, min(1.0, float(level)))

    def set_waveform(self, values):
        """Accept a decimated peak envelope from the audio-analysis worker."""
        self._waveform = tuple(max(0.0, min(1.0, float(value))) for value in values)

    def reset(self):
        self._target = self._envelope = 0.0
        self._waveform = ()
        self.update()

    def _advance(self):
        # Attack is intentionally faster than release.  This makes consonants
        # readable while retaining a small visual tail after a phrase ends.
        coefficient = .56 if self._target > self._envelope else .105
        self._envelope += (self._target - self._envelope) * coefficient
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(1, 2, -1, -2)
        if rect.width() <= 0 or rect.height() <= 0:
            return

        # Cover the printed decorative waveform only, preserving the brushed
        # nameplate around it.  The subtle centre line remains visible at rest.
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#10100F"))
        painter.drawRoundedRect(rect, 2, 2)
        centre_y = rect.center().y()
        painter.setPen(QPen(QColor(121, 92, 57, 100), .55))
        painter.drawLine(QPointF(rect.left(), centre_y), QPointF(rect.right(), centre_y))

        bar_width = max(.9, rect.width() / self._BAR_COUNT * .32)
        for index in range(self._BAR_COUNT):
            position = index / (self._BAR_COUNT - 1)
            waveform_index = min(len(self._waveform) - 1, int(position * len(self._waveform)))
            sample_peak = self._waveform[waveform_index] if self._waveform else 0.0
            # The shape comes from real decimated audio peaks. The smoothed
            # envelope only supplies the visual release after sound stops.
            amplitude = 1.0 + max(sample_peak, self._envelope * .32) * rect.height() * .47
            x = rect.left() + (index + .5) * rect.width() / self._BAR_COUNT
            top = centre_y - amplitude
            bottom = centre_y + amplitude
            intensity = int(72 + 183 * min(1.0, max(sample_peak, self._envelope) * 1.45 + .08))
            painter.setPen(QPen(QColor(226, 166, 86, intensity), bar_width, Qt.PenStyle.SolidLine,
                                Qt.PenCapStyle.RoundCap))
            painter.drawLine(QPointF(x, top), QPointF(x, bottom))
        painter.end()


class AnalogKnobPanel(QWidget):
    """Static lower-panel shell; each live value is owned by KnobValueReadout."""

    def __init__(self, title: str, controls: tuple[tuple[str, int, int], ...], parent=None):
        super().__init__(parent)
        self._title = title
        self._controls = controls  # label, minimum, maximum
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)

    def set_controls(self, controls: tuple[tuple[str, int, int], ...]):
        """Replace captions from DAW state without rebuilding knob widgets."""
        if len(controls) != len(self._controls):
            return
        self._controls = controls
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        surface = QLinearGradient(rect.topLeft(), rect.bottomLeft())
        surface.setColorAt(0, QColor("#24211D"))
        surface.setColorAt(.08, QColor("#141516"))
        surface.setColorAt(1, QColor("#0A0B0C"))
        painter.setPen(QPen(QColor("#805D39"), 1.1))
        painter.setBrush(surface)
        painter.drawRoundedRect(rect, 10, 10)
        painter.setPen(QPen(QColor(255, 218, 166, 52), 1))
        painter.drawLine(QPointF(rect.left() + 12, rect.top() + 2), QPointF(rect.right() - 12, rect.top() + 2))

        title_font = QFont("Segoe UI", 10, QFont.Weight.Bold)
        title_font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 1.45)
        painter.setFont(title_font); painter.setPen(QColor("#DAC8A9"))
        painter.drawText(QRectF(rect.left() + 18, rect.top() + 9, rect.width() - 36, 20), Qt.AlignmentFlag.AlignLeft, self._title)

        count = len(self._controls)
        for index, (label, minimum, maximum) in enumerate(self._controls):
            center_x = rect.left() + rect.width() * (index + .5) / count
            if index:
                painter.setPen(QPen(QColor(202, 181, 150, 34), 1))
                painter.drawLine(QPointF(rect.left() + rect.width() * index / count, rect.top() + 35),
                                 QPointF(rect.left() + rect.width() * index / count, rect.bottom() - 14))
            label_font = QFont("Segoe UI", 10, QFont.Weight.Bold)
            painter.setFont(label_font); painter.setPen(QColor("#E4D5BE"))
            painter.drawText(QRectF(center_x - 62, rect.top() + 29, 124, 18), Qt.AlignmentFlag.AlignCenter, label)
            scale_font = QFont("Segoe UI", 8, QFont.Weight.Bold)
            painter.setFont(scale_font); painter.setPen(QColor("#C7B293"))
            painter.drawText(QRectF(center_x - 56, rect.bottom() - 37, 28, 14), Qt.AlignmentFlag.AlignLeft, str(minimum))
            painter.drawText(QRectF(center_x + 28, rect.bottom() - 37, 28, 14), Qt.AlignmentFlag.AlignRight, str(maximum))
            display = QRectF(center_x - 30, rect.bottom() - 31, 60, 23)
            painter.setPen(QPen(QColor("#32291F"), 1)); painter.setBrush(QColor("#07090A"))
            painter.drawRoundedRect(display, 4, 4)
        painter.end()


class MacroRoutingCaption(QWidget):
    """An always-live routing label drawn above the chassis reference artwork.

    The lower analogue artwork contains printed effect names.  Those names are
    useful as a visual reference, but must never be mistaken for the actual
    DAW mapping.  This compact, opaque caption deliberately owns that small
    patch of the faceplate and replaces it with the current macro state.
    """

    def __init__(self, macro_number: int, parent=None):
        super().__init__(parent)
        self._macro_number = macro_number
        self._summary = "UNASSIGNED"
        self._state = "unassigned"
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, True)

    def set_mapping(self, summary: str, state: str):
        summary = (summary or "UNASSIGNED").upper()
        state = state or "unassigned"
        if summary == self._summary and state == self._state:
            return
        self._summary = summary
        self._state = state
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect())
        # Opaque faceplate patch: this intentionally masks the baked reference
        # caption below, making the runtime mapping the single source of truth.
        painter.fillRect(rect, QColor("#141516"))
        colors = {
            "active": QColor("#70D9C9"),
            "review": QColor("#E1B56F"),
            "bypassed": QColor("#B86C62"),
            "unassigned": QColor("#8B857A"),
        }
        accent = colors.get(self._state, colors["unassigned"])
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(accent)
        painter.drawEllipse(QRectF(1.5, rect.center().y() - 2.25, 4.5, 4.5))

        painter.setFont(QFont("Segoe UI", 7, QFont.Weight.Bold))
        painter.setPen(QColor("#C4B69E"))
        painter.drawText(QRectF(9, 0, 18, rect.height()), Qt.AlignmentFlag.AlignVCenter,
                         f"M{self._macro_number}")
        painter.setFont(QFont("Segoe UI", 7, QFont.Weight.DemiBold))
        painter.setPen(QColor("#EFE1C7") if self._state == "active" else QColor("#C3B8A8"))
        painter.drawText(QRectF(28, 0, rect.width() - 30, rect.height()),
                         Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                         painter.fontMetrics().elidedText(self._summary, Qt.TextElideMode.ElideRight,
                                                          max(10, int(rect.width() - 31))))
        painter.end()


class KnobValueReadout(QWidget):
    """Small independently repaintable value field for one rotary control."""

    def __init__(self, value: int, parent=None):
        super().__init__(parent)
        self._value = int(value)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        # This widget paints every pixel of its small value field. Declaring
        # that fact prevents Qt from rebuilding the translucent overlay and
        # complete chassis behind it for every changed digit.
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, True)

    def set_value(self, value: int):
        value = int(value)
        if value != self._value:
            self._value = value
            self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect())
        # The static panel owns the bezel. This small widget clears and draws
        # only its changing interior/text, avoiding a parent-panel repaint.
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#07090A"))
        painter.drawRoundedRect(rect, 4, 4)
        painter.setFont(QFont("Consolas", 13, QFont.Weight.Bold))
        painter.setPen(QColor("#F1DDBA"))
        painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, str(self._value))
        painter.end()


class AnalogPressButton(QPushButton):
    """Transparent hit area with a subtle, shape-constrained press response."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._press_depth = 0.0
        self._press_animation = QPropertyAnimation(self, b"pressDepth", self)
        self._release_animation = QSequentialAnimationGroup(self)
        # A short compression followed by a slower damped return reads as a
        # sprung mechanical switch, without delaying the actual click signal.
        self.pressed.connect(lambda: self._animate_press(1.65, 92))
        self.released.connect(self._animate_release)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setStyleSheet("QPushButton { background: transparent; border: 0; }")

    def _animate_press(self, target: float, duration: int):
        self._release_animation.stop()
        self._press_animation.stop()
        self._press_animation.setDuration(duration)
        self._press_animation.setEasingCurve(
            QEasingCurve.Type.OutCubic
        )
        self._press_animation.setStartValue(self._press_depth)
        self._press_animation.setEndValue(target)
        self._press_animation.start()

    def _animate_release(self):
        """Release with a short, damped mechanical rebound above rest."""
        self._press_animation.stop()
        self._release_animation.stop()
        self._release_animation.clear()
        rebound = QPropertyAnimation(self, b"pressDepth", self._release_animation)
        rebound.setDuration(92)
        rebound.setEasingCurve(QEasingCurve.Type.OutCubic)
        rebound.setStartValue(self._press_depth)
        rebound.setEndValue(-.28)
        settle = QPropertyAnimation(self, b"pressDepth", self._release_animation)
        settle.setDuration(58)
        settle.setEasingCurve(QEasingCurve.Type.InOutQuad)
        settle.setStartValue(-.28)
        settle.setEndValue(0.0)
        self._release_animation.addAnimation(rebound)
        self._release_animation.addAnimation(settle)
        self._release_animation.start()

    def _get_press_depth(self):
        return self._press_depth

    def _set_press_depth(self, value):
        self._press_depth = max(-.35, min(1.8, float(value)))
        self.update()

    pressDepth = pyqtProperty(float, fget=_get_press_depth, fset=_set_press_depth)

    def paintEvent(self, event):
        if abs(self._press_depth) <= .01:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        if self.property("mode_tile"):
            rect = QRectF(self.rect()).adjusted(3.25, 3.25, -3.25, -3.25)
            radius = 6.0
        else:
            rect = QRectF(self.rect()).adjusted(2.5, 2.5, -2.5, -2.5)
            radius = min(10.0, min(rect.width(), rect.height()) * .17)
        # Do not paint a second opaque face: the chassis asset already owns
        # the button surface.  A moving recessed edge alone reads as travel
        # while preserving the original material underneath.
        contact = rect.translated(0, self._press_depth)
        # A low-opacity material tint is constrained to the real rounded
        # silhouette. It gives the surface a compressed, less reflective face
        # without creating the old floating black rectangle.
        face = QLinearGradient(contact.topLeft(), contact.bottomLeft())
        face.setColorAt(0, QColor(0, 0, 0, 16))
        face.setColorAt(1, QColor(0, 0, 0, 44))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(face)
        painter.drawRoundedRect(contact, radius, radius)
        painter.setPen(QPen(QColor(0, 0, 0, 138), 1.15))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(contact, radius, radius)
        painter.setPen(QPen(QColor(244, 214, 160, 28), .7))
        painter.drawLine(
            QPointF(contact.left() + 8, contact.top() + .7),
            QPointF(contact.right() - 8, contact.top() + .7),
        )
        painter.setPen(QPen(QColor(0, 0, 0, 105), 1))
        painter.drawLine(
            QPointF(contact.left() + 8, contact.bottom() - .7),
            QPointF(contact.right() - 8, contact.bottom() - .7),
        )
        if self._press_depth < 0:
            painter.setPen(QPen(QColor(244, 214, 160, 54), .8))
            painter.drawLine(
                QPointF(contact.left() + 9, contact.top() + .5),
                QPointF(contact.right() - 9, contact.top() + .5),
            )
        painter.end()


class ModeCardButton(QPushButton):
    """A complete mode card with one renderer for its scene and foreground."""

    _SCENE_INSET = 24
    _ICON_DIAMETER = 38
    _ICON_LEFT = 19
    _LABEL_LEFT = 78
    _ICON_TRANSPARENT_PADDING = 34
    _FOREGROUND_COLOR = "#D0C3AB"
    # Lofi/Bolero's artwork is optically centred slightly above the geometric
    # midpoint. Keep both foreground elements on that same visual baseline.
    _CONTENT_CENTER_OFFSET_RATIO = -.07
    # Their labels sit a little above their icon centre; preserve that visual
    # baseline instead of treating text metrics as geometrically centred.
    _LABEL_CENTER_OFFSET_RATIO = -.055

    def __init__(self, background_path, label: str, icon_kind: str, icon_path, parent=None):
        super().__init__(parent)
        source = QPixmap(str(background_path))
        inset = self._SCENE_INSET
        self._background = source.copy(
            inset,
            inset,
            max(1, source.width() - inset * 2),
            max(1, source.height() - inset * 2),
        )
        self._label = label
        self._icon_kind = icon_kind
        self._requires_icon_vignette = icon_kind == "remix"
        icon_source = QPixmap(str(icon_path)) if icon_path else QPixmap()
        icon_inset = self._ICON_TRANSPARENT_PADDING
        cropped_icon = icon_source.copy(
            icon_inset,
            icon_inset,
            max(1, icon_source.width() - icon_inset * 2),
            max(1, icon_source.height() - icon_inset * 2),
        )
        self._icon = self._tint_icon(cropped_icon)
        self._press_depth = 0.0
        self._press_animation = QPropertyAnimation(self, b"pressDepth", self)
        self._release_animation = QSequentialAnimationGroup(self)
        self.pressed.connect(lambda: self._animate_press(1.65, 92))
        self.released.connect(self._animate_release)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setStyleSheet("QPushButton { background: transparent; border: 0; }")

    def _animate_press(self, target: float, duration: int):
        self._release_animation.stop()
        self._press_animation.stop()
        self._press_animation.setDuration(duration)
        self._press_animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._press_animation.setStartValue(self._press_depth)
        self._press_animation.setEndValue(target)
        self._press_animation.start()

    def _animate_release(self):
        self._press_animation.stop()
        self._release_animation.stop()
        self._release_animation.clear()
        rebound = QPropertyAnimation(self, b"pressDepth", self._release_animation)
        rebound.setDuration(92)
        rebound.setEasingCurve(QEasingCurve.Type.OutCubic)
        rebound.setStartValue(self._press_depth)
        rebound.setEndValue(-.28)
        settle = QPropertyAnimation(self, b"pressDepth", self._release_animation)
        settle.setDuration(58)
        settle.setEasingCurve(QEasingCurve.Type.InOutQuad)
        settle.setStartValue(-.28)
        settle.setEndValue(0.0)
        self._release_animation.addAnimation(rebound)
        self._release_animation.addAnimation(settle)
        self._release_animation.start()

    def _get_press_depth(self):
        return self._press_depth

    def _set_press_depth(self, value):
        self._press_depth = max(-.35, min(1.8, float(value)))
        self.update()

    pressDepth = pyqtProperty(float, fget=_get_press_depth, fset=_set_press_depth)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        card = QRectF(self.rect()).adjusted(.75, .75, -.75, -.75)
        radius = min(8.0, card.height() * .11)

        painter.save()
        painter.setClipPath(self._rounded_path(card, radius))
        if not self._background.isNull():
            scene = self._background.scaled(
                card.size().toSize(),
                Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                Qt.TransformationMode.SmoothTransformation,
            )
            painter.drawPixmap(
                card.center() - QPointF(scene.width() / 2, scene.height() / 2), scene,
            )

        # The left contrast falloff belongs to this card itself; it is not an
        # extra overlay hiding any artwork underneath.
        shade = QLinearGradient(card.left(), card.top(), card.left() + card.width() * .72, card.top())
        shade.setColorAt(0, QColor(4, 5, 5, 142))
        shade.setColorAt(.52, QColor(4, 5, 5, 92))
        shade.setColorAt(1, QColor(4, 5, 5, 0))
        painter.fillRect(card, shade)
        painter.restore()

        border = QPen(QColor("#9A7650"), 1.15)
        painter.setPen(border)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(card, radius, radius)
        painter.setPen(QPen(QColor(246, 218, 176, 64), .55))
        painter.drawRoundedRect(card.adjusted(1.25, 1.25, -1.25, -1.25), radius - 1, radius - 1)

        # Keep artwork, label and icon on the same fixed printed face as the
        # Lofi/Bolero cards.  The recessed contact layer is shared with their
        # AnalogPressButton, so all four modes feel mechanically identical.
        self._draw_icon(painter, card)
        self._draw_label(painter, card)
        if abs(self._press_depth) > .01:
            painter.save()
            painter.setClipPath(self._rounded_path(card, radius))
            contact = card.adjusted(2.5, 2.5, -2.5, -2.5).translated(0, self._press_depth)
            material = QLinearGradient(contact.topLeft(), contact.bottomLeft())
            material.setColorAt(0, QColor(0, 0, 0, 16))
            material.setColorAt(1, QColor(0, 0, 0, 44))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(material)
            painter.drawRoundedRect(contact, 6, 6)
            painter.setPen(QPen(QColor(0, 0, 0, 125), 1))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRoundedRect(contact, 6, 6)
            painter.setPen(QPen(QColor(244, 214, 160, 28), .7))
            painter.drawLine(
                QPointF(contact.left() + 8, contact.top() + .7),
                QPointF(contact.right() - 8, contact.top() + .7),
            )
            if self._press_depth < 0:
                painter.setPen(QPen(QColor(244, 214, 160, 54), .8))
                painter.drawLine(
                    QPointF(contact.left() + 9, contact.top() + .5),
                    QPointF(contact.right() - 9, contact.top() + .5),
                )
            painter.restore()
        painter.end()

    @staticmethod
    def _rounded_path(rect: QRectF, radius: float):
        path = QPainterPath()
        path.addRoundedRect(rect, radius, radius)
        return path

    def _draw_icon(self, painter: QPainter, card: QRectF):
        diameter = min(self._ICON_DIAMETER, card.height() - 20)
        center_y = card.center().y() + card.height() * self._CONTENT_CENTER_OFFSET_RATIO
        icon = QRectF(
            card.left() + self._ICON_LEFT,
            center_y - diameter / 2,
            diameter,
            diameter,
        )
        if self._icon_kind == "music":
            self._draw_music_icon(painter, icon)
            return
        if self._icon.isNull():
            return
        if self._requires_icon_vignette:
            # The city-light remix scene is much brighter than the other mode
            # scenes. A soft local vignette preserves the original ivory icon
            # without adding a separate widget or a hard-edged black patch.
            vignette = QRadialGradient(icon.center(), icon.width() * .78)
            vignette.setColorAt(0, QColor(3, 5, 5, 150))
            vignette.setColorAt(.62, QColor(3, 5, 5, 92))
            vignette.setColorAt(1, QColor(3, 5, 5, 0))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(vignette)
            painter.drawEllipse(icon.adjusted(-7, -7, 7, 7))
        rendered = self._icon.scaled(
            icon.size().toSize(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        painter.drawPixmap(
            icon.center() - QPointF(rendered.width() / 2, rendered.height() / 2),
            rendered,
        )

    @classmethod
    def _tint_icon(cls, icon: QPixmap) -> QPixmap:
        """Apply the same warm-metal foreground token as the mode labels."""
        if icon.isNull():
            return icon
        tinted = QPixmap(icon.size())
        tinted.fill(Qt.GlobalColor.transparent)
        painter = QPainter(tinted)
        painter.drawPixmap(0, 0, icon)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
        painter.fillRect(tinted.rect(), QColor(cls._FOREGROUND_COLOR))
        painter.end()
        return tinted

    def _draw_music_icon(self, painter: QPainter, icon: QRectF):
        """Render a centred quaver with the same restrained line weight as its peers."""
        foreground = QColor(self._FOREGROUND_COLOR)
        painter.setPen(QPen(foreground, 1.55))
        painter.setBrush(QColor(5, 7, 7, 70))
        painter.drawEllipse(icon)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        stem_x = icon.center().x() + icon.width() * .10
        stem_top = icon.top() + icon.height() * .24
        stem_bottom = icon.top() + icon.height() * .69
        painter.drawLine(QPointF(stem_x, stem_top), QPointF(stem_x, stem_bottom))
        flag = QPainterPath(QPointF(stem_x, stem_top))
        flag.cubicTo(
            QPointF(stem_x + icon.width() * .18, stem_top + 2),
            QPointF(stem_x + icon.width() * .20, stem_top + icon.height() * .16),
            QPointF(stem_x + icon.width() * .07, stem_top + icon.height() * .22),
        )
        painter.drawPath(flag)
        head = QRectF(stem_x - icon.width() * .25, stem_bottom - 3.5, 9, 7)
        painter.setBrush(foreground)
        painter.drawEllipse(head)

    def _draw_label(self, painter: QPainter, card: QRectF):
        font = QFont("Arial")
        font.setPixelSize(max(12, round(card.height() * .18)))
        font.setWeight(550)
        font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, .25)
        painter.setFont(font)
        label_rect = QRectF(
            card.left() + self._LABEL_LEFT,
            card.top() + card.height() * (
                self._CONTENT_CENTER_OFFSET_RATIO + self._LABEL_CENTER_OFFSET_RATIO
            ),
            card.width() - self._LABEL_LEFT - 12,
            card.height(),
        )
        alignment = Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft
        # Static Lofi/Bolero lettering is printed into textured artwork rather
        # than shown as a high-contrast HUD label. Reproduce that soft edge in
        # the one card renderer, without an effect/widget layered on top.
        diffusion = QColor(self._FOREGROUND_COLOR)
        diffusion.setAlpha(26)
        painter.setPen(diffusion)
        for offset in (QPointF(-.45, 0), QPointF(.45, 0), QPointF(0, -.45), QPointF(0, .45)):
            painter.drawText(label_rect.translated(offset), alignment, self._label)
        printed_ink = QColor(self._FOREGROUND_COLOR)
        printed_ink.setAlpha(205)
        painter.setPen(printed_ink)
        painter.drawText(label_rect, alignment, self._label)
