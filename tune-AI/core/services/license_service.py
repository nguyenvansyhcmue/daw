import json
import os
import sys
import time
import uuid
from pathlib import Path

try:
    import gspread
except Exception:
    gspread = None

try:
    from core.services.credentials_service import load_credentials_dict
except Exception:
    load_credentials_dict = None


class LicenseService:
    """
    Google Sheet license service.

    Cấu trúc Google Sheet:
    - Cột A: KEY
    - Cột B: NGÀY HẾT HẠN, dạng dd/mm/yyyy hoặc yyyy-mm-dd
    - Cột C: ID MACHINE
    """

    GOOGLE_SHEET_ID = "1CJ_x_SYfAc31t4UymSVhJI09LT3swtHf-xLZhlAVIiI"
    COL_KEY = 1
    COL_EXPIRE = 2
    COL_MACHINE = 3

    def __init__(self):
        # base_dir = thư mục cạnh .exe (frozen) hoặc gốc project (dev).
        # Dùng cho user data PERSIST giữa các lần chạy (license.json, cache).
        self.base_dir = self._get_base_dir()
        self.data_dir = self.base_dir / "data"
        self.data_dir.mkdir(parents=True, exist_ok=True)

        self.license_path = self.data_dir / "license.json"
        self.license_status_path = self.data_dir / "license_status.json"

        # bundled_dir = thư mục PyInstaller bundle resource (sys._MEIPASS khi frozen).
        # Dùng cho file READ-ONLY đã được bundle vào .exe (credentials, assets,...).
        # PyInstaller v6+ onedir: sys._MEIPASS = dist/AppName/_internal/
        # PyInstaller onefile:    sys._MEIPASS = temp folder runtime extract
        # Dev mode:               sys._MEIPASS không có → dùng project root
        self.bundled_dir = self._get_bundled_dir()

        self.service_account_enc_path = self.bundled_dir / "service_account.enc"
        self.service_account_json_path = self.bundled_dir / "service_account.json"

    def _get_base_dir(self) -> Path:
        if getattr(sys, "frozen", False):
            return Path(sys.executable).resolve().parent
        return Path(__file__).resolve().parents[2]

    def _get_bundled_dir(self) -> Path:
        """
        Thư mục chứa các resource đã bundle (read-only).
        Khác với base_dir — base_dir cạnh .exe, bundled_dir trong _internal/.
        """
        if getattr(sys, "frozen", False):
            # sys._MEIPASS là chuẩn của PyInstaller — chỉ tới nơi bundle data.
            meipass = getattr(sys, "_MEIPASS", None)
            if meipass:
                return Path(meipass).resolve()
            # Fallback nếu vì lý do nào đó _MEIPASS không có:
            # thử _internal/ cạnh .exe, hoặc cuối cùng là cạnh .exe.
            exe_dir = Path(sys.executable).resolve().parent
            internal = exe_dir / "_internal"
            return internal if internal.exists() else exe_dir
        # Dev mode: gốc project.
        return Path(__file__).resolve().parents[2]

    # =========================================================
    # DEVICE / TIME
    # =========================================================

    def get_device_id(self) -> str:
        mac = uuid.getnode()
        mac_hex = f"{mac:012X}"
        return "-".join(mac_hex[i:i + 2] for i in range(0, 12, 2))

    def parse_expiry_date(self, date_str: str) -> float:
        date_str = str(date_str or "").strip()
        if not date_str:
            return 0.0

        for fmt in ("%d/%m/%Y", "%Y-%m-%d"):
            try:
                t = time.strptime(date_str, fmt)
                return float(time.mktime(t))
            except ValueError:
                continue
        return 0.0

    def format_date(self, timestamp_value: float) -> str:
        try:
            if not timestamp_value:
                return ""
            return time.strftime("%d/%m/%Y", time.localtime(float(timestamp_value)))
        except Exception:
            return ""

    def get_days_left(self, expires_at: float) -> int:
        try:
            return max(0, int((float(expires_at) - time.time()) / 86400))
        except Exception:
            return 0

    # =========================================================
    # LOCAL LICENSE FILE
    # =========================================================

    def load_license(self):
        if not self.license_path.exists():
            return None
        try:
            with open(self.license_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
            return None
        except Exception:
            return None

    def save_license(self, key: str, device_id: str, expires_at: float, expire_date_text: str = ""):
        days_left = self.get_days_left(expires_at)
        expire_date = expire_date_text or self.format_date(expires_at)

        data = {
            "key": str(key or "").strip(),
            "device_id": str(device_id or "").strip(),
            "expires_at": float(expires_at),
            "expire_date": expire_date,
            "license_expire_date": expire_date,
            "license_days_left": days_left,
            "checked_at": int(time.time()),
        }

        with open(self.license_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

        with open(self.license_status_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

        return data

    def clear_license(self):
        for path in [self.license_path, self.license_status_path]:
            try:
                if path.exists():
                    path.unlink()
            except Exception:
                pass

    # =========================================================
    # GOOGLE SHEET
    # =========================================================

    def _require_gspread(self):
        if gspread is None:
            raise RuntimeError(
                "Chưa cài thư viện gspread. Hãy chạy: pip install gspread oauth2client"
            )

        # Có file mã hóa (.enc) hoặc file gốc (.json) đều OK.
        if not self.service_account_enc_path.exists() and not self.service_account_json_path.exists():
            raise FileNotFoundError(
                "Không tìm thấy credentials Google. "
                f"Cần có một trong hai file:\n"
                f"  - {self.service_account_enc_path}  (đã mã hóa, ưu tiên)\n"
                f"  - {self.service_account_json_path} (plain text, fallback)"
            )

    def _load_credentials_dict(self) -> dict:
        """
        Đọc credentials theo thứ tự ưu tiên:
            1. service_account.enc (đã mã hóa) — production
            2. service_account.json (plain text) — dev/legacy
        """
        # Ưu tiên file đã mã hóa.
        if self.service_account_enc_path.exists():
            if load_credentials_dict is None:
                raise RuntimeError(
                    "Không import được credentials_service. "
                    "Cài cryptography: pip install cryptography"
                )
            try:
                return load_credentials_dict(self.service_account_enc_path)
            except Exception as e:
                raise RuntimeError(
                    f"Không giải mã được {self.service_account_enc_path}: {e}"
                )

        # Fallback đọc plain text (legacy).
        if self.service_account_json_path.exists():
            try:
                with self.service_account_json_path.open("r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception as e:
                raise RuntimeError(
                    f"Không đọc được {self.service_account_json_path}: {e}"
                )

        raise FileNotFoundError("Không tìm thấy credentials.")

    def _open_sheet(self):
        self._require_gspread()

        try:
            creds_dict = self._load_credentials_dict()
            gc = gspread.service_account_from_dict(creds_dict)
        except Exception as e:
            raise RuntimeError(f"Không đọc được service account credentials: {e}")

        try:
            sh = gc.open_by_key(self.GOOGLE_SHEET_ID)
        except Exception as e:
            raise RuntimeError(f"Không mở được Google Sheet: {e}")

        try:
            return sh.sheet1
        except Exception as e:
            raise RuntimeError(f"Không truy cập được sheet1: {e}")

    def validate_key_with_sheet(self, key: str, device_id: str, allow_bind_if_empty: bool) -> tuple[float, str]:
        key_norm = str(key or "").strip()
        if not key_norm:
            raise ValueError("KEY rỗng.")

        ws = self._open_sheet()

        try:
            key_col_values = ws.col_values(self.COL_KEY)
        except Exception as e:
            raise RuntimeError(f"Không đọc được cột KEY từ sheet: {e}")

        row_index = None
        for idx, cell_value in enumerate(key_col_values, start=1):
            if str(cell_value or "").strip() == key_norm:
                row_index = idx
                break

        if row_index is None:
            raise ValueError("KEY không tồn tại trong hệ thống.")

        try:
            expire_str = ws.cell(row_index, self.COL_EXPIRE).value or ""
        except Exception as e:
            raise RuntimeError(f"Không đọc được cột ngày hết hạn: {e}")

        expires_at = self.parse_expiry_date(expire_str)
        if not expires_at or expires_at <= time.time():
            raise ValueError("KEY đã hết hạn hoặc ngày hết hạn không hợp lệ.")

        try:
            machine_in_sheet = ws.cell(row_index, self.COL_MACHINE).value or ""
        except Exception as e:
            raise RuntimeError(f"Không đọc được cột ID MACHINE: {e}")

        machine_in_sheet = str(machine_in_sheet or "").strip()
        device_id = str(device_id or "").strip()

        if not machine_in_sheet:
            if allow_bind_if_empty:
                try:
                    ws.update_cell(row_index, self.COL_MACHINE, device_id)
                except Exception as e:
                    raise RuntimeError(f"Không ghi được ID MACHINE lên sheet: {e}")
            else:
                raise ValueError("KEY chưa được gắn máy. Vui lòng nhập KEY kích hoạt lại.")
        elif machine_in_sheet != device_id:
            raise ValueError(
                "KEY này đã được kích hoạt cho máy khác.\n"
                f"Máy trên sheet: {machine_in_sheet}\n"
                f"Máy hiện tại: {device_id}"
            )

        return float(expires_at), str(expire_str or "").strip()

    # =========================================================
    # HIGH-LEVEL API
    # =========================================================

    def check_saved_license(self):
        lic = self.load_license()
        if not lic:
            return None

        key = str(lic.get("key", "") or "").strip()
        stored_device_id = str(lic.get("device_id", "") or "").strip()
        current_device_id = self.get_device_id()

        if not key or stored_device_id != current_device_id:
            return None

        expires_at, expire_str = self.validate_key_with_sheet(
            key=key,
            device_id=current_device_id,
            allow_bind_if_empty=False,
        )

        return self.save_license(
            key=key,
            device_id=current_device_id,
            expires_at=expires_at,
            expire_date_text=expire_str,
        )

    def activate_key(self, key: str):
        device_id = self.get_device_id()
        expires_at, expire_str = self.validate_key_with_sheet(
            key=key,
            device_id=device_id,
            allow_bind_if_empty=True,
        )
        return self.save_license(
            key=key,
            device_id=device_id,
            expires_at=expires_at,
            expire_date_text=expire_str,
        )

    def get_license_status(self):
        lic = self.load_license()
        if not lic:
            return {
                "activated": False,
                "display": "Chưa kích hoạt",
                "days_left": 0,
                "expire_date": "",
            }

        expires_at = float(lic.get("expires_at", 0) or 0)
        days_left = self.get_days_left(expires_at)
        expire_date = str(lic.get("license_expire_date") or lic.get("expire_date") or self.format_date(expires_at))

        return {
            "activated": bool(expires_at and expires_at > time.time()),
            "key": lic.get("key", ""),
            "device_id": lic.get("device_id", ""),
            "expires_at": expires_at,
            "expire_date": expire_date,
            "days_left": days_left,
            "display": f"còn {days_left} ngày" if days_left > 0 else "Hết hạn",
        }

    def get_license_display_text(self) -> str:
        return self.get_license_status().get("display", "Chưa kích hoạt")
