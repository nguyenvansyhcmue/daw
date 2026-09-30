from __future__ import annotations


from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QCheckBox,
    QDialog,
    QFileDialog,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QWidget,
)




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

    # Ép style riêng cho QMessageBox để không bị ăn theme tối của app chính
    # dẫn tới chữ đen trên nền đen/khó đọc như popup nhập/xuất dữ liệu.
    msg.setStyleSheet("""
        QMessageBox {
            background-color: #f8fafc;
            color: #0f172a;
        }
        QMessageBox QLabel {
            color: #0f172a;
            background: transparent;
            font-size: 13px;
            font-weight: 600;
            min-width: 420px;
        }
        QMessageBox QPushButton {
            background-color: #e8eefc;
            color: #0f172a;
            border: 1px solid #94a3b8;
            border-radius: 8px;
            padding: 7px 16px;
            font-size: 13px;
            font-weight: 800;
            min-width: 72px;
        }
        QMessageBox QPushButton:hover {
            background-color: #dbeafe;
        }
    """)

    # Việt hóa nút để nhân viên/khách dễ hiểu.
    try:
        yes_btn = msg.button(QMessageBox.StandardButton.Yes)
        if yes_btn:
            yes_btn.setText("Có")
        no_btn = msg.button(QMessageBox.StandardButton.No)
        if no_btn:
            no_btn.setText("Không")
        ok_btn = msg.button(QMessageBox.StandardButton.Ok)
        if ok_btn:
            ok_btn.setText("OK")
        cancel_btn = msg.button(QMessageBox.StandardButton.Cancel)
        if cancel_btn:
            cancel_btn.setText("Hủy")
    except Exception:
        pass

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

