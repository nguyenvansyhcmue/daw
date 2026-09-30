import sys
import ctypes
from pathlib import Path

# Sideload yt-dlp — phải chạy trước mọi import của app.
# Khi đóng gói .exe, update_ytdlp() sẽ giải nén bản mới vào _sideload/.
# Lần mở kế tiếp, dòng này ưu tiên bản sideload thay vì bản đóng trong .exe.
if getattr(sys, "frozen", False):
    _sideload = Path(sys.executable).parent / "_sideload"
    if (_sideload / "yt_dlp").exists():
        sys.path.insert(0, str(_sideload))

from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QApplication

from core.constants import ASSETS_DIR
from core.app_controller import AppController
from ui.main_window import MainWindow


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

    controller = AppController()
    window = MainWindow(controller)

    if icon_path.exists():
        window.setWindowIcon(QIcon(str(icon_path)))

    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
