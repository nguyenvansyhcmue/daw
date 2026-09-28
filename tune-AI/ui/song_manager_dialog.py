from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

SCALE_NAMES = ["--", "Major", "Minor"]


# =========================================================
# SILENT MESSAGE BOX PATCH
# =========================================================
# App dùng để hát/live nên tất cả popup phải im lặng, không phát âm thanh Windows.
# QMessageBox.Information / Warning / Critical thường kích hoạt tiếng "ding".
# Bản patch này ép các popup tĩnh dùng NoIcon để không gây âm thanh.

def _thm_silent_message_box(parent, title, text, buttons=None, default_button=None):
    msg = QMessageBox(parent)
    msg.setIcon(QMessageBox.Icon.NoIcon)
    msg.setWindowTitle(str(title or ""))
    msg.setText(str(text or ""))
    msg.setStandardButtons(buttons or QMessageBox.StandardButton.Ok)
    if default_button is not None:
        try:
            msg.setDefaultButton(default_button)
        except Exception:
            pass
    return msg.exec()


def _thm_silent_information(parent, title, text, *args, **kwargs):
    buttons = args[0] if len(args) >= 1 else kwargs.get("buttons", QMessageBox.StandardButton.Ok)
    default_button = args[1] if len(args) >= 2 else kwargs.get("defaultButton", None)
    return _thm_silent_message_box(parent, title, text, buttons, default_button)


def _thm_silent_warning(parent, title, text, *args, **kwargs):
    buttons = args[0] if len(args) >= 1 else kwargs.get("buttons", QMessageBox.StandardButton.Ok)
    default_button = args[1] if len(args) >= 2 else kwargs.get("defaultButton", None)
    return _thm_silent_message_box(parent, title, text, buttons, default_button)


def _thm_silent_critical(parent, title, text, *args, **kwargs):
    buttons = args[0] if len(args) >= 1 else kwargs.get("buttons", QMessageBox.StandardButton.Ok)
    default_button = args[1] if len(args) >= 2 else kwargs.get("defaultButton", None)
    return _thm_silent_message_box(parent, title, text, buttons, default_button)


