"""First-run consent surface for live audio visualisation."""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QDialogButtonBox, QLabel, QPushButton, QVBoxLayout

from ui.analog_dialog_theme import ANALOG_DIALOG_QSS
from ui.window_chrome import AnalogDialog


class AudioPermissionsDialog(AnalogDialog):
    """Explains why native permission sheets are about to appear."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Âm thanh trực tiếp")
        self.setModal(True)
        self.setFixedWidth(440)
        self.setStyleSheet(ANALOG_DIALOG_QSS)

        root = QVBoxLayout(self)
        root.setContentsMargins(28, self.TITLE_HEIGHT + 20, 28, 24)
        root.setSpacing(12)

        title = QLabel("Bật âm thanh đầy đủ")
        title.setObjectName("DialogTitle")
        root.addWidget(title)
        body = QLabel(
            "AI‑Tune sẽ hiển thị nhịp âm thanh từ giọng hát và nhạc đang phát trên máy. "
            "Ở lần đầu, macOS hoặc Windows có thể hỏi quyền Microphone và System Audio."
        )
        body.setWordWrap(True)
        body.setObjectName("DialogCaption")
        root.addWidget(body)
        note = QLabel("Bạn chỉ cần bấm Cho phép trong hộp thoại của hệ điều hành.")
        note.setWordWrap(True)
        root.addWidget(note)

        enable = QPushButton("Bật âm thanh đầy đủ")
        enable.setDefault(True)
        enable.clicked.connect(self.accept)
        root.addWidget(enable)
        skip = QPushButton("Để sau")
        skip.clicked.connect(self.reject)
        root.addWidget(skip, alignment=Qt.AlignmentFlag.AlignRight)
