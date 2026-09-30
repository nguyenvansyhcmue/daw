# THM Vocal Panel — Design UI Handoff

Branch: `design-ui-analog-handoff`
Base UI commit: `185830a`

## Cập nhật project trên máy khác

```powershell
cd D:\Project\daw
git fetch origin
git pull origin main
```

Lấy branch tài liệu/design:

```powershell
git fetch origin
git switch design-ui-analog-handoff
git pull origin design-ui-analog-handoff
```

Nếu chưa clone:

```powershell
cd D:\Project
git clone https://github.com/nguyenvansyhcmue/daw.git
cd .\daw\tune-AI
```

Đóng app trước khi pull/switch branch để tránh file bị khóa.

## Chạy và kiểm tra

```powershell
cd D:\Project\daw\tune-AI
.\.venv\Scripts\python.exe -m compileall -q ui core
.\.venv\Scripts\python.exe main.py
```

## Phần UI đã làm hôm nay

- Dựng `AnalogChassis` theo `THM_full_app_reference.png`.
- Dùng hệ tọa độ reference-base 1080×684.
- Dùng `ReferenceOverlay` để scale live controls.
- Bỏ `legacy QWidget` ẩn.
- Xóa các builder UI cũ: header, tone, mode, effect, volume, monitor, system và các helper card/rotary cũ.
- Giữ `_slider` cho logic drag, smoothing và backend binding.
- Núm mới dùng vùng input live trên asset chassis, không vẽ chồng thân núm cũ.
- Đã nối callback/controller cho Reverb ngắn/dài, Echo, Mic Vol, Music Vol, Tune, Tone +/-, Auto/Fix/Edit, mode và Monitor/System.
- LOFI dùng đúng `on_lofi_clicked()`; các mode còn lại dùng `controller.apply_mode()`.
- Bổ sung compatibility handles `btn_detect_tone` và `lbl_end_mod_info`.
- Thêm `ui/analog_components.py`, `ui/analog_theme.py` và asset pack vào `assets/analog/`.
- Thêm Blender render asset tại `assets/analog/blender_render/`.

## Kiến trúc presentation

```text
MainWindow
└── AnalogChassis
    └── ReferenceOverlay
        ├── live tone controls
        ├── live mode hit areas
        ├── live RotaryKnob controls
        └── live monitor/system controls
```

Controller/DSP/audio engine giữ nguyên.

## Asset mapping

```text
01_BACKGROUNDS  → chassis/background
02_KNOBS        → knob style/reference
03_LCD_METERS   → LCD/meter
04_TOP_CONTROLS → AUTO/FIX/EDIT và +/-
05_MODE_TILES   → LOFI/NHẠC TRẺ/BOLERO/REMIX
06_BOTTOM_BUTTONS → monitor/system buttons
07_ICONS        → icons
08_TEXTURES     → material/texture
blender_render  → Blender 4.5 hardware render
```

## Việc cần tiếp tục

1. Kiểm tra visual ở 100%, 125%, 150%.
2. Test kéo núm và con lăn trên máy thật.
3. Kiểm tra MIDI device thật cho từng núm/nút.
4. Test Settings, Song List, Karaoke, Contact.
5. Đối chiếu screenshot với `THM_full_app_reference.png`.
6. Chỉ chỉnh presentation layer, không sửa DSP/controller nếu không cần.

## Commit/push tiếp theo

```powershell
cd D:\Project\daw
git status
git add .
git commit -m "Describe UI change"
git push origin design-ui-analog-handoff
```

Merge về main sau khi kiểm tra:

```powershell
git switch main
git pull origin main
git merge design-ui-analog-handoff
git push origin main
```
