# Tune-AI — Codex Handoff

Ngày: 2026-09-30
Project: `D:\Project\daw\tune-AI`

## Mục tiêu hiện tại

Rebuild presentation layer theo `THM_full_app_reference.png` và asset pack Analog Future, giữ nguyên controller, DSP/audio, callback, state và business logic.

## Đã hoàn thành hôm nay

- Dựng `AnalogChassis` dùng reference production làm nền.
- Dựng `ReferenceOverlay` theo hệ tọa độ reference-base 1080×684.
- Không còn khởi tạo `legacy QWidget` ẩn.
- Đã xóa các UI builder cũ không còn sử dụng:
  - `_build_top_header`
  - `_build_tone_card`
  - `_build_mode_card`
  - `_build_effect_card`
  - `_build_volume_card`
  - `_build_monitor_card`
  - `_build_system_card`
  - `_build_reference_overlay`
  - `_card`, `_section_label`, `_rotary_cell`
- Giữ lại `_slider` vì live knob mới dùng chung logic drag/smoothing/backend callback.
- Live controls hiện giữ đúng các thuộc tính cũ để không phá controller:
  - `slider_reverb_short`, `slider_reverb_long`, `slider_echo`
  - `slider_mic_vol`, `slider_music_vol`, `slider_tune`
  - `btn_mode_lofi`, `btn_mode_tre`, `btn_mode_bolero`, `btn_mode_remix`
  - các nút monitor/system và tone controls.
- Sửa LOFI dùng `on_lofi_clicked()` thay vì gọi sai `controller.apply_mode("LOFI")`.
- Bổ sung compatibility handles cần cho tone workflow:
  - `btn_detect_tone`
  - `lbl_end_mod_info`
- Núm trong reference overlay dùng làm vùng input live, không vẽ chồng thân núm cũ lên production chassis.
- Asset pack hiện nằm tại `assets/analog/`.

## File chính đã sửa/thêm

- `ui/main_window.py`
- `ui/premium_widgets.py`
- `ui/preset_assets.py`
- `ui/analog_components.py`
- `ui/analog_theme.py`
- `assets/analog/`
- `assets/knobs/`

## Backend/controller giữ nguyên

Các callback hiện vẫn gọi backend cũ:

- `controller.set_reverb_short`
- `controller.set_reverb_long`
- `controller.set_echo`
- `controller.set_mic_volume`
- `controller.set_music_volume`
- `controller.set_tune_from_slider_position`
- `controller.toggle_mic`
- `controller.toggle_vang`
- `controller.toggle_lofi`
- `controller.apply_mode`
- `controller.set_global_pitch`

## Đã kiểm tra

Smoke test và compile đã đạt:

```powershell
cd D:\Project\daw\tune-AI
$env:QT_QPA_PLATFORM='offscreen'
.\.venv\Scripts\python.exe -m compileall -q ui core
```

Khởi tạo `MainWindow` thành công với 24 live controls.

Đã gọi thử thành công:

- Auto detect
- Reverb ngắn/dài
- Echo
- Mic Vol
- Music Vol
- Tune
- Tone +/-
- LOFI / NHẠC TRẺ / BOLERO / REMIX
- Mic/Vang callback

## Lệnh mở app

```powershell
cd D:\Project\daw\tune-AI
.\.venv\Scripts\python.exe main.py
```

## Việc cần làm tiếp

1. Chạy app trên máy mới và kiểm tra click/drag thực tế ở tỷ lệ 100%, 125%, 150%.
2. Kiểm tra các nút mở dialog như Settings, Song List, Karaoke, Contact.
3. Kiểm tra backend MIDI thật với thiết bị đang dùng.
4. Đối chiếu screenshot với `assets/analog/THM_full_app_reference.png`.
5. Không đưa lại các builder UI cũ đã xóa.
6. Nếu cần thay đổi visual, chỉ chỉnh `analog_theme.py`, `analog_components.py` và overlay layout; không sửa DSP/controller.

## Lưu ý

- `data/mode_settings.json` đang có thay đổi dữ liệu trước đó, không tự reset.
- Chỗ tone “Đang dò tone” được yêu cầu giữ logic hiện tại; các handle tone compatibility vẫn cần tồn tại dù không render card UI cũ.
- Blender 4.5 executable đã dùng trước đó tại:
  `C:\Program Files\Blender Foundation\Blender 4.5\blender.exe`
