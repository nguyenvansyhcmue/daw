"""Native painted controls used by the THM Vocal Panel surface."""

from math import cos, radians, sin

from PyQt6.QtCore import QElapsedTimer, Qt, QPointF, QRectF, QTimer, pyqtSignal
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
        self._asset = QPixmap()

    def set_asset(self, path):
        self._asset = QPixmap(str(path)) if path else QPixmap()
        self.update()

    def paintEvent(self, event):
        if not self._asset.isNull():
            painter = QPainter(self)
            target = QRectF(self.rect()).adjusted(1, 1, -1, -1)
            if self.isDown():
                target.translate(0, 2)
            image = self._asset.scaled(target.size().toSize(), Qt.AspectRatioMode.KeepAspectRatio,
                                       Qt.TransformationMode.SmoothTransformation)
            painter.drawPixmap(target.center() - QPointF(image.width() / 2, image.height() / 2), image)
            painter.end()
            return
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
        self._asset = QPixmap()

    def set_asset(self, path):
        self._asset = QPixmap(str(path)) if path else QPixmap()
        self.update()

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
        if not self._asset.isNull():
            target = r.translated(0, 2 if self.isDown() else 0)
            image = self._asset.scaled(
                target.size().toSize(), Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            p.drawPixmap(target.center() - QPointF(image.width() / 2, image.height() / 2), image)
            p.end()
            return
        tint=QColor(self._accent); tint.setAlpha(24 if self.isChecked() else 10)
        p.setPen(Qt.PenStyle.NoPen); p.setBrush(QColor(0, 0, 0, 125)); p.drawRoundedRect(r.translated(0, 3),9,9)
        face=QLinearGradient(r.topLeft(),r.bottomLeft()); face.setColorAt(0,QColor('#1A2933')); face.setColorAt(.18,QColor('#0F1B24')); face.setColorAt(.55,QColor('#09141C')); face.setColorAt(1,QColor('#050B10'))
        p.setPen(QPen(QColor(self._accent.red(),self._accent.green(),self._accent.blue(),150),1)); p.setBrush(face); p.drawRoundedRect(r,9,9)
        inner = r.adjusted(3,3,-3,-3)
        p.setPen(QPen(QColor(205,225,232,30),.7)); p.setBrush(Qt.BrushStyle.NoBrush); p.drawRoundedRect(inner,7,7)
        p.setPen(QPen(QColor(0,0,0,95),1)); p.drawLine(QPointF(r.left()+10,r.bottom()-7),QPointF(r.right()-10,r.bottom()-7))
        p.setBrush(tint); p.setPen(Qt.PenStyle.NoPen); p.drawRoundedRect(r,9,9)
        if self.isChecked():
            p.setBrush(self._accent); p.drawEllipse(QRectF(r.right()-14,r.top()+7,6,6))
            p.setBrush(QColor(255,255,255,135)); p.drawEllipse(QRectF(r.right()-12.5,r.top()+8.5,2,2))
        if self._mode:
            self._draw_icon(p,QPointF(r.center().x(),r.top()+r.height()*.34),32)
            lines=self.text().split("\n"); font=p.font(); font.setPointSize(10); font.setWeight(600); p.setFont(font); p.setPen(QColor('#E2EBF0'))
            p.drawText(QRectF(r.left(),r.top()+r.height()*.56,r.width(),r.height()*.34),Qt.AlignmentFlag.AlignHCenter|Qt.AlignmentFlag.AlignTop,"\n".join(lines))
        else:
            # Analog rack module layout: icon sits in a recessed metal well,
            # while the caption is engraved below it instead of being a flat
            # icon-plus-text row.
            icon_center = QPointF(r.center().x(), r.top() + r.height() * .40)
            well = QRectF(icon_center.x() - 25, icon_center.y() - 25, 50, 50)
            p.setPen(QPen(QColor(0, 0, 0, 150), 2)); p.setBrush(QColor('#050B10'))
            p.drawEllipse(well.translated(0, 2))
            p.setPen(QPen(QColor(self._accent.red(), self._accent.green(), self._accent.blue(), 105), 1))
            p.setBrush(QLinearGradient(well.topLeft(), well.bottomRight()))
            p.drawEllipse(well)
            p.setPen(QPen(QColor(210, 232, 238, 48), .8)); p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawEllipse(well.adjusted(4, 4, -4, -4))
            self._draw_icon(p, icon_center, 25)
            font=p.font(); font.setPointSize(9); font.setWeight(650); font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, .8); p.setFont(font); p.setPen(QColor('#DCE7ED'))
            p.drawText(QRectF(r.left()+8,r.top()+r.height()*.68,r.width()-16,r.height()*.22),Qt.AlignmentFlag.AlignHCenter|Qt.AlignmentFlag.AlignVCenter,self.text().replace("\n"," "))
            p.setPen(QColor(self._accent)); p.drawLine(QPointF(r.left()+18,r.bottom()-8), QPointF(r.right()-18,r.bottom()-8))
        p.end()


