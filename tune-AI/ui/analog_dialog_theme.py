"""One visual language for every secondary THM surface.

The main Vocal Panel is a dark physical chassis.  Dialogs must feel like
pull-out rack drawers from that chassis, not unrelated desktop windows.
"""

from html import escape

from PyQt6.QtCore import Qt


ANALOG_DIALOG_QSS = """
QDialog {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1b1a18, stop:.18 #111211, stop:1 #090a09);
    color: #e8d9c2;
    border: 1px solid #8c765e;
    font-family: "Segoe UI", "Arial", sans-serif;
    font-size: 12px;
}
QLabel { color: #e8d9c2; background: transparent; font-size: 12px; font-weight: 600; }
QLabel#DialogTitle {
    color: #f0dcc0;
    font-size: 15px;
    font-weight: 700;
    letter-spacing: 1px;
}
QLabel#DialogCaption, QLabel#SettingsHelpLabel {
    color: #bfae96;
    font-size: 11px;
    font-weight: 500;
}
QLabel#SettingsValueLabel {
    background: #0a0b0a;
    color: #e8d9c2;
    border: 1px solid #5f503e;
    border-radius: 5px;
    padding: 6px 8px;
    min-height: 18px;
}
QLineEdit, QComboBox, QDoubleSpinBox {
    background: #080909;
    color: #f0dfc5;
    border: 1px solid #695845;
    border-radius: 6px;
    padding: 7px 10px;
    min-height: 22px;
    selection-background-color: #6a4827;
    selection-color: #fff4e2;
}
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {
    border: 1px solid #c89155;
    background: #11100e;
}
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 18px;
    background: #211c17;
    border-left: 1px solid #5f503e;
}
QComboBox::drop-down { border: none; width: 26px; }
QComboBox QAbstractItemView {
    background: #11110f;
    color: #eadac3;
    border: 1px solid #8c765e;
    outline: 0;
    padding: 4px;
    selection-background-color: #5a3d23;
    selection-color: #fff4e2;
}
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #3b3229, stop:.12 #241f1a, stop:1 #12110f);
    color: #eadac3;
    border: 1px solid #80684e;
    border-radius: 6px;
    padding: 7px 14px;
    min-height: 22px;
    font-size: 12px;
    font-weight: 700;
}
QPushButton:hover { border-color: #d5a466; background: #31271e; color: #fff1dc; }
QPushButton:pressed { background: #090909; padding-top: 9px; padding-bottom: 5px; }
QPushButton[destructive="true"] { color: #ffaaa0; border-color: #9e4c45; background: #2a1514; }
QPushButton#SettingsDangerButton { color: #ffaaa0; border-color: #9e4c45; background: #2a1514; }
QPushButton#SettingsDangerButton:hover { background: #3a1c1a; border-color: #d96d61; }
QGroupBox {
    color: #dec6a6;
    font-size: 12px;
    font-weight: 800;
    border: 1px solid #5d5143;
    border-radius: 7px;
    margin-top: 12px;
    padding: 10px 8px 8px 8px;
    background: rgba(0, 0, 0, 58);
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }
QTableWidget {
    background: #0b0c0c;
    alternate-background-color: #121310;
    color: #e9dac4;
    gridline-color: #302b24;
    border: 1px solid #6d5941;
    border-radius: 7px;
    selection-background-color: #533b24;
    selection-color: #fff2df;
    outline: 0;
}
QTableWidget::item { padding: 7px; border-bottom: 1px solid #24211c; }
QHeaderView::section {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #302a23,stop:1 #1a1815);
    color: #dfc9a9;
    padding: 8px;
    border: none;
    border-right: 1px solid #4c4033;
    border-bottom: 1px solid #7d6549;
    font-size: 12px;
    font-weight: 800;
}
QCheckBox { color: #e7d7c0; spacing: 7px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #80684e; border-radius: 3px; background: #090a09; }
QCheckBox::indicator:checked { background: #b77b35; border-color: #e0ad68; }
QWidget#SettingsWrap { background: transparent; }
QScrollBar:vertical { background: #0a0b0a; width: 10px; margin: 4px; }
QScrollBar::handle:vertical { background: #665441; min-height: 28px; border-radius: 4px; }
"""


ANALOG_MESSAGE_BOX_QSS = ANALOG_DIALOG_QSS + """
QMessageBox {
    border: 1px solid #7d6549;
    border-radius: 10px;
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #181613, stop:1 #090a09);
}
QMessageBox QLabel { min-width: 330px; font-size: 13px; font-weight: 600; padding: 8px 4px; }
QMessageBox QPushButton { min-width: 92px; }
"""


def apply_analog_dialog_theme(widget):
    """Apply the shared rack-drawer appearance to a dialog or message box."""
    widget.setStyleSheet(ANALOG_DIALOG_QSS)


def apply_analog_message_theme(message_box):
    """Keep information, warning and confirmation boxes visually consistent."""
    # QMessageBox otherwise asks macOS/Windows to paint a light native title
    # strip. Its semantic title is rendered inside the dark body instead.
    title = escape(str(message_box.property("analogTitle") or message_box.windowTitle()).strip())
    text = escape(message_box.text()).replace("\n", "<br>")
    if title:
        message_box.setText(f"<b>{title}</b><br><span>{text}</span>")
    message_box.setWindowFlags(message_box.windowFlags() | Qt.WindowType.FramelessWindowHint)
    message_box.setStyleSheet(ANALOG_MESSAGE_BOX_QSS)