def _thm_silent_question(parent, title, text, buttons=None, defaultButton=None, *args, **kwargs):
    if buttons is None:
        buttons = kwargs.get(
            "buttons",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
    if defaultButton is None:
        defaultButton = kwargs.get("defaultButton", QMessageBox.StandardButton.No)
    return _thm_silent_message_box(parent, title, text, buttons, defaultButton)


QMessageBox.information = staticmethod(_thm_silent_information)
QMessageBox.warning = staticmethod(_thm_silent_warning)
QMessageBox.critical = staticmethod(_thm_silent_critical)
QMessageBox.question = staticmethod(_thm_silent_question)



class SongEditDialog(QDialog):
    def __init__(self, controller, parent=None, song_data=None):
        super().__init__(parent)
        self.controller = controller
        self.song_data = song_data or {}

        self.setWindowTitle("Thông tin bài hát")
        self.setModal(True)
        self.resize(760, 500)

        self._build_ui()
        self._load_data()

    def _build_ui(self):
        self.setStyleSheet("""
            QDialog {
                background: #f7f9fc;
            }
            QLabel {
                color: #111827;
                font-size: 15px;
                font-weight: 700;
                background: transparent;
            }
            QLineEdit, QComboBox {
                background: #ffffff;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 10px;
                padding: 10px 12px;
                font-size: 15px;
                min-height: 24px;
            }
            QPushButton {
                background: #e9eef8;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 10px;
                padding: 10px 18px;
                font-size: 15px;
                font-weight: 800;
            }
            QPushButton:hover {
                background: #dbe7ff;
            }
        """)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(18)

        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        form.setFormAlignment(Qt.AlignmentFlag.AlignTop)
        form.setHorizontalSpacing(18)
        form.setVerticalSpacing(16)

        self.edt_title = QLineEdit()
        self.edt_youtube = QLineEdit()

        self.cbo_scale = QComboBox()
        self.cbo_scale.addItems(["Major", "Minor"])
        self.cbo_scale.currentTextChanged.connect(self.on_scale_changed)

        self.cbo_key = QComboBox()

        self.edt_raise_time = QLineEdit("00:00")

        self.cbo_end_scale = QComboBox()
        self.cbo_end_scale.addItems(SCALE_NAMES)
        self.cbo_end_scale.currentTextChanged.connect(self.on_end_scale_changed)

        self.cbo_end_key = QComboBox()

        form.addRow("Tên bài hát:", self.edt_title)
        form.addRow("Link YouTube:", self.edt_youtube)
        form.addRow("Scale:", self.cbo_scale)
        form.addRow("Key:", self.cbo_key)
        form.addRow("Thời gian lên tone:", self.edt_raise_time)
        form.addRow("Scale lên tone cuối:", self.cbo_end_scale)
        form.addRow("Key lên tone cuối:", self.cbo_end_key)

        layout.addLayout(form)

        btn_row = QHBoxLayout()
        self.btn_save = QPushButton("Lưu")
        self.btn_cancel = QPushButton("Hủy")
        self.btn_save.clicked.connect(self.accept)
        self.btn_cancel.clicked.connect(self.reject)
        btn_row.addStretch()
        btn_row.addWidget(self.btn_save)
        btn_row.addWidget(self.btn_cancel)
        layout.addLayout(btn_row)

    def _reload_key_combo(self, combo: QComboBox, scale_name: str, selected_key: str = ""):
        combo.blockSignals(True)
        combo.clear()

        if scale_name == "--":
            combo.addItem("--")
            combo.setCurrentText("--")
            combo.blockSignals(False)
            return

        options = self.controller.get_key_options_for_scale(scale_name)
        combo.addItems(options)

        normalized = self.controller.normalize_key_for_scale(selected_key or options[0], scale_name)
        if normalized in options:
            combo.setCurrentText(normalized)
        else:
            combo.setCurrentIndex(0)

        combo.blockSignals(False)

    def _load_data(self):
        scale = str(self.song_data.get("scale", "Major"))
        end_scale = str(self.song_data.get("end_scale", "--"))
        key = str(self.song_data.get("key", "C"))
        end_key = str(self.song_data.get("end_key", "--"))

        self.edt_title.setText(str(self.song_data.get("title", "")))
        self.edt_youtube.setText(str(self.song_data.get("youtube_url", "")))
        self.edt_raise_time.setText(str(self.song_data.get("raise_time", "00:00")))

        if scale in ["Major", "Minor"]:
            self.cbo_scale.setCurrentText(scale)

        if end_scale in SCALE_NAMES:
            self.cbo_end_scale.setCurrentText(end_scale)
        else:
            self.cbo_end_scale.setCurrentText("--")

        self._reload_key_combo(self.cbo_key, self.cbo_scale.currentText(), key)
        self._reload_key_combo(self.cbo_end_key, self.cbo_end_scale.currentText(), end_key)

    def on_scale_changed(self, scale_name: str):
        current_key = self.cbo_key.currentText()
        self._reload_key_combo(self.cbo_key, scale_name, current_key)

    def on_end_scale_changed(self, scale_name: str):
        current_key = self.cbo_end_key.currentText()
        self._reload_key_combo(self.cbo_end_key, scale_name, current_key)

    def get_data(self):
        data = dict(self.song_data)
        data["title"] = self.edt_title.text().strip()
        data["youtube_url"] = self.edt_youtube.text().strip()
        data["scale"] = self.cbo_scale.currentText()
        data["key"] = self.cbo_key.currentText()
        data["raise_time"] = self.edt_raise_time.text().strip() or "00:00"
        data["end_scale"] = self.cbo_end_scale.currentText()
        data["end_key"] = self.cbo_end_key.currentText()
        data["tone_label"] = f"{data['key']} {data['scale']}"
        return data


class SongManagerDialog(QDialog):
    def __init__(self, controller, apply_song_callback, parent=None):
        super().__init__(parent)
        self.controller = controller
        self.apply_song_callback = apply_song_callback

        self.setWindowTitle("Danh sách bài hát")
        self.resize(1320, 760)
        self.setModal(True)

        self._build_ui()
        self.load_table()

    def _build_ui(self):
        self.setStyleSheet("""
            QDialog {
                background: #f7f9fc;
            }
            QLabel {
                color: #111827;
                font-size: 15px;
                font-weight: 800;
                background: transparent;
            }
            QLineEdit {
                background: #ffffff;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 10px;
                padding: 10px 12px;
                font-size: 15px;
                min-height: 22px;
            }
            QPushButton {
                background: #e9eef8;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 10px;
                padding: 10px 16px;
                font-size: 15px;
                font-weight: 800;
                min-height: 24px;
            }
            QPushButton:hover {
                background: #dbe7ff;
            }
            QTableWidget {
                background: #ffffff;
                color: #111827;
                gridline-color: #d9e1ec;
                font-size: 15px;
                selection-background-color: #dbe7ff;
                selection-color: #111827;
                border: 1px solid #d9e1ec;
                border-radius: 10px;
            }
            QTableWidget::item {
                padding: 8px;
                background: #ffffff;
                color: #111827;
            }
            QHeaderView::section {
                background: #e9eef8;
                color: #111827;
                padding: 10px;
                font-size: 15px;
                font-weight: 900;
                border: none;
                border-right: 1px solid #d9e1ec;
                border-bottom: 1px solid #d9e1ec;
            }
        """)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 18, 20, 18)
        layout.setSpacing(14)

        top_row = QHBoxLayout()
        self.edt_search = QLineEdit()
        self.edt_search.setPlaceholderText("Tìm kiếm tên bài, key, scale, link, video id...")
        self.btn_search = QPushButton("Tìm kiếm")
        self.btn_add = QPushButton("Thêm mới")
        self.btn_edit = QPushButton("Sửa")
        self.btn_delete = QPushButton("Xóa")

        self.btn_search.clicked.connect(self.on_search)
        self.btn_add.clicked.connect(self.on_add)
        self.btn_edit.clicked.connect(self.on_edit)
        self.btn_delete.clicked.connect(self.on_delete)

        top_row.addWidget(QLabel("Tìm kiếm:"))
        top_row.addWidget(self.edt_search, 1)
        top_row.addWidget(self.btn_search)
        top_row.addWidget(self.btn_add)
        top_row.addWidget(self.btn_edit)
        top_row.addWidget(self.btn_delete)
        layout.addLayout(top_row)

        self.table = QTableWidget()
        self.table.setColumnCount(9)
        self.table.setHorizontalHeaderLabels([
            "Tên bài hát",
            "Link YouTube",
            "Hát",
            "Key",
            "Scale",
            "Thời gian lên tone",
            "Key cuối",
            "Scale cuối",
            "Video ID",
        ])

        self.table.verticalHeader().setVisible(False)
        self.table.setShowGrid(True)
        self.table.setAlternatingRowColors(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setWordWrap(True)
        self.table.verticalHeader().setDefaultSectionSize(70)

        header = self.table.horizontalHeader()
        header.setStretchLastSection(False)
        header.setSectionResizeMode(QHeaderView.ResizeMode.Interactive)

        layout.addWidget(self.table)

        bottom_row = QHBoxLayout()
        self.btn_close = QPushButton("Đóng")
        self.btn_close.clicked.connect(self.reject)
        bottom_row.addStretch()
        bottom_row.addWidget(self.btn_close)
        layout.addLayout(bottom_row)

    def _apply_column_widths(self):
        self.table.setColumnWidth(0, 330)
        self.table.setColumnWidth(1, 250)
        self.table.setColumnWidth(2, 110)
        self.table.setColumnWidth(3, 80)
        self.table.setColumnWidth(4, 90)
        self.table.setColumnWidth(5, 150)
        self.table.setColumnWidth(6, 95)
        self.table.setColumnWidth(7, 100)
        self.table.setColumnWidth(8, 140)

    def _make_item(self, text: str, center: bool = False):
        item = QTableWidgetItem(str(text))
        if center:
            item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
        else:
            item.setTextAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        return item

    def load_table(self, keyword: str = ""):
        songs = self.controller.list_songs(keyword)
        self.table.setRowCount(len(songs))

        for row, song in enumerate(songs):
            title_item = self._make_item(song.get("title", ""))
            title_item.setData(Qt.ItemDataRole.UserRole, song.get("id"))
            self.table.setItem(row, 0, title_item)

            self.table.setItem(row, 1, self._make_item(song.get("youtube_url", "")))
            self.table.setItem(row, 3, self._make_item(song.get("key", ""), center=True))
            self.table.setItem(row, 4, self._make_item(song.get("scale", ""), center=True))
            self.table.setItem(row, 5, self._make_item(song.get("raise_time", "00:00"), center=True))
            self.table.setItem(row, 6, self._make_item(song.get("end_key", ""), center=True))
            self.table.setItem(row, 7, self._make_item(song.get("end_scale", ""), center=True))
            self.table.setItem(row, 8, self._make_item(song.get("youtube_video_id", ""), center=True))

            btn_widget = QWidget()
            btn_widget.setStyleSheet("background: transparent;")
            btn_layout = QHBoxLayout(btn_widget)
            btn_layout.setContentsMargins(8, 8, 8, 8)
            btn_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)

            btn_sing = QPushButton("HÁT")
            btn_sing.setMinimumWidth(80)
            btn_sing.clicked.connect(lambda _, sid=song.get("id"): self.on_sing(sid))
            btn_layout.addWidget(btn_sing)

            self.table.setCellWidget(row, 2, btn_widget)

        self._apply_column_widths()

    def _get_selected_song_id(self):
        current_row = self.table.currentRow()
        if current_row < 0:
            return None
        item = self.table.item(current_row, 0)
        if not item:
            return None
        return item.data(Qt.ItemDataRole.UserRole)

    def on_search(self):
        self.load_table(self.edt_search.text().strip())

    def on_add(self):
        dlg = SongEditDialog(self.controller, self)
        if dlg.exec():
            data = dlg.get_data()
            if not data["title"]:
                QMessageBox.warning(self, "Thiếu dữ liệu", "Bạn chưa nhập tên bài hát.")
                return
            self.controller.save_song(data)
            self.load_table(self.edt_search.text().strip())
            QMessageBox.information(self, "Thành công", "Bài hát đã được lưu.")

    def on_edit(self):
        song_id = self._get_selected_song_id()
        if not song_id:
            QMessageBox.warning(self, "Chưa chọn", "Bạn chưa chọn bài hát để sửa.")
            return

        song = self.controller.get_song(song_id)
        if not song:
            QMessageBox.warning(self, "Lỗi", "Không tìm thấy bài hát.")
            return

        dlg = SongEditDialog(self.controller, self, song)
        if dlg.exec():
            data = dlg.get_data()
            data["id"] = song_id
            data["youtube_video_id"] = song.get("youtube_video_id", "")
            self.controller.save_song(data)
            self.load_table(self.edt_search.text().strip())
            QMessageBox.information(self, "Thành công", "Bài hát đã được cập nhật.")

    def on_delete(self):
        song_id = self._get_selected_song_id()
        if not song_id:
            QMessageBox.warning(self, "Chưa chọn", "Bạn chưa chọn bài hát để xóa.")
            return

        reply = QMessageBox.question(
            self,
            "Xóa bài hát",
            "Bạn có chắc muốn xóa bài hát này không?"
        )
        if reply == QMessageBox.StandardButton.Yes:
            self.controller.delete_song(song_id)
            self.load_table(self.edt_search.text().strip())
            QMessageBox.information(self, "Đã xóa", "Bài hát đã được xóa.")

    def on_sing(self, song_id: str):
        song = self.controller.get_song(song_id)
        if not song:
            QMessageBox.warning(self, "Lỗi", "Không tìm thấy bài hát.")
            return

        self.apply_song_callback(song)
        self.accept()