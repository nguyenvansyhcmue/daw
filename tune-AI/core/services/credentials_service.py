"""
Mã hóa & giải mã file credentials Google Service Account.

Mục đích: file service_account.json (chứa private_key) không nên xuất hiện
ở dạng plain text trên đĩa. App sẽ:
    - Đọc file đã mã hóa (service_account.enc)
    - Giải mã ra dict trong RAM
    - Truyền dict cho gspread.service_account_from_dict()

Mức bảo vệ: chống "user mở Notepad nhìn thấy plain text".
KHÔNG bảo vệ chống reverse engineer .exe (vì key Fernet hardcoded trong code).
Chấp nhận trade-off này vì app phân phối Windows .exe — bảo mật tuyệt đối
là không khả thi cho app desktop offline.

Quy trình DEV (làm 1 lần):
    1. Có file service_account.json
    2. Chạy: python encrypt_credentials.py
    3. Tạo ra service_account.enc
    4. Xóa service_account.json (không cần nữa)
    5. Build .exe với PyInstaller (.spec đã bundle service_account.enc)

Quy trình RUNTIME:
    license_service.py → load_credentials_dict() → giải mã .enc → dict
    → gspread.service_account_from_dict(dict)
"""

from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path

from cryptography.fernet import Fernet


# =========================================================================
# SECRET KEY DERIVATION
# =========================================================================
# Key này được dùng để derive Fernet key. KHÔNG share công khai (đặc biệt
# trên git). Khi muốn xoay key, đổi giá trị này + chạy lại encrypt_credentials.py.
#
# Lưu ý bảo mật: nếu attacker có .exe, họ có thể reverse engineer ra value này.
# Đây là giới hạn tự nhiên của app desktop. Mức bảo vệ này đủ chống "Notepad attack"
# (user vô tình mở file ra), không chống được professional attacker.
# =========================================================================
_SECRET_PASSPHRASE = (
    b"THM_VOCAL_PANEL_v3_truyenhuumusic_credentials_protect_2026"
)


def _get_fernet() -> Fernet:
    """Derive Fernet key (32 bytes base64) từ secret passphrase qua SHA256."""
    key_material = hashlib.sha256(_SECRET_PASSPHRASE).digest()
    fernet_key = base64.urlsafe_b64encode(key_material)
    return Fernet(fernet_key)


# =========================================================================
# DEV-TIME: ENCRYPT FILE
# =========================================================================

def encrypt_file(input_path: str | Path, output_path: str | Path) -> int:
    """
    Mã hóa file `input_path` (vd service_account.json) thành `output_path`
    (vd service_account.enc).

    Trả về số byte ciphertext output.
    Raise FileNotFoundError nếu input không tồn tại.
    """
    input_path = Path(input_path)
    output_path = Path(output_path)

    if not input_path.exists():
        raise FileNotFoundError(f"Không tìm thấy file để mã hóa: {input_path}")

    cipher = _get_fernet()

    with input_path.open("rb") as f:
        plaintext = f.read()

    # Verify nó là JSON hợp lệ trước khi encrypt (tránh encrypt nhầm file rác).
    try:
        json.loads(plaintext.decode("utf-8"))
    except Exception as e:
        raise ValueError(
            f"File {input_path} không phải JSON hợp lệ. "
            f"Không nên mã hóa. Lỗi: {e}"
        )

    ciphertext = cipher.encrypt(plaintext)

    with output_path.open("wb") as f:
        f.write(ciphertext)

    return len(ciphertext)


# =========================================================================
# RUNTIME: DECRYPT FILE → DICT
# =========================================================================

def load_credentials_dict(enc_path: str | Path) -> dict:
    """
    Giải mã file `enc_path` (service_account.enc) và trả về dict credentials.

    Dict này có cùng schema với service_account.json gốc — có thể truyền
    trực tiếp cho gspread.service_account_from_dict(...).

    Raise FileNotFoundError nếu file .enc không tồn tại.
    Raise ValueError nếu giải mã thất bại (sai key hoặc file hỏng).
    """
    enc_path = Path(enc_path)

    if not enc_path.exists():
        raise FileNotFoundError(
            f"Không tìm thấy file credentials đã mã hóa: {enc_path}\n"
            f"Có thể chưa chạy encrypt_credentials.py để tạo file này."
        )

    cipher = _get_fernet()

    with enc_path.open("rb") as f:
        ciphertext = f.read()

    try:
        plaintext = cipher.decrypt(ciphertext)
    except Exception as e:
        raise ValueError(
            f"Giải mã credentials thất bại. File có thể bị hỏng hoặc "
            f"_SECRET_PASSPHRASE đã đổi từ lúc mã hóa. Lỗi: {e}"
        )

    try:
        return json.loads(plaintext.decode("utf-8"))
    except Exception as e:
        raise ValueError(f"Credentials sau giải mã không phải JSON hợp lệ: {e}")
