# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path


root = Path(SPECPATH)
datas = [
    (str(root / "assets"), "assets"),
    (str(root / "ui" / "theme.qss"), "ui"),
]
credentials = root / "service_account.enc"
if credentials.exists():
    datas.append((str(credentials), "."))

icon = root / "assets" / "logo.icns"

a = Analysis(
    [str(root / "main.py")],
    pathex=[str(root)],
    binaries=[],
    datas=datas,
    hiddenimports=[
        "sklearn", "sklearn.ensemble", "sklearn.preprocessing", "sklearn.tree",
        "sklearn.neighbors", "sklearn.utils._typedefs", "sklearn.utils._heap",
        "sklearn.utils._sorting", "sklearn.utils._vector_sentinel", "librosa",
        "librosa.beat", "librosa.effects", "librosa.feature", "librosa.segment",
        "scipy", "scipy.signal", "sounddevice", "yt_dlp", "gspread",
        "oauth2client", "requests", "websocket", "websocket._app", "cryptography",
        "cryptography.fernet", "cryptography.hazmat.primitives.ciphers",
        "cryptography.hazmat.primitives.ciphers.algorithms",
        "cryptography.hazmat.primitives.ciphers.modes",
        "cryptography.hazmat.primitives.hashes", "cryptography.hazmat.primitives.hmac",
        "cryptography.hazmat.primitives.padding", "cryptography.hazmat.backends.openssl",
        "cryptography.hazmat.backends.openssl.backend",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="THM Vocal Panel",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=True,
    icon=str(icon) if icon.exists() else None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="THM Vocal Panel",
)

app = BUNDLE(
    coll,
    name="THM Vocal Panel.app",
    icon=str(icon) if icon.exists() else None,
    bundle_identifier="com.truyenhuumusic.thmvocalpanel",
)
