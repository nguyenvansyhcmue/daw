from __future__ import annotations

import ctypes
import json
import time
from pathlib import Path

try:
    import win32api
    import win32con
    import win32gui
    import win32process
except Exception:  # pragma: no cover
    win32api = None
    win32con = None
    win32gui = None
    win32process = None


def _friendly_win_error(e: Exception) -> RuntimeError:
    msg = str(e)
    if "SetCursorPos" in msg or "Access is denied" in msg or "Access denied" in msg or "2147024891" in msg:
        return RuntimeError(
            "Windows đang chặn Auto-Key điều khiển chuột.\n\n"
            "Cách xử lý:\n"
            "1. Tắt THM Vocal Panel và cửa sổ Auto-Key.\n"
            "2. Mở THM Vocal Panel bằng Run as administrator.\n"
            "3. Đảm bảo cửa sổ Auto-Key không bị minimize hoặc bị popup che."
        )
    return RuntimeError(msg)


def _block_input(block: bool):
    """
    Khóa input toàn hệ thống có thể bị Windows chặn ở một số máy.
    Vì vậy nếu lỗi thì bỏ qua, không để làm crash Auto-Key.
    """
    try:
        ctypes.windll.user32.BlockInput(1 if block else 0)
    except Exception:
        pass


class AutoKeyService:
    def __init__(self, path=None):
        if path is None:
            path = Path(__file__).resolve().parents[2] / "data" / "autokey_config.json"
        self.path = Path(path)

        self.config = {
            "target_hwnd": 0,
            "target_title": "",
            "target_class": "",
            "target_hint": "",
            "p1": {"x": None, "y": None},
            "p2": {"x": None, "y": None},
            "p3": {"x": None, "y": None},
            "delay_23": 12.0,
            "auto_run_on_play": False,
            # False để giảm lỗi Access Denied trên máy khách.
            "block_input_during_autokey": False,
        }
        self.load()

    # =========================================================
    # JSON
    # =========================================================

    def load(self):
        if self.path.exists():
            try:
                with open(self.path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, dict):
                    self.config.update(data)
            except Exception:
                pass
        return self.config

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "w", encoding="utf-8") as f:
            json.dump(self.config, f, ensure_ascii=False, indent=2)

    def get_config(self):
        return dict(self.config)

    def update_config(self, patch: dict):
        if isinstance(patch, dict):
            for k, v in patch.items():
                self.config[k] = v
            self.save()

    # =========================================================
    # WIN32
    # =========================================================

    def _require_win(self):
        if win32api is None or win32gui is None or win32con is None:
            raise RuntimeError("Thiếu pywin32 để điều khiển Auto-Key trên Windows.")

    def _find_hwnd_by_class_and_hint(self, wclass: str, hint: str):
        self._require_win()
        candidates = []

        def enum_cb(hwnd, _):
            try:
                if not win32gui.IsWindowVisible(hwnd):
                    return
                title = win32gui.GetWindowText(hwnd) or ""
                if not title.strip():
                    return
                cls = win32gui.GetClassName(hwnd) or ""
                score = 0
                if wclass and cls.lower() == wclass.lower():
                    score += 50
                if hint and hint.lower() in title.lower():
                    score += 30
                if score > 0:
                    candidates.append((score, hwnd, title, cls))
            except Exception:
                pass

        win32gui.EnumWindows(enum_cb, None)
        if not candidates:
            return None
        candidates.sort(key=lambda x: x[0], reverse=True)
        return int(candidates[0][1])

    def resolve_target_hwnd(self):
        self._require_win()
        hwnd = int(self.config.get("target_hwnd") or 0)

        if hwnd and win32gui.IsWindow(hwnd):
            return hwnd

        new_hwnd = self._find_hwnd_by_class_and_hint(
            str(self.config.get("target_class", "") or "").strip(),
            str(self.config.get("target_hint", "") or "").strip(),
        )
        if new_hwnd and win32gui.IsWindow(new_hwnd):
            self.config["target_hwnd"] = int(new_hwnd)
            self.save()
            return int(new_hwnd)
        return None

    def capture_target_from_cursor(self):
        self._require_win()
        x, y = win32api.GetCursorPos()
        hwnd = win32gui.WindowFromPoint((x, y))
        hwnd = win32gui.GetAncestor(hwnd, win32con.GA_ROOT)
        if not hwnd or not win32gui.IsWindow(hwnd):
            raise RuntimeError("Không lấy được cửa sổ mục tiêu.")

        title = win32gui.GetWindowText(hwnd) or ""
        cls = win32gui.GetClassName(hwnd) or ""
        hint = title[:32]

        self.config.update({
            "target_hwnd": int(hwnd),
            "target_title": title,
            "target_class": cls,
            "target_hint": hint,
        })
        self.save()
        return dict(self.config)

    def capture_point_from_cursor(self, point_name: str):
        self._require_win()
        hwnd = self.resolve_target_hwnd()
        if not hwnd:
            raise RuntimeError("Chưa chọn cửa sổ Auto-Key.")

        x, y = win32api.GetCursorPos()
        left, top, right, bottom = win32gui.GetWindowRect(hwnd)

        rel_x = int(x - left)
        rel_y = int(y - top)

        if rel_x < 0 or rel_y < 0 or x > right or y > bottom:
            raise RuntimeError("Vị trí chuột hiện tại không nằm trong cửa sổ Auto-Key đã chọn.")

        self.config[point_name] = {"x": rel_x, "y": rel_y}
        self.save()
        return dict(self.config[point_name])

    def _rel_to_abs(self, hwnd: int, rel_x: int, rel_y: int):
        left, top, _, _ = win32gui.GetWindowRect(hwnd)
        return int(left + rel_x), int(top + rel_y)

    def _safe_set_cursor_pos(self, x: int, y: int):
        try:
            win32api.SetCursorPos((int(x), int(y)))
        except Exception as e:
            raise _friendly_win_error(e)

    def _left_click(self):
        try:
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        except Exception as e:
            raise _friendly_win_error(e)

    def _double_click_abs(self, x: int, y: int):
        self._safe_set_cursor_pos(x, y)
        time.sleep(0.08)
        self._left_click()
        time.sleep(0.08)
        self._left_click()

    def _focus_window(self, hwnd: int):
        try:
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        except Exception:
            pass

        # SetForegroundWindow đôi khi bị Windows chặn; vẫn tiếp tục nếu lỗi.
        try:
            win32gui.SetForegroundWindow(hwnd)
            time.sleep(0.15)
            return
        except Exception:
            pass

        # Fallback: click nhẹ vào title/window để đưa lên trước.
        try:
            left, top, right, bottom = win32gui.GetWindowRect(hwnd)
            x = int(left + min(80, max(20, (right - left) // 2)))
            y = int(top + 15)
            self._safe_set_cursor_pos(x, y)
            time.sleep(0.05)
            self._left_click()
            time.sleep(0.15)
        except Exception:
            pass

    # =========================================================
    # RUN
    # =========================================================

    def run_cycle(self):
        self._require_win()
        hwnd = self.resolve_target_hwnd()
        if not hwnd:
            raise RuntimeError("Không tìm thấy cửa sổ Auto-Key mục tiêu.")

        p1 = self.config.get("p1") or {}
        p2 = self.config.get("p2") or {}
        p3 = self.config.get("p3") or {}

        if None in (p1.get("x"), p1.get("y"), p2.get("x"), p2.get("y"), p3.get("x"), p3.get("y")):
            raise RuntimeError("Chưa lấy đủ tọa độ P1, P2, P3.")

        delay_23 = float(self.config.get("delay_23", 12.0) or 12.0)
        use_block_input = bool(self.config.get("block_input_during_autokey", False))

        old_pos = win32api.GetCursorPos()

        try:
            self._focus_window(hwnd)
            time.sleep(0.25)
            if use_block_input:
                _block_input(True)

            x1, y1 = self._rel_to_abs(hwnd, int(p1["x"]), int(p1["y"]))
            self._double_click_abs(x1, y1)
            time.sleep(0.15)

            x2, y2 = self._rel_to_abs(hwnd, int(p2["x"]), int(p2["y"]))
            self._double_click_abs(x2, y2)

            time.sleep(delay_23)

            x3, y3 = self._rel_to_abs(hwnd, int(p3["x"]), int(p3["y"]))
            self._double_click_abs(x3, y3)

        except RuntimeError:
            raise
        except Exception as e:
            raise _friendly_win_error(e)
        finally:
            try:
                self._safe_set_cursor_pos(old_pos[0], old_pos[1])
            except Exception:
                pass
            if use_block_input:
                _block_input(False)
