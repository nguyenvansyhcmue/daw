#!/usr/bin/env python
"""
Tool DEV-ONLY: Mã hóa file service_account.json → service_account.enc.

Chạy 1 LẦN duy nhất sau khi nhận file service_account.json mới từ Google Cloud.
Sau khi chạy xong:
    1. Verify file service_account.enc đã được tạo
    2. (Tùy chọn) Test app: `python main.py` → app phải chạy bình thường
    3. XÓA service_account.json (không còn cần plain text)
    4. Build .exe lại: `pyinstaller "THM Vocal Panel.spec"`

Usage:
    python encrypt_credentials.py
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

# Force UTF-8 output cho console Windows.
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

from core.services.credentials_service import encrypt_file


INPUT = ROOT / "service_account.json"
OUTPUT = ROOT / "service_account.enc"


def main() -> int:
    print("=" * 60)
    print("THM Vocal Panel — Encrypt Service Account Credentials")
    print("=" * 60)
    print(f"Input :  {INPUT}")
    print(f"Output:  {OUTPUT}")
    print()

    if not INPUT.exists():
        print(f"[ERROR] Không tìm thấy {INPUT.name} ở thư mục dự án.")
        print(f"        Hãy đặt file service_account.json vào: {ROOT}")
        return 1

    if OUTPUT.exists():
        print(f"[WARNING] {OUTPUT.name} đã tồn tại.")
        answer = input("Ghi đè? (y/N): ").strip().lower()
        if answer not in ("y", "yes"):
            print("Đã hủy.")
            return 0
        print()

    try:
        size = encrypt_file(INPUT, OUTPUT)
    except Exception as e:
        print(f"[ERROR] Mã hóa thất bại: {e}")
        return 1

    print(f"[OK] Đã mã hóa thành công.")
    print(f"     {OUTPUT.name}: {size:,} bytes")
    print()
    print("Bước tiếp theo:")
    print("  1. Test app:                  python main.py")
    print("  2. Xóa file plain text:       del service_account.json")
    print("  3. Build .exe:                pyinstaller \"THM Vocal Panel.spec\"")
    print()
    print("Khi distribute .exe:")
    print(f"  - {OUTPUT.name} sẽ được PyInstaller bundle vào dist/ tự động.")
    print(f"  - Khách chỉ cần copy nguyên thư mục dist/ là dùng được.")
    print(f"  - Không cần copy thêm service_account.json (đã không còn).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
