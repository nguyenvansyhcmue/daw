"""Reusable presentation components for the Analog Future chassis."""

from PyQt6.QtCore import Qt, QRectF, QSize
from PyQt6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap
from PyQt6.QtWidgets import QFrame, QLabel, QPushButton, QWidget

from ui.analog_theme import TOKENS, asset


class AnalogChassis(QFrame):
    """Reference-base surface for the complete five-tier application UI."""

    REFERENCE_SIZE = QSize(TOKENS.reference_width, TOKENS.reference_height)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._reference = QPixmap(str(asset("THM_full_app_reference.png")))
        self.setObjectName("AnalogChassis")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        painter.fillRect(self.rect(), QColor(TOKENS.chassis_black))
        if not self._reference.isNull():
            painter.drawPixmap(self.rect(), self._reference)
        painter.end()


class ReferenceOverlay(QWidget):
    """Absolute reference-base overlay with one responsive coordinate system."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._placements = []
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)

    def place(self, widget, x, y, width, height):
        widget.setParent(self)
        self._placements.append((widget, float(x), float(y), float(width), float(height)))
        widget.show()
        self._apply_one(self._placements[-1])
        return widget

    def _apply_one(self, placement):
        widget, x, y, width, height = placement
        sx = self.width() / TOKENS.reference_width if self.width() else 1.0
        sy = self.height() / TOKENS.reference_height if self.height() else 1.0
        widget.setGeometry(round(x * sx), round(y * sy), round(width * sx), round(height * sy))

    def resizeEvent(self, event):
        for placement in self._placements:
            self._apply_one(placement)
        super().resizeEvent(event)


class HardwarePanel(QFrame):
    """A restrained graphite faceplate shared by all reference sections."""

    def __init__(self, title: str = "", parent=None):
        super().__init__(parent)
        self.title = title
        self.setObjectName("AnalogHardwarePanel")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(0, 0, 0, TOKENS.panel_shadow_alpha))
        painter.drawRoundedRect(rect.translated(0, 3), TOKENS.panel_radius, TOKENS.panel_radius)
        gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft())
        gradient.setColorAt(0, QColor("#242526"))
        gradient.setColorAt(.16, QColor(TOKENS.panel_graphite))
        gradient.setColorAt(1, QColor("#0D0E0F"))
        painter.setBrush(gradient)
        painter.setPen(QPen(QColor(TOKENS.warm_steel), 1))
        painter.drawRoundedRect(rect, TOKENS.panel_radius, TOKENS.panel_radius)
        inner = rect.adjusted(3, 3, -3, -3)
        painter.setPen(QPen(QColor(232, 217, 194, TOKENS.edge_highlight_alpha), .7))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(inner, TOKENS.panel_radius - 2, TOKENS.panel_radius - 2)
        if self.title:
            font = QFont("Segoe UI", 9)
            font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 2.0)
            painter.setFont(font)
            painter.setPen(QColor(TOKENS.ivory_text))
            painter.drawText(QRectF(rect.left() + 16, rect.top() + 8, rect.width() - 32, 18),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                             self.title.upper())
        painter.end()


class DigitalLCD(QLabel):
    """Small amber LCD readout; state remains owned by the caller."""

    def __init__(self, text: str = "00", parent=None):
        super().__init__(text, parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumHeight(34)
        self.setStyleSheet(
            "QLabel { background:#090705; color:#E49A3A; border:1px solid #6B3A18;"
            "border-radius:4px; padding:2px 10px; font:24px 'Consolas'; }"
        )


class HardwareButton(QPushButton):
    """Asset-backed flat physical button with no indicator lamp or glow."""

    def __init__(self, text: str, asset_path=None, parent=None):
        super().__init__(text, parent)
        self._asset = QPixmap(str(asset_path)) if asset_path else QPixmap()
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setMinimumSize(QSize(112, 70))

    def set_asset(self, asset_path):
        self._asset = QPixmap(str(asset_path)) if asset_path else QPixmap()
        self.update()

    def paintEvent(self, event):
        if self._asset.isNull():
            super().paintEvent(event)
            return
        painter = QPainter(self)
        target = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        if self.isDown():
            target.translate(0, 2)
        pixmap = self._asset.scaled(target.size().toSize(), Qt.AspectRatioMode.KeepAspectRatio,
                                    Qt.TransformationMode.SmoothTransformation)
        painter.drawPixmap(target.center().x() - pixmap.width() / 2,
                           target.center().y() - pixmap.height() / 2, pixmap)
        painter.end()
