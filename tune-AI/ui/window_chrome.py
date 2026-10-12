"""Small, reusable dark chrome for secondary modal surfaces.

The main window deliberately keeps native OS chrome. Dialogs use this lighter
custom chrome so they remain on-brand without pretending to be a full window.
"""

from PyQt6.QtCore import Qt, QPoint
from PyQt6.QtGui import QColor, QPainter, QPen
from PyQt6.QtWidgets import QDialog


class AnalogDialog(QDialog):
    TITLE_HEIGHT = 38

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlag(Qt.WindowType.FramelessWindowHint, True)
        self._drag_origin: QPoint | None = None

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.fillRect(0, 0, self.width(), self.TITLE_HEIGHT, QColor("#151311"))
        painter.setPen(QPen(QColor("#7c654c"), 1))
        painter.drawLine(0, self.TITLE_HEIGHT - 1, self.width(), self.TITLE_HEIGHT - 1)
        painter.setPen(QColor("#ead8c0"))
        painter.drawText(16, 25, self.windowTitle())
        painter.setBrush(QColor("#ad4845"))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(self.width() - 27, 12, 14, 14)
        painter.setPen(QColor("#2b1111"))
        painter.drawText(self.width() - 25, 23, "×")

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and event.position().y() < self.TITLE_HEIGHT:
            if event.position().x() >= self.width() - 42:
                self.reject()
                return
            self._drag_origin = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._drag_origin is not None and event.buttons() & Qt.MouseButton.LeftButton:
            self.move(event.globalPosition().toPoint() - self._drag_origin)
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._drag_origin = None
        super().mouseReleaseEvent(event)
