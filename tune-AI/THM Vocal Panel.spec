# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=[],
    datas=[('assets', 'assets'), ('ui\\theme.qss', 'ui'), ('service_account.enc', '.')],
    hiddenimports=['sklearn', 'sklearn.ensemble', 'sklearn.preprocessing', 'sklearn.tree', 'sklearn.neighbors', 'sklearn.utils._typedefs', 'sklearn.utils._heap', 'sklearn.utils._sorting', 'sklearn.utils._vector_sentinel', 'librosa', 'librosa.beat', 'librosa.effects', 'librosa.feature', 'librosa.segment', 'scipy', 'scipy.signal', 'sounddevice', 'mido.backends.rtmidi', 'yt_dlp', 'gspread', 'oauth2client', 'requests', 'websocket', 'websocket._app', 'cryptography', 'cryptography.fernet', 'cryptography.hazmat.primitives.ciphers', 'cryptography.hazmat.primitives.ciphers.algorithms', 'cryptography.hazmat.primitives.ciphers.modes', 'cryptography.hazmat.primitives.hashes', 'cryptography.hazmat.primitives.hmac', 'cryptography.hazmat.primitives.padding', 'cryptography.hazmat.backends.openssl', 'cryptography.hazmat.backends.openssl.backend'],
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
    name='THM Vocal Panel',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['assets\\logo.ico'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='THM Vocal Panel',
)
