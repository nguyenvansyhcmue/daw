import os
import platform
import subprocess
import psutil


class CubaseService:
    def open_project(self, path: str):
        if not path:
            raise ValueError("Chưa có đường dẫn project.")
        if not os.path.exists(path):
            raise FileNotFoundError(f"Không tìm thấy file project: {path}")

        if platform.system() == "Darwin":
            # Project .logicx là package (thư mục) trên macOS.
            subprocess.run(["open", "-a", "Logic Pro", path], check=True)
            return

        os.startfile(path)

    def is_cubase_running(self):
        process_hint = "logic" if platform.system() == "Darwin" else "cubase"
        for proc in psutil.process_iter(["name"]):
            name = (proc.info.get("name") or "").lower()
            if process_hint in name:
                return True
        return False

    def close_cubase_gracefully(self):
        """Yêu cầu DAW đóng bình thường để ứng dụng còn hỏi lưu project."""
        if platform.system() == "Darwin":
            result = subprocess.run(
                ["osascript", "-e", 'tell application "Logic Pro" to quit'],
                capture_output=True,
                text=True,
                timeout=10,
            )
            return result.returncode == 0

        pids = []
        for proc in psutil.process_iter(["name", "pid"]):
            name = (proc.info.get("name") or "").lower()
            if "cubase" in name:
                pids.append(proc.info["pid"])

        if not pids:
            return False

        for pid in pids:
            try:
                subprocess.run(
                    ["taskkill", "/PID", str(pid)],
                    capture_output=True,
                    timeout=10,
                )
            except Exception:
                pass

        return True
