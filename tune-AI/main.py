import sys
import ctypes
import os
from pathlib import Path

# Sideload yt-dlp — phải chạy trước mọi import của app.
# Khi đóng gói .exe, update_ytdlp() sẽ giải nén bản mới vào _sideload/.
# Lần mở kế tiếp, dòng này ưu tiên bản sideload thay vì bản đóng trong .exe.
if getattr(sys, "frozen", False):
    _sideload = Path(sys.executable).parent / "_sideload"
    if (_sideload / "yt_dlp").exists():
        sys.path.insert(0, str(_sideload))

from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QApplication, QInputDialog, QMessageBox

from core.constants import ASSETS_DIR
from core.app_controller import AppController
from core.services.license_service import LicenseService
from ui.main_window import MainWindow


def ensure_license(app: QApplication) -> bool:
    """
    Kiểm tra license trước khi mở app chính.
    - Có license cũ: kiểm tra lại với Google Sheet.
    - Chưa có / sai / hết hạn: hỏi KEY kích hoạt.

    Bản sửa: không cho vào app nếu check_saved_license() trả về None.
    """
    # Bản chạy từ source có thể bỏ qua bước kích hoạt khi phát triển cục bộ.
    # Cờ này không có hiệu lực với ứng dụng đã đóng gói.
    if not getattr(sys, "frozen", False) and os.environ.get("THM_DEV_SKIP_LICENSE") == "1":
        return True

    service = LicenseService()

    # Thử kiểm tra license đã lưu trước.
    first_error = ""
    try:
        saved_status = service.check_saved_license()
        if saved_status:
            expires_at = float(saved_status.get("expires_at", 0) or 0)
            if expires_at > 0:
                return True

        # Không có license hoặc file license rỗng/sai định dạng.
        first_error = "Chưa kích hoạt."

    except Exception as e:
        # Có thể chưa có license, hết hạn, key bị gỡ, chưa gắn máy...
        first_error = str(e)

    device_id = service.get_device_id()

    while True:
        message = "Nhập KEY kích hoạt Bản Pro V3:"
        if first_error:
            message += f"\n\nThông báo: {first_error}"
        message += f"\n\nMã máy hiện tại:\n{device_id}"

        key, ok = QInputDialog.getText(None, "Kích hoạt phần mềm", message)
        if not ok:
            return False

        key = str(key or "").strip()
        if not key:
            QMessageBox.warning(None, "Thiếu KEY", "Anh chưa nhập KEY kích hoạt.")
            first_error = ""
            continue

        try:
            status = service.activate_key(key)
        except Exception as e:
            QMessageBox.critical(None, "KEY không hợp lệ", str(e))
            first_error = ""
            continue

        days_left = int(status.get("license_days_left", 0) or 0)
        expire_date = status.get("license_expire_date", "") or status.get("expire_date", "")
        QMessageBox.information(
            None,
            "Kích hoạt thành công",
            f"Đã kích hoạt Bản Pro V3.\nHạn sử dụng: {expire_date}\nCòn lại: {days_left} ngày",
        )
        return True


def main():
    # Giúp Windows nhận diện đúng icon app trên taskbar, Alt-Tab và shortcut.
    # Dòng này nên chạy TRƯỚC khi tạo QApplication.
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(
            "TruyenHuu.THMVocalPanel.ProV3"
        )
    except Exception:
        pass

    app = QApplication(sys.argv)
    app.setApplicationName("THM Vocal Panel")
    app.setOrganizationName("Truyen Huu Music")
    app.setApplicationDisplayName("THM Vocal Panel")

    # Ưu tiên logo.ico cho Windows. Nếu chưa có thì dùng logo.png.
    icon_path = ASSETS_DIR / "logo.ico"
    if not icon_path.exists():
        icon_path = ASSETS_DIR / "logo.png"

    if icon_path.exists():
        app.setWindowIcon(QIcon(str(icon_path)))

    if not ensure_license(app):
        sys.exit(0)

    controller = AppController()
    window = MainWindow(controller)

    if icon_path.exists():
        window.setWindowIcon(QIcon(str(icon_path)))

    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
