"""Native painted controls used by the THM Vocal Panel surface."""

from PyQt6.QtCore import Qt, QPointF, QRectF, pyqtSignal
from PyQt6.QtGui import QColor, QConicalGradient, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QPolygonF, QRadialGradient
from PyQt6.QtWidgets import QFrame, QLabel, QPushButton, QSlider, QWidget


class WaveformDecoration(QWidget):
    """Deterministic, irregular audio waveform used as a quiet visual texture."""

    def __init__(self, accent="#52E7FF", height=28, parent=None):
        super().__init__(parent)
        self._accent = QColor(accent)
        self.setFixedHeight(height)
        self.setMinimumWidth(120)

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        center = self.rect().center().y()
        p.setPen(QPen(QColor(self._accent.red(), self._accent.green(), self._accent.blue(), 115), 1))
        pattern = (3, 5, 9, 4, 14, 7, 22, 8, 5, 17, 11, 28, 6, 13, 4, 19, 8, 5, 11, 3)
        width = max(1, self.width() - 10)
        for index, amplitude in enumerate(pattern):
            x = 5 + index * width / max(1, len(pattern) - 1)
            p.drawLine(QPointF(x, center - amplitude / 2), QPointF(x, center + amplitude / 2))
        p.end()


class MicrophoneVisual(QWidget):
    """A low-contrast studio microphone illustration for the input block."""

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        background = QRadialGradient(r.center() - QPointF(0, r.height() * .25), r.width() * .68)
        background.setColorAt(0, QColor('#12304A')); background.setColorAt(.52, QColor('#07121E')); background.setColorAt(1, QColor('#04080D'))
        p.setPen(QPen(QColor(76, 139, 188, 55), 1)); p.setBrush(background); p.drawRoundedRect(r, 10, 10)
        body = QRectF(r.center().x() - r.width() * .105, r.top() + r.height() * .16, r.width() * .21, r.height() * .48)
        metal = QLinearGradient(body.topLeft(), body.bottomRight())
        metal.setColorAt(0, QColor('#5C7180')); metal.setColorAt(.35, QColor('#172937')); metal.setColorAt(1, QColor('#050A10'))
        p.setPen(QPen(QColor(123, 171, 202, 110), 1)); p.setBrush(metal); p.drawRoundedRect(body, body.width() / 2, body.width() / 2)
        p.setPen(QPen(QColor(168, 212, 238, 46), 1))
        for y in range(int(body.top()) + 8, int(body.bottom()) - 5, 5):
            p.drawLine(QPointF(body.left() + 4, y), QPointF(body.right() - 4, y))
        p.setPen(QPen(QColor('#31536B'), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        stem_y = body.bottom() + r.height() * .15
        p.drawLine(QPointF(body.center().x(), body.bottom()), QPointF(body.center().x(), stem_y))
        p.drawLine(QPointF(body.center().x() - r.width() * .17, stem_y), QPointF(body.center().x() + r.width() * .17, stem_y))
        p.end()


class HardwareButton(QPushButton):
    """Bevelled ± button rendered as a small piece of studio hardware."""

    def __init__(self, text: str, accent="#52E7FF", parent=None):
        super().__init__(text, parent)
        self._accent = QColor(accent)

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(2, 2, -2, -2)
        # Separate chassis, bevel and inset face make these read as physical
        # controls rather than illuminated flat buttons.
        p.setPen(Qt.PenStyle.NoPen); p.setBrush(QColor(0, 0, 0, 95))
        p.drawRoundedRect(r.translated(0, 2), 15, 15)
        edge = QColor(self._accent); edge.setAlpha(190 if self.underMouse() else 135)
        chassis = QLinearGradient(r.topLeft(), r.bottomLeft())
        chassis.setColorAt(0, QColor('#30465A'))
        chassis.setColorAt(.08, QColor('#162A3A'))
        chassis.setColorAt(1, QColor('#07111B'))
        p.setPen(QPen(edge, 1.1)); p.setBrush(chassis); p.drawRoundedRect(r, 15, 15)
        inset = r.adjusted(3, 3, -3, -3)
        face = QLinearGradient(inset.topLeft(), inset.bottomLeft())
        face.setColorAt(0, QColor('#14283A') if self.underMouse() else QColor('#0D1D2C'))
        face.setColorAt(.48, QColor('#091622'))
        face.setColorAt(1, QColor('#050C14'))
        p.setPen(QPen(QColor(171, 215, 240, 28), 1)); p.setBrush(face); p.drawRoundedRect(inset, 12, 12)
        p.setPen(QPen(QColor(220, 242, 255, 68), 1))
        p.drawLine(QPointF(inset.left() + 12, inset.top() + 1.5), QPointF(inset.right() - 12, inset.top() + 1.5))
        font = p.font(); font.setPointSize(25); font.setWeight(600); p.setFont(font)
        p.setPen(QColor(self._accent).lighter(125)); p.drawText(inset, Qt.AlignmentFlag.AlignCenter, self.text())
        p.end()


class ToneShiftChassis(QFrame):
    """Chamfered hardware housing for the semitone controls.

    It remains a normal QFrame, so the existing layout and all controller
    bindings stay untouched; only its material is painted here.
    """

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(1, 1, -1, -1)
        cut = min(18.0, rect.height() * .18)

        def chassis_path(inset=0.0):
            r = rect.adjusted(inset, inset, -inset, -inset)
            c = max(6.0, cut - inset * .5)
            path = QPainterPath()
            path.moveTo(r.left() + c, r.top())
            path.lineTo(r.right() - c, r.top())
            path.lineTo(r.right(), r.top() + c)
            path.lineTo(r.right(), r.bottom() - c)
            path.lineTo(r.right() - c, r.bottom())
            path.lineTo(r.left() + c, r.bottom())
            path.lineTo(r.left(), r.bottom() - c)
            path.lineTo(r.left(), r.top() + c)
            path.closeSubpath()
            return path

        shadow = chassis_path().translated(0, 2)
        painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(QColor(0, 0, 0, 110)); painter.drawPath(shadow)
        material = QLinearGradient(rect.topLeft(), rect.bottomLeft())
        material.setColorAt(0, QColor('#101E2B'))
        material.setColorAt(.18, QColor('#091522'))
        material.setColorAt(1, QColor('#050C13'))
        painter.setPen(QPen(QColor(111, 160, 193, 125), 1)); painter.setBrush(material); painter.drawPath(chassis_path())
        painter.setPen(QPen(QColor(192, 222, 240, 30), 1)); painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawPath(chassis_path(4))
        painter.end()


class VectorActionButton(QPushButton):
    """One aligned line-icon and label system for mode and footer action controls."""

    def __init__(self, text: str, icon_kind: str, accent="#57B8D9", mode=False, parent=None):
        super().__init__(text, parent)
        self._kind, self._accent, self._mode = icon_kind, QColor(accent), mode

    def _draw_icon(self, p, center, size):
        color = QColor(self._accent).lighter(120)
        p.setPen(QPen(color, 1.8, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.setBrush(Qt.BrushStyle.NoBrush)
        x, y = center.x(), center.y()
        if self._kind in ("auto", "target"):
            p.drawEllipse(QRectF(x-size*.34,y-size*.34,size*.68,size*.68)); p.drawLine(QPointF(x-size*.52,y), QPointF(x+size*.52,y)); p.drawLine(QPointF(x,y-size*.52), QPointF(x,y+size*.52)); p.drawEllipse(QRectF(x-2,y-2,4,4))
        elif self._kind in ("fix", "sliders"):
            for offset, knob in ((-.26,.13),(0,-.12),(.26,.18)):
                yy=y+offset*size; p.drawLine(QPointF(x-size*.42,yy), QPointF(x+size*.42,yy)); p.drawEllipse(QRectF(x+knob*size-2,yy-2,4,4))
        elif self._kind in ("edit", "music"):
            p.drawLine(QPointF(x+size*.15,y-size*.38), QPointF(x+size*.15,y+size*.25)); p.drawLine(QPointF(x+size*.15,y-size*.38), QPointF(x+size*.42,y-size*.47)); p.drawEllipse(QRectF(x-size*.18,y+size*.18,size*.28,size*.22))
        elif self._kind == "headphones":
            p.drawArc(QRectF(x-size*.38,y-size*.38,size*.76,size*.76), 25*16, 130*16); p.drawLine(QPointF(x-size*.38,y), QPointF(x-size*.38,y+size*.28)); p.drawLine(QPointF(x+size*.38,y), QPointF(x+size*.38,y+size*.28))
        elif self._kind == "mic":
            p.drawRoundedRect(QRectF(x-size*.13,y-size*.38,size*.26,size*.52),size*.13,size*.13); p.drawArc(QRectF(x-size*.29,y-size*.16,size*.58,size*.54),0,180*16); p.drawLine(QPointF(x,y+size*.38),QPointF(x,y+size*.52))
        elif self._kind == "save":
            p.drawRect(QRectF(x-size*.35,y-size*.38,size*.7,size*.76)); p.drawLine(QPointF(x-size*.18,y-size*.38),QPointF(x-size*.18,y-size*.08)); p.drawLine(QPointF(x+size*.18,y-size*.38),QPointF(x+size*.18,y-size*.08)); p.drawRect(QRectF(x-size*.18,y+size*.1,size*.36,size*.2))
        elif self._kind == "list":
            for offset in (-.22,0,.22): p.drawLine(QPointF(x-size*.34,y+offset*size),QPointF(x+size*.34,y+offset*size))
        elif self._kind == "play":
            path=QPainterPath(); path.moveTo(x-size*.2,y-size*.32); path.lineTo(x+size*.34,y); path.lineTo(x-size*.2,y+size*.32); path.closeSubpath(); p.setBrush(color); p.drawPath(path)
        elif self._kind == "phone":
            handset = QPainterPath(); handset.moveTo(x-size*.30,y-size*.30); handset.cubicTo(x-size*.43,y-size*.10,x-size*.34,y+size*.20,x-size*.12,y+size*.35); handset.lineTo(x+size*.06,y+size*.19); handset.cubicTo(x-size*.05,y+size*.11,x-size*.12,y+size*.03,x,y-size*.10); handset.lineTo(x-size*.18,y-size*.22); handset.closeSubpath(); p.drawPath(handset)
            p.drawRoundedRect(QRectF(x-size*.34,y-size*.37,size*.18,size*.16),2,2); p.drawRoundedRect(QRectF(x-size*.05,y+size*.19,size*.18,size*.16),2,2)
        elif self._kind == "settings":
            p.drawEllipse(QRectF(x-size*.17,y-size*.17,size*.34,size*.34))
            for angle in range(0,360,45):
                p.save(); p.translate(x,y); p.rotate(angle); p.drawRoundedRect(QRectF(-2,-size*.45,4,size*.16),1,1); p.restore()
        elif self._kind == "power":
            p.drawArc(QRectF(x-size*.34,y-size*.34,size*.68,size*.68),-45*16,270*16); p.drawLine(QPointF(x,y-size*.48),QPointF(x,y-size*.03))
        else: p.drawEllipse(QRectF(x-size*.23,y-size*.23,size*.46,size*.46))

    def paintEvent(self, event):
        p=QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r=QRectF(self.rect()).adjusted(1,1,-1,-1)
        tint=QColor(self._accent); tint.setAlpha(24 if self.isChecked() else 10)
        face=QLinearGradient(r.topLeft(),r.bottomLeft()); face.setColorAt(0,QColor('#101C26')); face.setColorAt(1,QColor('#09131B'))
        p.setPen(QPen(QColor(self._accent.red(),self._accent.green(),self._accent.blue(),105),1)); p.setBrush(face); p.drawRoundedRect(r,9,9)
        p.setBrush(tint); p.setPen(Qt.PenStyle.NoPen); p.drawRoundedRect(r,9,9)
        if self.isChecked(): p.setBrush(self._accent); p.drawEllipse(QRectF(r.right()-12,r.top()+7,5,5))
        if self._mode:
            self._draw_icon(p,QPointF(r.center().x(),r.top()+r.height()*.34),32)
            lines=self.text().split("\n"); font=p.font(); font.setPointSize(10); font.setWeight(600); p.setFont(font); p.setPen(QColor('#E2EBF0'))
            p.drawText(QRectF(r.left(),r.top()+r.height()*.56,r.width(),r.height()*.34),Qt.AlignmentFlag.AlignHCenter|Qt.AlignmentFlag.AlignTop,"\n".join(lines))
        else:
            self._draw_icon(p,QPointF(r.left()+32,r.center().y()),28)
            font=p.font(); font.setPointSize(10); font.setWeight(550); p.setFont(font); p.setPen(QColor('#DCE7ED'))
            p.drawText(QRectF(r.left()+60,r.top(),r.width()-68,r.height()),Qt.AlignmentFlag.AlignVCenter|Qt.AlignmentFlag.AlignLeft,self.text().replace("\n"," "))
        p.end()


class ToneShiftDisplay(QLabel):
    """LCD-like semitone display; inherits QLabel so existing state binding stays intact."""

    step_requested = pyqtSignal(int)
    reset_requested = pyqtSignal()

    _segments = {
        "0": (0, 1, 2, 3, 4, 5), "1": (1, 2), "2": (0, 1, 6, 4, 3),
        "3": (0, 1, 6, 2, 3), "4": (5, 6, 1, 2), "5": (0, 5, 6, 2, 3),
        "6": (0, 5, 6, 4, 2, 3), "7": (0, 1, 2), "8": (0, 1, 2, 3, 4, 5, 6),
        "9": (0, 1, 2, 3, 5, 6),
    }

    def __init__(self, text="+0", parent=None):
        super().__init__(text, parent)
        self._drag_y = None
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setCursor(Qt.CursorShape.SizeVerCursor)

    @staticmethod
    def _segment_paths(area: QRectF):
        """Return seven tapered LED segments, preserving legibility at HiDPI."""
        thickness = max(2.6, area.width() * .115)
        bevel = thickness * .42
        half = area.height() / 2

        def horizontal(y):
            return QPolygonF([
                QPointF(area.left() + thickness, y),
                QPointF(area.right() - thickness, y),
                QPointF(area.right() - thickness - bevel, y + thickness / 2),
                QPointF(area.right() - thickness, y + thickness),
                QPointF(area.left() + thickness, y + thickness),
                QPointF(area.left() + thickness + bevel, y + thickness / 2),
            ])

        def vertical(x, top, bottom):
            return QPolygonF([
                QPointF(x + thickness / 2, top), QPointF(x + thickness, top + bevel),
                QPointF(x + thickness, bottom - bevel), QPointF(x + thickness / 2, bottom),
                QPointF(x, bottom - bevel), QPointF(x, top + bevel),
            ])

        upper_top, upper_bottom = area.top() + thickness * .80, area.top() + half - thickness * .55
        lower_top, lower_bottom = area.top() + half + thickness * .55, area.bottom() - thickness * .80
        return (
            horizontal(area.top()), vertical(area.right() - thickness, upper_top, upper_bottom),
            vertical(area.right() - thickness, lower_top, lower_bottom), horizontal(area.bottom() - thickness),
            vertical(area.left(), lower_top, lower_bottom), vertical(area.left(), upper_top, upper_bottom),
            horizontal(area.center().y() - thickness / 2),
        )

    def _draw_digit(self, painter, area, digit):
        active = set(self._segments.get(digit, ()))
        for index, segment in enumerate(self._segment_paths(area)):
            color = QColor('#DDF6FA') if index in active else QColor(99, 145, 158, 16)
            painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(color); painter.drawPolygon(segment)

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(2, 2, -2, -2)
        p.setPen(Qt.PenStyle.NoPen); p.setBrush(QColor(0, 0, 0, 95)); p.drawRoundedRect(r.translated(0, 2), 11, 11)
        bezel = QLinearGradient(r.topLeft(), r.bottomLeft())
        bezel.setColorAt(0, QColor('#1B2934')); bezel.setColorAt(.18, QColor('#111C25')); bezel.setColorAt(1, QColor('#091118'))
        p.setPen(QPen(QColor(105, 148, 170, 110), 1)); p.setBrush(bezel); p.drawRoundedRect(r, 10, 10)
        # The display is a deep VFD window; reserve the bottom rail solely for
        # the calibrated semitone scale, as in physical pitch processors.
        glass = r.adjusted(8, 8, -8, -20)
        glass_gradient = QLinearGradient(glass.topLeft(), glass.bottomLeft())
        glass_gradient.setColorAt(0, QColor('#09151A')); glass_gradient.setColorAt(.28, QColor('#03090C')); glass_gradient.setColorAt(1, QColor('#010405'))
        p.setPen(QPen(QColor(106, 175, 202, 48), 1)); p.setBrush(glass_gradient); p.drawRoundedRect(glass, 6, 6)
        # Fine technical notches, deliberately irregular rather than a debug grid.
        p.setPen(QPen(QColor(79, 162, 186, 17), .7))
        for fraction in (.11, .18, .74, .81, .89):
            x = glass.left() + glass.width() * fraction
            p.drawLine(QPointF(x, glass.top() + 9), QPointF(x, glass.bottom() - 7))
        text = self.text().strip() or "+0"
        sign = text[0] if text[:1] in ("+", "-") else "+"
        digits = "".join(char for char in text if char.isdigit())[-2:] or "0"
        digit_width = min(27.0, (glass.width() - 38) / (2.1 if len(digits) > 1 else 1.35))
        digit_gap = max(2.0, digit_width * .10)
        digits_width = digit_width * len(digits) + digit_gap * (len(digits) - 1)
        group_width = 15 + 6 + digits_width
        group_left = glass.center().x() - group_width / 2
        digits_left = group_left + 20
        for index, digit in enumerate(digits):
            digit_area = QRectF(digits_left + index * (digit_width + digit_gap), glass.top() + 7, digit_width, glass.height() - 14)
            self._draw_digit(p, digit_area, digit)
        p.setPen(QPen(QColor('#C8EDF2'), 2.5, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        plus_center = QPointF(group_left + 7, glass.center().y())
        p.drawLine(QPointF(plus_center.x()-4.5, plus_center.y()), QPointF(plus_center.x()+4.5, plus_center.y()))
        if sign != "-": p.drawLine(QPointF(plus_center.x(), plus_center.y()-4.5), QPointF(plus_center.x(), plus_center.y()+4.5))
        scale_y = r.bottom() - 13
        scale = ("-12", "-6", "0", "+6", "+12")
        font = p.font(); font.setPointSize(6); font.setWeight(500); font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 0); p.setFont(font); p.setPen(QColor('#7891A0'))
        for index, label in enumerate(scale):
            x = r.left() + 17 + index * (r.width() - 34) / 4
            p.drawText(QRectF(x - 12, scale_y, 24, 9), Qt.AlignmentFlag.AlignCenter, label)
        p.end()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_y = event.position().y(); self.setFocus(); event.accept(); return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._drag_y is not None:
            delta = self._drag_y - event.position().y()
            threshold = 16 if event.modifiers() & Qt.KeyboardModifier.ShiftModifier else 8
            if abs(delta) >= threshold:
                self.step_requested.emit(1 if delta > 0 else -1)
                self._drag_y = event.position().y()
            event.accept(); return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._drag_y = None; super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event):
        self.reset_requested.emit(); event.accept()

    def wheelEvent(self, event):
        self.step_requested.emit(1 if event.angleDelta().y() > 0 else -1); event.accept()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Up: self.step_requested.emit(1)
        elif event.key() == Qt.Key.Key_Down: self.step_requested.emit(-1)
        elif event.key() == Qt.Key.Key_Home: self.reset_requested.emit()
        else: super().keyPressEvent(event)

class PresetCardButton(QPushButton):
    """Checkable preset card rendered from its dedicated background and icon."""

    def __init__(self, label: str, accent: str, assets, parent=None):
        super().__init__(label, parent)
        self._accent = QColor(accent)
        self._background = QPixmap(str(assets.background))
        self._icon = QPixmap(str(assets.icon))
        self.setMinimumHeight(94)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(.5, .5, -.5, -.5)
        clip = QPainterPath()
        clip.addRoundedRect(r, 10, 10)
        p.setClipPath(clip)
        if self._background.isNull():
            p.fillRect(r, QColor('#08131F'))
        else:
            image = self._background.scaled(
                r.size().toSize(),
                Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                Qt.TransformationMode.SmoothTransformation,
            )
            p.drawPixmap(r.center() - QPointF(image.width() / 2, image.height() / 2), image)
        shade = QLinearGradient(r.topLeft(), r.bottomRight())
        shade.setColorAt(0, QColor(2, 9, 17, 42))
        shade.setColorAt(1, QColor(2, 7, 14, 105))
        p.fillRect(r, shade)
        p.setClipping(False)
        p.setPen(QPen(QColor(self._accent.red(), self._accent.green(), self._accent.blue(), 225 if self.isChecked() else 150), 1.3 if self.isChecked() else 1))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRoundedRect(r, 10, 10)

        icon_size = min(60, r.height() * .64)
        icon = QRectF(r.left() + 18, r.center().y() - icon_size / 2, icon_size, icon_size)
        if not self._icon.isNull():
            p.drawPixmap(icon.toRect(), self._icon)
        else:
            p.setPen(QPen(QColor(self._accent), 1)); p.setBrush(QColor(self._accent.red(), self._accent.green(), self._accent.blue(), 18)); p.drawEllipse(icon)

        name = self.text()
        font = p.font(); font.setPointSize(12); font.setWeight(600); p.setFont(font); p.setPen(QColor('#E2EBF0'))
        text_area = QRectF(r.left() + icon.right() + 12, r.top() + 18, r.right() - icon.right() - 24, 24); p.drawText(text_area, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter, name)
        if self.isChecked():
            underline = QRectF(r.center().x() - 27, r.bottom() - 5, 54, 2.5)
            p.setPen(Qt.PenStyle.NoPen); p.setBrush(self._accent); p.drawRoundedRect(underline, 1, 1)
        p.end()


class RotaryKnob(QSlider):
    """A QSlider with rotary studio-hardware presentation and normal slider signals."""

    def __init__(self, accent: str, default_value: int, parent=None):
        super().__init__(Qt.Orientation.Horizontal, parent)
        self._accent = QColor(accent)
        self._default_value = default_value
        self._drag_origin = None
        self._drag_value = default_value
        self.setFixedSize(126, 126)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setCursor(Qt.CursorShape.SizeVerCursor)

    def set_diameter(self, diameter: int):
        diameter = max(56, min(126, int(diameter)))
        if self.width() != diameter or self.height() != diameter:
            self.setFixedSize(diameter, diameter)

    def _angle(self, value=None):
        value = self.value() if value is None else value
        span = max(1, self.maximum() - self.minimum())
        return 225.0 + 270.0 * (value - self.minimum()) / span

    @staticmethod
    def _point(center, radius, angle):
        from math import cos, radians, sin
        a = radians(angle)
        # UI angles start at 12 o'clock and advance clockwise.  This maps the
        # 225°→495° range to the standard lower-left → upper arc → lower-right
        # hardware sweep, matching the QPainter drawArc conversion below.
        return QPointF(center.x() + radius * sin(a), center.y() - radius * cos(a))

    @staticmethod
    def _arc_start(angle):
        """Convert the control's clock angle to Qt's counter-clockwise arc angle."""
        return int((90.0 - angle) * 16)

    def _draw_ticks(self, painter, center, radius, active_angle):
        """Draw the fine engraved calibration ring outside the value arc."""
        for tick in range(37):
            angle = 225 + tick * 270 / 36
            major = tick % 6 == 0
            outer = self._point(center, radius + (3.0 if major else 1.8), angle)
            inner = self._point(center, radius - (4.5 if major else 2.2), angle)
            if angle <= active_angle + .1:
                color = QColor(self._accent); color.setAlpha(145 if major else 100)
            else:
                color = QColor(151, 180, 198, 88 if major else 60)
            painter.setPen(QPen(color, 1.0 if major else .75,
                                Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            painter.drawLine(outer, inner)

    def _draw_value_arc(self, painter, arc, start, active):
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(QColor('#192A3A'), 5.0, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(arc, self._arc_start(495), int(270 * 16))
        if active <= start:
            return
        accent_shadow = QColor(self._accent); accent_shadow.setAlpha(20)
        painter.setPen(QPen(accent_shadow, 7.0, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(arc, self._arc_start(active), int((active - start) * 16))
        arc_gradient = QConicalGradient(arc.center(), 90 - active)
        dim = QColor(self._accent); dim.setAlpha(145)
        arc_gradient.setColorAt(0, QColor(self._accent).lighter(118))
        arc_gradient.setColorAt(.82, dim)
        arc_gradient.setColorAt(1, dim)
        painter.setPen(QPen(arc_gradient, 4.8, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(arc, self._arc_start(active), int((active - start) * 16))
        endpoint = self._point(arc.center(), arc.width() * .5, active)
        painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(QColor(self._accent).lighter(118))
        painter.drawEllipse(QRectF(endpoint.x() - 2.4, endpoint.y() - 2.4, 4.8, 4.8))

    def _draw_body(self, painter, body, active):
        center = body.center()
        # Multiple narrow rings create a machined rim without adding a box.
        painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(QColor(0, 0, 0, 135))
        painter.drawEllipse(body.translated(0, 3).adjusted(-2, -2, 2, 2))
        outer = QRadialGradient(center - QPointF(body.width() * .18, body.height() * .20), body.width() * .72)
        outer.setColorAt(0, QColor('#3A5263')); outer.setColorAt(.30, QColor('#1A2A38'))
        outer.setColorAt(.78, QColor('#08111A')); outer.setColorAt(1, QColor('#020508'))
        painter.setPen(QPen(QColor('#3D596B'), 1)); painter.setBrush(outer); painter.drawEllipse(body)
        bevel = body.adjusted(2.5, 2.5, -2.5, -2.5)
        painter.setPen(QPen(QColor(202, 222, 229, 105), 1)); painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(bevel)
        groove = body.adjusted(4.5, 4.5, -4.5, -4.5)
        painter.setPen(QPen(QColor('#010407'), 1.5)); painter.setBrush(QColor('#050B12')); painter.drawEllipse(groove)
        face = body.adjusted(5.5, 5.5, -5.5, -5.5)
        face_gradient = QRadialGradient(face.center() - QPointF(face.width() * .21, face.height() * .24), face.width() * .82)
        face_gradient.setColorAt(0, QColor('#4D6575')); face_gradient.setColorAt(.18, QColor('#2A4050'))
        face_gradient.setColorAt(.60, QColor('#142330')); face_gradient.setColorAt(1, QColor('#05090E'))
        painter.setPen(QPen(QColor(150, 184, 201, 80), 1)); painter.setBrush(face_gradient); painter.drawEllipse(face)
        # A restrained top-left metallic reflection replaces the old visible spokes.
        painter.setPen(QPen(QColor(218, 239, 250, 38), 1.1, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(face.adjusted(3, 3, -3, -3), 116 * 16, 64 * 16)
        pointer_color = QColor(self._accent).lighter(130)
        painter.setPen(QPen(pointer_color, 2.0, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(self._point(center, face.width() * .12, active),
                         self._point(center, face.width() * .34, active))

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        rect = QRectF(self.rect()).adjusted(2, 2, -2, -4)
        side = min(rect.width(), rect.height())
        body = QRectF(rect.center().x() - side * .32, rect.center().y() - side * .32,
                     side * .64, side * .64)
        arc = body.adjusted(-side * .11, -side * .11, side * .11, side * .11)
        center = body.center()
        start, active = self._angle(self.minimum()), self._angle()
        self._draw_ticks(p, center, arc.width() / 2, active)
        self._draw_value_arc(p, arc, start, active)
        self._draw_body(p, body, active)
        p.end()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_origin = event.position().y()
            self._drag_value = self.value()
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._drag_origin is not None:
            factor = .25 if event.modifiers() & Qt.KeyboardModifier.ShiftModifier else 1.0
            delta = (self._drag_origin - event.position().y()) * factor
            self.setValue(round(self._drag_value + delta * (self.maximum() - self.minimum()) / 120))
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._drag_origin = None
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event):
        self.setValue(self._default_value)
        event.accept()

    def wheelEvent(self, event):
        steps = event.angleDelta().y() / 120
        self.setValue(self.value() + round(steps * 2))
        event.accept()