class ToneShiftDisplay(QLabel):
    """Dynamic digits inside the Tone Shift LCD owned by the chassis artwork.

    The reference image owns the LCD bezel and its static semitone scale.  This
    widget deliberately paints only the changing glass/digits so those fixed
    layers can never be rendered twice.
    """

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
            color = QColor('#F0D6A7') if index in active else QColor(126, 104, 72, 15)
            painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(color); painter.drawPolygon(segment)

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = QRectF(self.rect()).adjusted(2, 2, -2, -2)
        # The cleaned chassis owns the entire LCD glass and bezel. This widget
        # renders values only, so it cannot form a second inset/LCD layer.
        display = r.adjusted(15, 2, -15, -24)
        text = self.text().strip() or "+0"
        sign = text[0] if text[:1] in ("+", "-") else "+"
        digits = "".join(char for char in text if char.isdigit())[-2:] or "0"
        digit_width = min(18.0, (display.width() - 34) / (2.1 if len(digits) > 1 else 1.35))
        digit_gap = max(2.0, digit_width * .10)
        digits_width = digit_width * len(digits) + digit_gap * (len(digits) - 1)
        group_width = 15 + 6 + digits_width
        # The plus glyph has less visual mass than a seven-segment digit;
        # compensate its optical weight so +0 reads geometrically centered.
        group_left = display.center().x() - group_width / 2 - 6
        digits_left = group_left + 20
        for index, digit in enumerate(digits):
            digit_area = QRectF(digits_left + index * (digit_width + digit_gap), display.top() + 2, digit_width, display.height() - 4)
            self._draw_digit(p, digit_area, digit)
        p.setPen(QPen(QColor('#F0D6A7'), 2.1, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        plus_center = QPointF(group_left + 7, display.center().y())
        p.drawLine(QPointF(plus_center.x()-4.5, plus_center.y()), QPointF(plus_center.x()+4.5, plus_center.y()))
        if sign != "-": p.drawLine(QPointF(plus_center.x(), plus_center.y()-4.5), QPointF(plus_center.x(), plus_center.y()+4.5))
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

class RotaryKnob(QSlider):
    """A QSlider with rotary studio-hardware presentation and normal slider signals."""

    MIN_ANGLE = 225.0       # -135° in the knob's 12-o'clock coordinate system
    MAX_ANGLE = 495.0       # +135°, continuing clockwise
    # Relative drag is the professional-plugin convention: this distance is
    # independent of knob diameter and lets a full range fit in one natural
    # wrist movement without needing to grab the control again.
    FULL_SCALE_DRAG_PIXELS = 150.0
    DRAG_RESISTANCE = 1.0 / FULL_SCALE_DRAG_PIXELS
    FINE_DRAG_MULTIPLIER = 0.20
    MAX_VELOCITY_GAIN = 1.35
    EDGE_RESISTANCE = 0.72
    EDGE_ZONE = 0.12
    _BODY_SOURCE_CACHE = {}
    _SCALED_BODY_CACHE = {}
    _STATIC_FRAME_CACHE = {}
    _ROTOR_FRAME_CACHE = {}
    valueCommitted = pyqtSignal(int)
    dragStarted = pyqtSignal()
    dragFinished = pyqtSignal()

    def __init__(self, accent: str, default_value: int, parent=None):
        super().__init__(Qt.Orientation.Horizontal, parent)
        self._accent = QColor(accent)
        self._default_value = default_value
        self._drag_origin = None
        self._drag_value = default_value
        # Pointer and logical value intentionally share one source of truth
        # while the user drags. Delayed smoothing here feels broken on a
        # realtime audio control; backend delivery is coalesced separately.
        self._target_value = float(default_value)
        self._render_value = float(default_value)
        self._last_drag_y = None
        self._drag_normalized_value = 0.0
        self._drag_clock = QElapsedTimer()
        self._end_stop_side = None
        self._end_stop_timer = QTimer(self)
        self._end_stop_timer.setSingleShot(True)
        self._end_stop_timer.timeout.connect(self._clear_end_stop_feedback)
        self._body_frame = QPixmap()
        self._background_frame = QPixmap()
        self._frame_directory = ""
        self.setFixedSize(126, 126)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setCursor(Qt.CursorShape.SizeVerCursor)

    def setValue(self, value):
        super().setValue(value)
        if self._drag_origin is None:
            self._target_value = float(self.value())
            self._render_value = float(self.value())
        self.update()

    def set_frame_directory(self, directory):
        """Load the original knob body; its live indicator remains vector drawn."""
        directory = str(directory)
        body = self._BODY_SOURCE_CACHE.get(directory)
        if body is None:
            body = QPixmap(f"{directory}/00.png")
            self._BODY_SOURCE_CACHE[directory] = body
        self._frame_directory = directory if not body.isNull() else ""
        self._body_frame = body
        # The cached frame covers the entire control surface. This avoids the
        # costly transparent-child propagation that otherwise repaints the
        # full ReferenceOverlay/chassis for every pointer frame.
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, False)
        self.update()

    def set_background_frame(self, background: QPixmap):
        """Install the exact static panel pixels behind this alpha PNG."""
        self._background_frame = QPixmap(background)
        self.setAttribute(
            Qt.WidgetAttribute.WA_OpaquePaintEvent,
            not self._body_frame.isNull() and not self._background_frame.isNull(),
        )
        self.update()

    def showEvent(self, event):
        super().showEvent(event)
        # QWidget polish during the parent window's first show can clear the
        # attribute set during construction. Restore it once the control is
        # actually in the backing store.
        if not self._body_frame.isNull() and not self._background_frame.isNull():
            self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, True)

    def _scaled_body_frame(self):
        if self._body_frame.isNull():
            return QPixmap()
        key = (self._frame_directory, self.width(), self.height())
        body = self._SCALED_BODY_CACHE.get(key)
        if body is None:
            body = self._body_frame.scaled(
                self.width(), self.height(), Qt.AspectRatioMode.IgnoreAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            self._SCALED_BODY_CACHE[key] = body
        return body

    def _static_frame(self):
        """Return the immutable rim/calibration layer for the current size.

        The moving reflection is painted separately.  The frame itself stays
        intact, keeping its engraved calibration marks sharply stationary.
        """
        key = (self._frame_directory, self.width(), self.height())
        frame = self._STATIC_FRAME_CACHE.get(key)
        if frame is None:
            frame = QPixmap(self.size())
            frame.fill(Qt.GlobalColor.transparent)
            painter = QPainter(frame)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            painter.drawPixmap(0, 0, self._scaled_body_frame())
            self._draw_frame_calibration(painter)
            painter.end()
            self._STATIC_FRAME_CACHE[key] = frame
        return frame

    def _rotor_frame(self):
        """Build an opaque, circular moving face from the supplied knob art."""
        key = (self._frame_directory, self.width(), self.height())
        rotor = self._ROTOR_FRAME_CACHE.get(key)
        if rotor is None:
            rotor = QPixmap(self.size())
            rotor.fill(Qt.GlobalColor.transparent)
            painter = QPainter(rotor)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            side = min(self.width(), self.height())
            center = QPointF(self.width() / 2.0, self.height() / 2.0)
            # This radius excludes the fixed engraved calibration marks while
            # retaining the reflective body that a user expects to turn.
            radius = side * .285
            face = QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor("#070807"))
            painter.drawEllipse(face)
            clip = QPainterPath()
            clip.addEllipse(face)
            painter.setClipPath(clip)
            painter.drawPixmap(0, 0, self._scaled_body_frame())
            painter.end()
            self._ROTOR_FRAME_CACHE[key] = rotor
        return rotor

    def _draw_rotating_frame_face(self, painter):
        """Turn the physical face while retaining the fixed chassis scale."""
        side = min(self.width(), self.height())
        press_offset = .8 if self._drag_origin is not None else 0.0
        center = QPointF(self.width() / 2.0, self.height() / 2.0 + press_offset)
        face_radius = side * .285
        rim_outer_radius = side * .395
        rim_inner_radius = side * .310
        painter.save()
        angle = -135.0 + self._normalized_value() * 270.0
        painter.translate(center)
        painter.rotate(angle)
        painter.translate(-center)
        painter.drawPixmap(0, 0, self._rotor_frame())
        painter.restore()

        # The real moving object includes the outer rotor rim, while the
        # calibration ticks beyond it remain fixed to the chassis.  A muted
        # conical metal grain rotates cleanly with the knob; it replaces the
        # previous scattered specular streaks.
        ring = QPainterPath()
        ring.setFillRule(Qt.FillRule.OddEvenFill)
        ring.addEllipse(QRectF(
            center.x() - rim_outer_radius, center.y() - rim_outer_radius,
            rim_outer_radius * 2, rim_outer_radius * 2,
        ))
        ring.addEllipse(QRectF(
            center.x() - rim_inner_radius, center.y() - rim_inner_radius,
            rim_inner_radius * 2, rim_inner_radius * 2,
        ))
        metal = QConicalGradient(center, -angle)
        metal.setColorAt(0.00, QColor(136, 109, 74, 42))
        metal.setColorAt(0.12, QColor(14, 15, 15, 92))
        metal.setColorAt(0.38, QColor(51, 44, 35, 36))
        metal.setColorAt(0.60, QColor(8, 9, 9, 98))
        metal.setColorAt(0.84, QColor(114, 91, 62, 32))
        metal.setColorAt(1.00, QColor(136, 109, 74, 42))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(metal)
        painter.drawPath(ring)
        # A single subtle engraved notch is a stable visual reference for the
        # user while dragging, rather than a bright decorative glare.
        notch_inner = self._point(center, rim_inner_radius + 1.5, angle)
        notch_outer = self._point(center, rim_outer_radius - 2.0, angle)
        painter.setPen(QPen(QColor(8, 8, 8, 155), 1.2,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(notch_inner, notch_outer)

    def _normalized_value(self, value=None):
        value = self._render_value if value is None else value
        span = max(1, self.maximum() - self.minimum())
        return max(0.0, min(1.0, (value - self.minimum()) / span))

    @property
    def target_value(self) -> float:
        """Latest logical value written by vertical drag; never rounded for paint."""
        return self._target_value

    @property
    def smoothed_render_value(self) -> float:
        """Low-pass filtered float consumed by the knob's renderer."""
        return self._render_value

    def _angle(self, value=None):
        """Single source of truth for pointer, LED arc and endpoint dot."""
        return self.MIN_ANGLE + (self.MAX_ANGLE - self.MIN_ANGLE) * self._normalized_value(value)

    @staticmethod
    def _point(center, radius, angle):
        a = radians(angle)
        # UI angles start at 12 o'clock and advance clockwise.  This maps the
        # 225°→495° range to the standard lower-left → upper arc → lower-right
        # hardware sweep, matching the QPainter drawArc conversion below.
        return QPointF(center.x() + radius * sin(a), center.y() - radius * cos(a))

    def _draw_ticks(self, painter, center, radius, active_angle):
        """Draw the fine engraved calibration ring outside the value arc."""
        for tick in range(37):
            angle = self.MIN_ANGLE + tick * (self.MAX_ANGLE - self.MIN_ANGLE) / 36
            major = tick % 6 == 0
            outer = self._point(center, radius + (3.0 if major else 1.8), angle)
            inner = self._point(center, radius - (4.5 if major else 2.2), angle)
            if angle <= active_angle + .1:
                color = QColor("#D6B783"); color.setAlpha(145 if major else 104)
            else:
                color = QColor(194, 173, 139, 84 if major else 52)
            painter.setPen(QPen(color, 1.0 if major else .75,
                                Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            painter.drawLine(outer, inner)

    def _draw_body(self, painter, body, active):
        center = body.center()
        # Multiple narrow rings create a machined rim without adding a box.
        painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(QColor(0, 0, 0, 135))
        painter.drawEllipse(body.translated(0, 3).adjusted(-2, -2, 2, 2))
        outer = QRadialGradient(center - QPointF(body.width() * .18, body.height() * .20), body.width() * .72)
        outer.setColorAt(0, QColor('#544535')); outer.setColorAt(.30, QColor('#28231D'))
        outer.setColorAt(.78, QColor('#0D0E0E')); outer.setColorAt(1, QColor('#030404'))
        painter.setPen(QPen(QColor('#846A49'), 1)); painter.setBrush(outer); painter.drawEllipse(body)
        bevel = body.adjusted(2.5, 2.5, -2.5, -2.5)
        painter.setPen(QPen(QColor(216, 195, 159, 105), 1)); painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(bevel)
        groove = body.adjusted(4.5, 4.5, -4.5, -4.5)
        painter.setPen(QPen(QColor('#010407'), 1.5)); painter.setBrush(QColor('#050B12')); painter.drawEllipse(groove)
        face = body.adjusted(5.5, 5.5, -5.5, -5.5)
        face_gradient = QRadialGradient(face.center() - QPointF(face.width() * .21, face.height() * .24), face.width() * .82)
        face_gradient.setColorAt(0, QColor('#544737')); face_gradient.setColorAt(.18, QColor('#30281F'))
        face_gradient.setColorAt(.60, QColor('#171512')); face_gradient.setColorAt(1, QColor('#060706'))
        painter.setPen(QPen(QColor(180, 151, 108, 80), 1)); painter.setBrush(face_gradient); painter.drawEllipse(face)
        # A restrained top-left metallic reflection replaces the old visible spokes.
        painter.setPen(QPen(QColor(218, 239, 250, 38), 1.1, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(face.adjusted(3, 3, -3, -3), 116 * 16, 64 * 16)
        pointer_color = QColor("#E7C691")
        painter.setPen(QPen(pointer_color, 2.0, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(self._point(center, face.width() * .12, active),
                         self._point(center, face.width() * .34, active))

    def _draw_frame_pointer(self, painter):
        """Draw the single live indicator over the original knob frame."""
        side = min(self.width(), self.height())
        press_offset = .8 if self._drag_origin is not None else 0.0
        center = QPointF(self.width() / 2.0, self.height() / 2.0 + press_offset)
        angle = self._angle()
        inner = self._point(center, side * .18, angle)
        outer = self._point(center, side * .295, angle)
        painter.setPen(QPen(QColor(0, 0, 0, 210), 3.6,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(inner, outer)
        painter.setPen(QPen(QColor("#E7C691"), 1.35,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(inner, outer)
        if self._end_stop_side is not None:
            end_angle = self.MIN_ANGLE if self._end_stop_side == "minimum" else self.MAX_ANGLE
            end_point = self._point(center, side * .445, end_angle)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor("#F4D497"))
            painter.drawEllipse(QRectF(end_point.x() - 2.2, end_point.y() - 2.2, 4.4, 4.4))

    def _clear_end_stop_feedback(self):
        self._end_stop_side = None
        self.update()

    def _show_end_stop_feedback(self, side: str):
        self._end_stop_side = side
        self._end_stop_timer.start(70)
        self.update()

    def _draw_frame_calibration(self, painter):
        """Render one restrained, high-contrast calibration ring for the frame."""
        side = min(self.width(), self.height())
        center = QPointF(self.width() / 2.0, self.height() / 2.0)
        # Twelve major detents with short secondary marks read like premium
        # machined hardware, rather than the previous sparse white scratches.
        for index in range(25):
            angle = self.MIN_ANGLE + index * (self.MAX_ANGLE - self.MIN_ANGLE) / 24
            major = index % 4 == 0
            outer = self._point(center, side * (.455 if major else .442), angle)
            inner = self._point(center, side * (.392 if major else .414), angle)
            color = QColor("#E7C691" if major else "#9A7C56")
            color.setAlpha(232 if major else 155)
            painter.setPen(QPen(color, 1.35 if major else .8,
                                Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            painter.drawLine(inner, outer)

    def paintEvent(self, event):
        if not self._body_frame.isNull():
            painter = QPainter(self)
            if not self._background_frame.isNull():
                painter.drawPixmap(0, 0, self._background_frame)
            painter.drawPixmap(0, 0, self._static_frame())
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            self._draw_rotating_frame_face(painter)
            self._draw_frame_pointer(painter)
            painter.end()
            return
        if self.property("reference_overlay"):
            return
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
        self._draw_body(p, body, active)
        p.end()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_origin = event.position().y()
            self._drag_value = self.value()
            self._target_value = float(self.value())
            self._render_value = float(self.value())
            self._drag_normalized_value = self._normalized_value(self.value())
            self._last_drag_y = event.position().y()
            self._drag_clock.start()
            # Keep receiving relative movement after the pointer leaves the
            # 100px artwork. Without an explicit grab, a long upward drag can
            # be truncated at the widget edge and wrongly require a second
            # click to complete the range.
            self.grabMouse()
            self.dragStarted.emit()
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._drag_origin is not None:
            current_y = event.position().y()
            delta_y = self._last_drag_y - current_y
            self._last_drag_y = current_y
            if delta_y:
                # Relative movement is deterministic: the same physical drag
                # always travels the same parameter distance regardless of
                # mouse-event cadence. Shift is a deliberate fine-control mode.
                resistance = self.DRAG_RESISTANCE
                if event.modifiers() & Qt.KeyboardModifier.ShiftModifier:
                    resistance *= self.FINE_DRAG_MULTIPLIER
                elapsed_ms = max(1, self._drag_clock.restart())
                pixels_per_ms = abs(delta_y) / elapsed_ms
                velocity_gain = min(
                    self.MAX_VELOCITY_GAIN,
                    1.0 + max(0.0, pixels_per_ms - .45) * .20,
                )
                delta_normalized = delta_y * resistance * velocity_gain
                # Add a small, physical resistance only while turning into an
                # end-stop. Turning away remains immediately responsive.
                toward_maximum = delta_normalized > 0
                distance_to_stop = (
                    1.0 - self._drag_normalized_value
                    if toward_maximum else self._drag_normalized_value
                )
                if distance_to_stop < self.EDGE_ZONE:
                    edge_blend = distance_to_stop / self.EDGE_ZONE
                    delta_normalized *= self.EDGE_RESISTANCE + (1.0 - self.EDGE_RESISTANCE) * edge_blend
                next_normalized = max(0.0, min(
                    1.0, self._drag_normalized_value + delta_normalized,
                ))
                if next_normalized != self._drag_normalized_value and next_normalized in (0.0, 1.0):
                    self._show_end_stop_feedback("minimum" if next_normalized == 0.0 else "maximum")
                self._drag_normalized_value = next_normalized
                self._target_value = self.minimum() + self._drag_normalized_value * (self.maximum() - self.minimum())
                self._render_value = self._target_value
                value = int(round(self._target_value))
                if value != self.value():
                    super().setValue(value)
                self.update()
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._drag_origin = None
        self._last_drag_y = None
        if QWidget.mouseGrabber() is self:
            self.releaseMouse()
        final_value = int(round(self._target_value))
        self._render_value = float(final_value)
        if final_value != self.value():
            super().setValue(final_value)
        self.update()
        self.valueCommitted.emit(final_value)
        self.dragFinished.emit()
        # Do not let QSlider reinterpret the release position and snap the
        # rotary control back to its original value.
        event.accept()

    def mouseDoubleClickEvent(self, event):
        self.setValue(self._default_value)
        event.accept()

    def wheelEvent(self, event):
        steps = event.angleDelta().y() / 120
        value = max(self.minimum(), min(self.maximum(), self.value() + round(steps * 2)))
        self._target_value = float(value)
        self._render_value = float(value)
        self.setValue(value)
        self.update()
        event.accept()
