from __future__ import annotations

import os
import subprocess
import sys
import time
from pathlib import Path


class BrowserLauncherService:
    """
    Tự mở Brave đúng chế độ để app đọc URL YouTube qua remote debugging.
    Bản này đã ẩn console phụ khi chạy từ app đóng gói .exe.
    """

    def __init__(self):
        self.debug_port = 9222
        self.profile_dir = Path.home() / "Documents" / "THM_Brave_Profile"

    def _possible_brave_paths(self):
        return [
            r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe",
            r"C:\Program Files (x86)\BraveSoftware\Brave-Browser\Application\brave.exe",
            str(Path.home() / r"AppData\Local\BraveSoftware\Brave-Browser\Application\brave.exe"),
        ]

    def _windows_no_console_kwargs(self) -> dict:
        if not sys.platform.startswith("win"):
            return {}

        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW

        return {
            "startupinfo": startupinfo,
            "creationflags": subprocess.CREATE_NO_WINDOW,
        }

    def find_brave_exe(self) -> str:
        for path in self._possible_brave_paths():
            if os.path.exists(path):
                return path

        raise FileNotFoundError(
            "Không tìm thấy brave.exe. Anh kiểm tra lại Brave đã cài chưa."
        )

    def open_brave_for_auto_detect(self, start_url: str = "https://www.youtube.com"):
        brave_exe = self.find_brave_exe()
        self.profile_dir.mkdir(parents=True, exist_ok=True)

        args = [
            brave_exe,
            f"--remote-debugging-port={self.debug_port}",
            # CRITICAL: Brave/Chromium v107+ chặn WebSocket CDP nếu thiếu flag
            # này (handshake 403 Forbidden). Cần allow origin cho websocket-client
            # connect được. Đây là cách chính thức Chromium documented.
            "--remote-allow-origins=*",
            f"--user-data-dir={str(self.profile_dir)}",
            "--new-window",
            start_url,
        ]

        subprocess.Popen(
            args,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            shell=False,
            **self._windows_no_console_kwargs(),
        )

        time.sleep(1.0)

        return {
            "brave_exe": brave_exe,
            "debug_port": self.debug_port,
            "profile_dir": str(self.profile_dir),
            "start_url": start_url,
        }

    def diagnose_cdp(self) -> dict:
        """
        Chẩn đoán trạng thái CDP để app biết phải làm gì:
            - is_brave_running: có process brave.exe chạy không
            - cdp_port_open: port 9222 có nhận connection không
            - tabs_count: số tab nếu CDP work
        """
        result = {
            "is_brave_running": False,
            "cdp_port_open": False,
            "tabs_count": 0,
            "error": "",
        }
        try:
            if sys.platform.startswith("win"):
                check = subprocess.run(
                    ["tasklist", "/FI", "IMAGENAME eq brave.exe", "/FO", "CSV", "/NH"],
                    capture_output=True,
                    timeout=1.5,
                    text=True,
                    **self._windows_no_console_kwargs(),
                )
                result["is_brave_running"] = "brave.exe" in (check.stdout or "").lower()
        except Exception:
            pass

        try:
            import requests
            r = requests.get(f"http://127.0.0.1:{self.debug_port}/json", timeout=1.5)
            if r.ok:
                data = r.json()
                if isinstance(data, list):
                    result["cdp_port_open"] = True
                    result["tabs_count"] = len(data)
        except Exception as e:
            result["error"] = str(e)

        return result

    def restart_brave_with_cdp(self, start_url: str = "https://www.youtube.com") -> dict:
        """
        Kết hợp: đóng tất cả Brave hiện tại + mở lại với cấu hình CDP đúng.
        Dùng khi user click "RESET BRAVE" hoặc app auto-fix CDP.
        """
        close_result = self.close_all_brave()
        # Đợi 1s cho process thực sự kết thúc
        time.sleep(1.0)
        try:
            launch_info = self.open_brave_for_auto_detect(start_url=start_url)
            return {
                "ok": True,
                "close": close_result,
                "launch": launch_info,
            }
        except Exception as e:
            return {
                "ok": False,
                "close": close_result,
                "error": str(e),
            }

    def close_all_brave(self) -> dict:
        """
        Đóng tất cả cửa sổ Brave khi user tắt app.

        Dùng taskkill cho graceful shutdown trước, fallback sang force kill
        nếu process không phản hồi trong 1.5s.
        """
        result = {"closed": False, "method": "", "error": ""}
        if not sys.platform.startswith("win"):
            result["error"] = "Chỉ hỗ trợ Windows"
            return result

        try:
            # Graceful close trước (gửi WM_CLOSE)
            subprocess.run(
                ["taskkill", "/IM", "brave.exe", "/T"],
                capture_output=True,
                timeout=2.0,
                **self._windows_no_console_kwargs(),
            )
            time.sleep(0.5)

            # Verify còn process Brave nào không
            check = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq brave.exe", "/FO", "CSV", "/NH"],
                capture_output=True,
                timeout=1.5,
                text=True,
                **self._windows_no_console_kwargs(),
            )
            still_running = "brave.exe" in (check.stdout or "").lower()

            if still_running:
                # Force kill nếu graceful close không xong
                subprocess.run(
                    ["taskkill", "/F", "/IM", "brave.exe", "/T"],
                    capture_output=True,
                    timeout=2.0,
                    **self._windows_no_console_kwargs(),
                )
                result["method"] = "force_kill"
            else:
                result["method"] = "graceful"

            result["closed"] = True
        except Exception as e:
            result["error"] = str(e)
        return result
