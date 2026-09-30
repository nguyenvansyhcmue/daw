# THM Vocal Panel

## Mục đích app
Ứng dụng phân tích âm nhạc dành cho karaoke phòng thu:
- Nhận URL YouTube từ người dùng, tải audio về (qua `yt_dlp`)
- Phân tích **20–30 giây đầu** của bài để xác định **âm chủ (key)** và **thang âm (scale)**
- Kết quả phải chính xác để gửi CC (control change) đến autotune — tune giọng hát người dùng không bị méo
- Độ chính xác là ưu tiên số 1, tốc độ phân tích là ưu tiên số 2

## Công nghệ chính
- **UI**: PyQt với stylesheet tùy chỉnh tại `ui/theme.qss`
- **Phân tích âm thanh**: `librosa` (pitch detection, chromagram, key estimation)
- **Machine learning**: `sklearn` (ensemble, preprocessing, neighbors) — model phân loại key/scale
- **Tải audio**: `yt_dlp` — tải từ YouTube về local
- **Điều khiển DAW**: trạng thái nội bộ StudioForge — không dùng MIDI port hay DAW ngoài
- **Thu âm live**: `sounddevice` — input microphone real-time
- **Cloud data**: `gspread` + `oauth2client` — lưu/đọc dữ liệu từ Google Sheets
- **Đóng gói**: PyInstaller với file config `THM_Vocal_Panel.spec`

## Cấu trúc project
```
THM_Vocal_Panel/
├── main.py                  # Entry point — khởi tạo PyQt app, điều phối các module
├── THM_Vocal_Panel.spec     # PyInstaller config — KHÔNG sửa trừ khi thêm dependency mới
├── service_account.json     # Google API credentials — KHÔNG BAO GIỜ SỬA hoặc commit
├── assets/                  # Icons, images, audio samples — giữ nguyên cấu trúc thư mục
│   └── logo.ico
├── ui/
│   └── theme.qss            # Stylesheet PyQt toàn app — chỉ sửa khi có yêu cầu UI rõ ràng
└── [các module chức năng]   # Chia theo chức năng rõ ràng (xem bên dưới)
```

## Các module chức năng (nhiều file, chia rõ ràng)
Khi thêm tính năng mới, hãy **đọc tất cả các module liên quan trước** để hiểu luồng dữ liệu hiện tại, tránh duplicate logic.

Module liên quan đến pipeline chính:
1. **Tải audio** — dùng `yt_dlp`, xử lý URL YouTube, convert sang định dạng phù hợp cho librosa
2. **Phân tích pitch/key** — dùng `librosa` chromagram + `sklearn` model
3. **Cập nhật trạng thái điều khiển** — giữ key/scale và hiệu ứng trong StudioForge
4. **Ghi log/dữ liệu** — dùng `gspread` lưu kết quả lên Google Sheets
5. **UI Panel** — PyQt widgets, kết nối signal/slot với các module trên

## Quy tắc bắt buộc

### Bảo mật
- **KHÔNG BAO GIỜ** đọc, sửa, in nội dung, hoặc commit file `service_account.json`
- Nếu cần test Google Sheets, dùng mock data thay vì credentials thật

### Quy trình làm việc
- **Luôn chạy test** (hoặc chạy thử app) trước khi báo hoàn thành một task
- Nếu thêm thư viện mới, cập nhật `THM_Vocal_Panel.spec` vào mục `hiddenimports`
- Khi sửa logic phân tích key/scale: phải test với ít nhất 2–3 bài có key khác nhau
- Ưu tiên **độ chính xác** hơn tốc độ trong mọi thay đổi liên quan đến pitch detection

### Phong cách code
- Tiếng Việt trong comment nếu logic liên quan đến nghiệp vụ âm nhạc
- Tiếng Anh trong tên biến, tên hàm, tên class
- Giữ nguyên cấu trúc thư mục `assets/` — không di chuyển hoặc đổi tên file trong đó

## Luồng chính (pipeline)
```
URL YouTube
    ↓ yt_dlp (tải + convert)
Audio file (local, tạm thời)
    ↓ librosa (load 20-30s đầu)
Chromagram / pitch data
    ↓ sklearn model
Key + Scale (vd: "C Major", "A Minor")
    ↓ điều khiển nội bộ StudioForge
Key/scale/effect state
    ↓ gspread (log kết quả)
Google Sheets
```

## Ngữ cảnh domain quan trọng
- **Key**: âm chủ của bài (C, D, E, F, G, A, B + # hoặc b) — 12 khả năng
- **Scale**: thang âm (Major / Minor / có thể mở rộng thêm các mode khác)
- **CC (Control Change)**: tín hiệu MIDI gửi đến autotune để set key tune
- Người dùng là ca sĩ/nhạc sĩ — interface phải trực quan, kết quả hiển thị rõ ràng (không chỉ số kỹ thuật)
- Mỗi sai lầm về key sẽ khiến autotune tune sai → giọng hát bị méo — đây là lỗi nghiêm trọng nhất

## Lệnh hữu ích
```bash
# Chạy app để test
python main.py

# Đóng gói thành .exe
pyinstaller THM_Vocal_Panel.spec

# Kiểm tra thư viện đã cài
pip list | grep -E "librosa|sklearn|sounddevice|yt-dlp|gspread"
```