class SettingsDialog(QDialog):
    def __init__(self, controller, parent=None):
        super().__init__(parent)
        self.controller = controller

        self.setWindowTitle("Cài đặt")
        self.setModal(True)
        self.resize(560, 560)

        self.settings = self.controller.get_settings()
        self.autokey_cfg = self.controller.get_autokey_config()

        self._build_ui()
        self._load_data()
        self._apply_light_style()

    # =========================================================
    # UI
    # =========================================================

    def _build_ui(self):
        self.setObjectName("SettingsRoot")

        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        # =========================
        # Auto Key
        # =========================
        gb_autokey = QGroupBox("Auto Key")
        form_autokey = QFormLayout(gb_autokey)
        form_autokey.setSpacing(10)
        form_autokey.setContentsMargins(10, 14, 10, 10)

        self.lbl_target_window = QLabel("-")
        self.lbl_target_window.setObjectName("SettingsValueLabel")
        self.lbl_target_window.setWordWrap(True)

        self.btn_capture_target = QPushButton("Lấy cửa sổ (3s)")
        self.btn_capture_target.setObjectName("SettingsButton")
        self.btn_capture_target.clicked.connect(self.on_capture_target_window)

        row_target = QHBoxLayout()
        row_target.setContentsMargins(0, 0, 0, 0)
        row_target.setSpacing(6)
        row_target.addWidget(self.lbl_target_window, 1)
        row_target.addWidget(self.btn_capture_target)

        wrap_target = QWidget()
        wrap_target.setObjectName("SettingsWrap")
        wrap_target.setLayout(row_target)

        self.lbl_p1 = QLabel("-")
        self.lbl_p1.setObjectName("SettingsValueLabel")

        self.lbl_p2 = QLabel("-")
        self.lbl_p2.setObjectName("SettingsValueLabel")

        self.lbl_p3 = QLabel("-")
        self.lbl_p3.setObjectName("SettingsValueLabel")

        self.btn_capture_p1 = QPushButton("P1")
        self.btn_capture_p1.setObjectName("SettingsButton")

        self.btn_capture_p2 = QPushButton("P2")
        self.btn_capture_p2.setObjectName("SettingsButton")

        self.btn_capture_p3 = QPushButton("P3")
        self.btn_capture_p3.setObjectName("SettingsButton")

        self.btn_capture_p1.clicked.connect(lambda: self.on_capture_point("p1"))
        self.btn_capture_p2.clicked.connect(lambda: self.on_capture_point("p2"))
        self.btn_capture_p3.clicked.connect(lambda: self.on_capture_point("p3"))

        def make_row(lbl, btn):
            row = QHBoxLayout()
            row.setContentsMargins(0, 0, 0, 0)
            row.setSpacing(6)
            row.addWidget(lbl, 1)
            row.addWidget(btn)
            w = QWidget()
            w.setObjectName("SettingsWrap")
            w.setLayout(row)
            return w

        self.spin_delay_p23 = QDoubleSpinBox()
        self.spin_delay_p23.setObjectName("SettingsSpinBox")
        self.spin_delay_p23.setRange(0.1, 60.0)
        self.spin_delay_p23.setDecimals(1)
        self.spin_delay_p23.setSingleStep(0.5)
        self.spin_delay_p23.setSuffix(" s")

        self.chk_auto_run_on_play = QCheckBox("Auto chạy")
        self.chk_auto_run_on_play.setObjectName("SettingsCheckBox")

        self.lbl_help = QLabel(
            "Cách lấy tọa độ:\n"
            "- Chọn cửa sổ Auto-Key trước.\n"
            "- Bấm lấy P1/P2/P3.\n"
            "- Sau đó di chuột tới đúng vị trí trong cửa sổ Auto-Key và giữ yên khoảng 3 giây.\n"
            "- Tọa độ được lưu theo cửa sổ, nên bạn di chuyển bảng Auto-Key đi nơi khác vẫn dùng được."
        )
        self.lbl_help.setObjectName("SettingsHelpLabel")
        self.lbl_help.setWordWrap(True)

        form_autokey.addRow("Window:", wrap_target)
        form_autokey.addRow("P1:", make_row(self.lbl_p1, self.btn_capture_p1))
        form_autokey.addRow("P2:", make_row(self.lbl_p2, self.btn_capture_p2))
        form_autokey.addRow("P3:", make_row(self.lbl_p3, self.btn_capture_p3))
        form_autokey.addRow("Delay P2→P3:", self.spin_delay_p23)
        form_autokey.addRow("", self.chk_auto_run_on_play)
        form_autokey.addRow("", self.lbl_help)


        # =========================
        # Maintenance
        # =========================
        gb_maintenance = QGroupBox("Bảo trì")
        form_maintenance = QFormLayout(gb_maintenance)
        form_maintenance.setSpacing(10)
        form_maintenance.setContentsMargins(10, 14, 10, 10)

        self.btn_update_ytdlp = QPushButton("Cập nhật")
        self.btn_update_ytdlp.setObjectName("SettingsButton")
        self.btn_update_ytdlp.clicked.connect(self.on_update_ytdlp)

        form_maintenance.addRow("YouTube:", self.btn_update_ytdlp)

        self.edt_cookies_path = QLineEdit()
        self.edt_cookies_path.setPlaceholderText("Chưa chọn — để trống nếu không dùng cookies")
        self.btn_browse_cookies = QPushButton("Chọn file…")
        self.btn_browse_cookies.setObjectName("SettingsButton")
        self.btn_browse_cookies.clicked.connect(self._browse_cookies_file)

        row_cookies = QHBoxLayout()
        row_cookies.setContentsMargins(0, 0, 0, 0)
        row_cookies.setSpacing(6)
        row_cookies.addWidget(self.edt_cookies_path)
        row_cookies.addWidget(self.btn_browse_cookies)

        wrap_cookies = QWidget()
        wrap_cookies.setObjectName("SettingsWrap")
        wrap_cookies.setLayout(row_cookies)

        form_maintenance.addRow("Cookies YouTube:", wrap_cookies)

        # =========================
        # Data Transfer
        # =========================
        gb_data = QGroupBox("Dữ liệu bài hát")
        form_data = QFormLayout(gb_data)
        form_data.setSpacing(10)
        form_data.setContentsMargins(10, 14, 10, 10)

        self.btn_export_data = QPushButton("Xuất dữ liệu")
        self.btn_export_data.setObjectName("SettingsButton")
        self.btn_export_data.clicked.connect(self.on_export_data)

        self.btn_import_data = QPushButton("Nhập dữ liệu")
        self.btn_import_data.setObjectName("SettingsButton")
        self.btn_import_data.clicked.connect(self.on_import_data)

        self.btn_clear_cache = QPushButton("Xóa cache test")
        self.btn_clear_cache.setObjectName("SettingsDangerButton")
        self.btn_clear_cache.clicked.connect(self.on_clear_cache_test)

        row_data = QHBoxLayout()
        row_data.setContentsMargins(0, 0, 0, 0)
        row_data.setSpacing(8)
        row_data.addWidget(self.btn_export_data)
        row_data.addWidget(self.btn_import_data)
        row_data.addWidget(self.btn_clear_cache)

        wrap_data = QWidget()
        wrap_data.setObjectName("SettingsWrap")
        wrap_data.setLayout(row_data)

        form_data.addRow("Cache/Tone:", wrap_data)

        # Label ẩn để tương thích hàm cập nhật yt-dlp, không hiển thị trên UI.
        self.lbl_update_ytdlp = QLabel("")
        self.lbl_update_ytdlp.setVisible(False)

        # =========================
        # Buttons
        # =========================
        row_btn = QHBoxLayout()
        row_btn.setContentsMargins(0, 0, 0, 0)
        row_btn.setSpacing(8)

        self.btn_save = QPushButton("Lưu")
        self.btn_save.setObjectName("SettingsButton")

        self.btn_cancel = QPushButton("Hủy")
        self.btn_cancel.setObjectName("SettingsButton")

        self.btn_save.clicked.connect(self.on_save)
        self.btn_cancel.clicked.connect(self.reject)

        row_btn.addStretch()
        row_btn.addWidget(self.btn_save)
        row_btn.addWidget(self.btn_cancel)

        root.addWidget(gb_autokey)
        root.addWidget(gb_maintenance)
        root.addWidget(gb_data)
        root.addStretch()
        root.addLayout(row_btn)

    # =========================================================
    # STYLE
    # =========================================================

    def _apply_light_style(self):
        self.setStyleSheet("""
            /* ROOT */
            #SettingsRoot {
                background: #f4f6fb;
            }

            #SettingsRoot QGroupBox {
                color: #111827;
                font-size: 12px;
                font-weight: 700;
                border: 1px solid #d7dce5;
                border-radius: 8px;
                margin-top: 8px;
                background: #ffffff;
                padding-top: 6px;
            }

            #SettingsRoot QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
                background: transparent;
            }

            #SettingsRoot QLabel {
                color: #111827;
                background: transparent;
                font-size: 12px;
            }

            #SettingsRoot QLabel#SettingsValueLabel {
                background: #ffffff;
                color: #111827;
                border: 1px solid #cfd7e6;
                border-radius: 6px;
                padding: 4px 6px;
                min-height: 18px;
            }

            #SettingsRoot QLabel#SettingsHelpLabel {
                color: #334155;
                background: transparent;
                line-height: 1.4;
            }

            #SettingsRoot QLineEdit#SettingsLineEdit,
            #SettingsRoot QDoubleSpinBox#SettingsSpinBox {
                background: #ffffff;
                color: #111827;
                border: 1px solid #cfd7e6;
                border-radius: 6px;
                padding: 4px 6px;
                min-height: 18px;
                font-size: 12px;
                selection-background-color: #c7ddff;
                selection-color: #111827;
            }

            #SettingsRoot QWidget#SettingsWrap {
                background: transparent;
            }

            #SettingsRoot QPushButton#SettingsButton {
                background: #e8eef9;
                color: #111827;
                border: 1px solid #c7d2e3;
                border-radius: 6px;
                padding: 5px 10px;
                font-size: 12px;
                font-weight: 600;
                min-height: 20px;
            }

            #SettingsRoot QPushButton#SettingsButton:hover {
                background: #dbe7ff;
            }

            #SettingsRoot QPushButton#SettingsButton:pressed {
                background: #cfe0ff;
            }

            #SettingsRoot QCheckBox#SettingsCheckBox {
                color: #111827;
                background: transparent;
                font-size: 12px;
                font-weight: 600;
                spacing: 6px;
            }

            #SettingsRoot QCheckBox#SettingsCheckBox::indicator {
                width: 16px;
                height: 16px;
            }
        """)

    # =========================================================
    # POPUP SÁNG
    # =========================================================

    def _show_info(self, title: str, text: str):
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.NoIcon)
        msg.setWindowTitle(title)
        msg.setText(text)
        msg.setStandardButtons(QMessageBox.StandardButton.Ok)
        msg.setStyleSheet("""
            QMessageBox {
                background: #f8fafc;
            }
            QMessageBox QLabel {
                color: #111827;
                font-size: 13px;
                min-width: 220px;
            }
            QMessageBox QPushButton {
                background: #e8eef9;
                color: #111827;
                border: 1px solid #c7d2e3;
                border-radius: 6px;
                padding: 6px 14px;
                min-width: 70px;
                font-weight: 600;
            }
            QMessageBox QPushButton:hover {
                background: #dbe7ff;
            }
        """)
        msg.exec()

    def _show_error(self, title: str, text: str):
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.NoIcon)
        msg.setWindowTitle(title)
        msg.setText(text)
        msg.setStandardButtons(QMessageBox.StandardButton.Ok)
        msg.setStyleSheet("""
            QMessageBox {
                background: #fff7f7;
            }
            QMessageBox QLabel {
                color: #7f1d1d;
                font-size: 13px;
                min-width: 240px;
            }
            QMessageBox QPushButton {
                background: #fee2e2;
                color: #7f1d1d;
                border: 1px solid #fecaca;
                border-radius: 6px;
                padding: 6px 14px;
                min-width: 70px;
                font-weight: 700;
            }
            QMessageBox QPushButton:hover {
                background: #fecaca;
            }
        """)
        msg.exec()

    # =========================================================
    # LOAD
    # =========================================================

    def _load_data(self):
        self.edt_cookies_path.setText(str(self.settings.get("ytdlp_cookies_file", "")))

        title = str(self.autokey_cfg.get("target_title", "") or "")
        cls = str(self.autokey_cfg.get("target_class", "") or "")
        hint = str(self.autokey_cfg.get("target_hint", "") or "")

        if title or cls:
            self.lbl_target_window.setText(f"{title} | {cls} | {hint}".strip(" |"))
        else:
            self.lbl_target_window.setText("-")

        self._set_point_label(self.lbl_p1, self.autokey_cfg.get("p1"))
        self._set_point_label(self.lbl_p2, self.autokey_cfg.get("p2"))
        self._set_point_label(self.lbl_p3, self.autokey_cfg.get("p3"))

        self.spin_delay_p23.setValue(float(self.autokey_cfg.get("delay_23", 8.0) or 8.0))
        self.chk_auto_run_on_play.setChecked(bool(self.autokey_cfg.get("auto_run_on_play", False)))

    def _set_point_label(self, label: QLabel, point_data):
        if isinstance(point_data, dict) and point_data.get("x") is not None and point_data.get("y") is not None:
            label.setText(f"({int(point_data['x'])}, {int(point_data['y'])})")
        else:
            label.setText("-")

    # =========================================================
    # EVENTS
    # =========================================================

    def on_capture_target_window(self):
        self._show_info(
            "Lấy cửa sổ mục tiêu",
            "Trong 3 giây tới, hãy đưa chuột vào cửa sổ Auto-Key và giữ yên."
        )
        self.hide()
        QTimer.singleShot(3200, self._do_capture_target_window)

    def _do_capture_target_window(self):
        try:
            data = self.controller.capture_target_window_from_cursor()
            title = str(data.get("target_title", "") or "")
            cls = str(data.get("target_class", "") or "")
            hint = str(data.get("target_hint", "") or "")
            self.lbl_target_window.setText(f"{title} | {cls} | {hint}".strip(" |"))
            self.show()
            self.raise_()
            self.activateWindow()
            self._show_info("Thành công", "Đã lấy cửa sổ Auto-Key.")
        except Exception as e:
            self.show()
            self.raise_()
            self.activateWindow()
            self._show_error("Lỗi", str(e))

    def on_capture_point(self, point_name: str):
        self._show_info(
            f"Lấy {point_name.upper()}",
            f"Trong 3 giây tới, hãy di chuột tới vị trí {point_name.upper()} trong cửa sổ Auto-Key và giữ yên."
        )
        self.hide()
        QTimer.singleShot(3200, lambda: self._do_capture_point(point_name))

    def _do_capture_point(self, point_name: str):
        try:
            point = self.controller.capture_autokey_point(point_name)

            if point_name == "p1":
                self._set_point_label(self.lbl_p1, point)
            elif point_name == "p2":
                self._set_point_label(self.lbl_p2, point)
            elif point_name == "p3":
                self._set_point_label(self.lbl_p3, point)

            self.show()
            self.raise_()
            self.activateWindow()
            self._show_info("Thành công", f"Đã lấy {point_name.upper()}.")
        except Exception as e:
            self.show()
            self.raise_()
            self.activateWindow()
            self._show_error("Lỗi", str(e))


    def on_update_ytdlp(self):
        self.btn_update_ytdlp.setEnabled(False)
        self.btn_update_ytdlp.setText("Đang cập nhật...")
        self.lbl_update_ytdlp.setText("Đang cập nhật yt-dlp, vui lòng chờ...")

        try:
            result = self.controller.update_ytdlp()
            self.lbl_update_ytdlp.setText("Đã cập nhật yt-dlp. Anh nên tắt app và mở lại để chắc chắn dùng bản mới.")
            self._show_info(
                "Cập nhật xong",
                "Đã cập nhật yt-dlp thành công.\nAnh nên tắt app và mở lại trước khi dò tone tiếp."
            )
        except Exception as e:
            self.lbl_update_ytdlp.setText("Cập nhật yt-dlp thất bại.")
            self._show_error("Lỗi cập nhật yt-dlp", str(e))
        finally:
            self.btn_update_ytdlp.setEnabled(True)
            self.btn_update_ytdlp.setText("Cập nhật")

    def _browse_cookies_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Chọn file cookies cho yt-dlp",
            "",
            "Cookies files (*.txt);;All files (*.*)",
        )
        if path:
            self.edt_cookies_path.setText(path)

    def on_export_data(self):
        try:
            default_name = self.controller.build_default_export_filename()
        except Exception:
            default_name = "THM_TONE_DATA.json"

        path, _ = QFileDialog.getSaveFileName(
            self,
            "Xuất dữ liệu bài hát / tone",
            default_name,
            "THM Tone Data (*.json);;JSON Files (*.json);;All Files (*)"
        )
        if not path:
            return

        try:
            result = self.controller.export_tone_data(path)
            self._show_info(
                "Xuất dữ liệu thành công",
                "Đã xuất dữ liệu.\n\n"
                f"File: {result.get('file_path', '')}\n"
                f"Tone cache: {result.get('tone_cache_count', 0)} bài\n"
                f"Đã xác nhận tay: {result.get('manual_verified_count', 0)} bài\n"
                f"Danh sách bài hát: {result.get('songs_count', 0)} bài\n"
                f"Lịch sử sửa sai: {result.get('correction_history_count', 0)} dòng"
            )
        except Exception as e:
            self._show_error("Lỗi xuất dữ liệu", str(e))

    def on_import_data(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Nhập dữ liệu bài hát / tone",
            "",
            "THM Tone Data (*.json);;JSON Files (*.json);;All Files (*)"
        )
        if not path:
            return

        confirm = QMessageBox.question(
            self,
            "Xác nhận nhập dữ liệu",
            "App sẽ nhập và gộp dữ liệu từ file này vào máy hiện tại.\n"
            "Dữ liệu cũ sẽ được backup tự động trước khi nhập.\n\n"
            "Anh có muốn tiếp tục không?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return

        try:
            result = self.controller.import_tone_data(path)
            self._show_info(
                "Nhập dữ liệu thành công",
                "Đã nhập và gộp dữ liệu.\n\n"
                f"Tone cache: +{result.get('tone_cache_added', 0)} mới, "
                f"{result.get('tone_cache_updated', 0)} cập nhật, "
                f"{result.get('tone_cache_skipped', 0)} bỏ qua\n"
                f"Danh sách bài hát: +{result.get('songs_added', 0)} mới, "
                f"{result.get('songs_updated', 0)} cập nhật, "
                f"{result.get('songs_skipped', 0)} bỏ qua\n"
                f"Lịch sử sửa sai: +{result.get('correction_history_added', 0)} mới, "
                f"{result.get('correction_history_updated', 0)} cập nhật, "
                f"{result.get('correction_history_skipped', 0)} bỏ qua\n\n"
                "Nên tắt app và mở lại để danh sách bài hát refresh đầy đủ."
            )
        except Exception as e:
            self._show_error("Lỗi nhập dữ liệu", str(e))

    def on_clear_cache_test(self):
        confirm = QMessageBox.question(
            self,
            "Xác nhận xóa cache test",
            "Thao tác này sẽ xóa cache tone để anh test lại từ đầu:\n"
            "- tone_cache.json\n"
            "- correction_history.json\n"
            "- kết quả phân tích lên tone cuối bài tạm thời\n\n"
            "Không xóa D.S BÀI HÁT / songs.json.\n"
            "App sẽ tự backup trước khi xóa.\n\n"
            "Anh có chắc muốn xóa cache test không?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return

        try:
            result = self.controller.clear_tone_cache_for_testing(make_backup=True)
            self._show_info(
                "Đã xóa cache test",
                "Đã xóa cache để test lại sạch.\n\n"
                f"Tone cache đã xóa: {result.get('tone_cache_count', 0)} bài\n"
                f"Lịch sử sửa sai đã xóa: {result.get('correction_history_count', 0)} dòng\n\n"
                "D.S BÀI HÁT không bị xóa.\n"
                "Nên tắt app và mở lại trước khi test lại bài cũ."
            )
        except Exception as e:
            self._show_error("Lỗi xóa cache", str(e))

    def on_save(self):
        payload = {
            "ytdlp_cookies_file": self.edt_cookies_path.text().strip(),
            "autokey": {
                "delay_23": float(self.spin_delay_p23.value()),
                "auto_run_on_play": self.chk_auto_run_on_play.isChecked(),
            }
        }

        self.controller.save_settings_payload(payload)
        self._show_info("Đã lưu", "Cài đặt đã được lưu.")
        self.accept()
