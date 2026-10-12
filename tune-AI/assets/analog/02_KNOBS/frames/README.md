# THM knob-frame contract

Đây là vị trí duy nhất cho frame Blender của núm động.

```text
frames/
├── main/00.png … 63.png
├── small/00.png … 63.png
```

- `00.png` tương ứng `-135°`, `63.png` tương ứng `+135°`.
- Frame phải là PNG RGBA, nền trong suốt, 256×256 px.
- Toàn bộ frame cùng một family phải dùng cùng camera, ánh sáng và pivot Blender.
- Tune dùng family `main`; khác biệt của Tune là dải giá trị `-50..+50`, không
  phải một model Blender thứ ba. File `.blend` chỉ có `KnobMain_*` và
  `KnobSmall_*`; không được tạo một prefix `KnobTune_*` giả.
- Không dùng các frame này trên `THM_full_app_reference.png`: ảnh đó đã có núm tĩnh.
- Trước khi runtime nạp frame, phải thay chassis bằng bản **pointerless / knobless**. Khi đó mỗi vùng núm được render đúng một lần từ frame tương ứng.

Xuất frame từ Blender 4.5:

```bash
cd /Users/hiimdrdey/code/daw/tune-AI
/Applications/Blender.app/Contents/MacOS/Blender \
  -b assets/analog/blender_render/THM_hardware_components.blend \
  -P tools/render_knob_frames.py -- \
  --output assets/analog/02_KNOBS/frames --size 256
```
