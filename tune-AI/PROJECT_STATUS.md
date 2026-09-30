# THM Vocal Panel — Project Status & Architecture

> **File này là tổng hợp đầy đủ project — nạp vào đầu hội thoại mới để Claude hiểu ngay context, tránh phải đọc lại từ đầu.**
>
> **Ngày cập nhật**: 2026-05-07
> **Phiên bản pipeline detect lên tone cuối bài**: `v7.15_novelty_crosscheck`
> **Trạng thái**: ✅ Pipeline detect ổn định 3/4 bài test (sai ±0-3s) | ✅ UI workflow 3-tầng hoàn chỉnh | ✅ CDP auto-fix đã verify | ✅ **Toàn bộ Option B (V7.15 + UI V2 + CDP + yt-dlp + realtime + docs) đã merged vào `main` trong 6 commit**

---

## 📋 Mục lục

1. [Mục đích app](#1-mục-đích-app)
2. [Công nghệ chính](#2-công-nghệ-chính)
3. [Cấu trúc project](#3-cấu-trúc-project)
4. [Workflow tổng thể](#4-workflow-tổng-thể-từ-paste-url-đến-apply-tone)
5. [Pipeline phát hiện lên tone cuối bài (V7.15)](#5-pipeline-phát-hiện-lên-tone-cuối-bài-v715)
6. [Cơ chế đồng bộ thời gian YouTube](#6-cơ-chế-đồng-bộ-thời-gian-youtube)
7. [UI Layout](#7-ui-layout)
8. [Auto-save & Correction Learning](#8-auto-save--correction-learning)
9. [Audio Caching](#9-audio-caching)
10. [Quy tắc bảo mật + phong cách code](#10-quy-tắc-bảo-mật--phong-cách-code)
11. [Lệnh hữu ích](#11-lệnh-hữu-ích)
12. [Lịch sử nâng cấp pipeline detection](#12-lịch-sử-nâng-cấp-pipeline-detection-v70--v715)
13. [Tình trạng công việc hiện tại](#13-tình-trạng-công-việc-hiện-tại)
14. [Lộ trình tiếp theo](#14-lộ-trình-tiếp-theo)

---

## 1. Mục đích app

Ứng dụng phân tích âm nhạc cho karaoke phòng thu chuyên nghiệp:
- Nhận URL YouTube → tải audio → phân tích **âm chủ (key) và thang âm (scale)** chính xác
- Giữ key/scale đã dò trong trạng thái điều khiển nội bộ StudioForge, không kết nối DAW ngoài
- Người hát không bị méo giọng nhờ autotune set đúng key
- Phát hiện **đoạn lên tone cuối bài** (modulation +1/+2/+3 semitone, đặc biệt phổ biến ở Vpop)
- Tự đổi key/scale khi YouTube chạm mốc thời gian lên tone

**Độ chính xác là ưu tiên số 1**, tốc độ phân tích là ưu tiên số 2.

**Ngữ cảnh user**: Ca sĩ/nhạc sĩ hát karaoke chuyên nghiệp. Mỗi sai key sẽ làm autotune tune sai → giọng méo → trải nghiệm cực tệ.

---

## 2. Công nghệ chính

| Lớp | Công nghệ | Vai trò |
|---|---|---|
| **UI** | PyQt6 + PyQt6-WebEngine | Giao diện chính + YouTube nội bộ |
| **Stylesheet** | `ui/theme.qss` | Theme tối, font Segoe UI 9px |
| **Phân tích âm thanh** | `librosa 0.11.0` | HPSS, chroma_cqt, beat tracking, SSM novelty |
| **Tín hiệu DSP** | `scipy.signal` | medfilt, find_peaks |
| **Machine learning** | `sklearn` | (Legacy V1 detect tone gốc) |
| **Tải audio** | `yt-dlp 2026.3.x` | Download YouTube với multi-client bypass bot detection |
| **Điều khiển DAW** | StudioForge internal state | Không dùng cổng MIDI hay DAW ngoài |
| **Thu âm live** | `sounddevice` | Realtime input mic |
| **Cloud** | `gspread` + `oauth2client` | Lưu/đọc Google Sheets |
| **Kết nối Brave** | `requests` + `websocket-client 1.9+` | Chrome DevTools Protocol để đọc currentTime |
| **Đóng gói** | PyInstaller (`THM Vocal Panel.spec`) | Build .exe |

---

## 3. Cấu trúc project

```
THM_Vocal_Panel/  (root: C:\Users\truye\Documents\toolv2\)
├── main.py                      # Entry point PyQt
├── THM Vocal Panel.spec         # PyInstaller config
├── service_account.enc          # Google API credentials (encrypted, KHÔNG ĐỘNG)
├── requirements.txt.txt         # Dependencies (lưu ý có .txt.txt)
├── assets/                      # Icons, images, audio samples
│   └── logo.ico
├── ui/
│   ├── main_window.py           # ~3700 dòng — UI chính + tất cả handlers
│   ├── youtube_panel.py         # YouTube nội bộ (in-app QWebEngineView)
│   ├── settings_dialog.py
│   ├── song_manager_dialog.py
│   └── theme.qss                # Stylesheet (font 9px tối thiểu)
├── core/
│   ├── app_controller.py        # ~1100 dòng — Logic trung tâm
│   ├── app_state.py
│   ├── constants.py
│   ├── models.py
│   └── services/
│       ├── tone_service.py             # V1 detect tone gốc + Krumhansl
│       ├── end_modulation_service.py   # ⭐ V7.15 detect lên tone cuối bài
│       ├── tone_cache_service.py       # Cache tone + correction history
│       ├── realtime_tone_service.py    # Mic input realtime
│       ├── browser_launcher_service.py # ⭐ Launch Brave với CDP flags
│       ├── browser_monitor_service.py  # ⭐ Đọc tabs Brave qua CDP HTTP API
│       ├── midi_service.py             # trạng thái điều khiển nội bộ StudioForge
│       ├── data_transfer_service.py
│       ├── license_service.py          # Activation key + service_account.enc
│       ├── settings_service.py
│       ├── mode_settings_service.py
│       ├── preset_service.py
│       ├── song_service.py
│       ├── credentials_service.py
│       └── autokey_service.py
├── data/
│   ├── app_settings.json
│   ├── autokey_config.json
│   ├── songs.json                    # D.S BÀI HÁT (KHÔNG bị xóa khi clear cache)
│   ├── tone_cache.json               # Cache tone gốc + end_modulation
│   ├── correction_history.json       # User correction học dần
│   ├── end_modulation_last_result.json  # Log debug pipeline
│   ├── mode_presets.json
│   └── mode_settings.json
└── .claude/
    └── worktrees/keen-liskov-3b589c/  # Worktree Claude làm việc
```

**Worktree quan trọng**: Claude làm việc tại `.claude/worktrees/keen-liskov-3b589c/`. User chạy `python main.py` từ đây để test code mới nhất.

---

## 4. Workflow tổng thể (từ paste URL đến apply tone)

```
┌─────────────────────────────────────────────────────────────────┐
│ [1] User paste URL YouTube vào Brave (qua nút KARAOKE)          │
│     hoặc panel YouTube nội bộ                                   │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [2] Pipeline V1 (tone_service.detect_segment) detect tone gốc   │
│     ~3s → hiển thị "TONE: F# Minor" (cyan box)                  │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
        ┌────────────────┼────────────────┐
        ↓                ↓                ↓
    [Đúng]           [Sai]            [Vẫn sai]
   không              ↓                  ↓
   thao tác   [Bấm FIX TONE]      [Bấm SỬA TONE]
                   ↓                  ↓
              V2 deep fix         Manual entry
              (~5-10s)           (instant)
        └────────────────┼────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [3] Schedule analyze end mod sau 20s (auto) hoặc 10s (FIX/SỬA)  │
│     - Trong khi chờ: status line "🔄 Đang dò lên tone cuối bài" │
│     - Worker thread chạy V7.15 pipeline                         │
│       (HPSS + chroma_cqt + likelihood + novelty + beats)        │
│     - Lần đầu: ~15-25s, Re-analyze (cache hit): ~2-3s           │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [4] AUTO-SAVE (NO POPUP) khi tìm thấy candidate                 │
│     - Lưu vào tone_cache.json (manual_confirmed=False)          │
│     - Set self.active_end_modulation                            │
│     - Status line cập nhật + 2 nút SỬA/HỦY xuất hiện trong      │
│       MODE CARD                                                  │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [5] Timer 200ms (end_modulation_apply_timer) tự động:           │
│     - Đọc currentTime YouTube qua CDP (extrapolate giữa polls)  │
│     - Nếu currentTime >= apply_time → gửi MIDI tone CUỐI        │
│     - Nếu currentTime < apply_time-2 → gửi MIDI tone GỐC        │
│     - Status line hiển thị 🟢/🔴/⚪ trạng thái sync             │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [6] User can interact ANY TIME:                                 │
│     - ✏️ SỬA MOD CUỐI: mở dialog edit raise_time/key/scale     │
│       → save → manual_confirmed=True (correction learning)      │
│     - 🚫 HỦY LẦN NÀY: clear active_end_modulation tạm thời      │
│       (cache giữ nguyên, bài sau load lại sẽ active)            │
└────────────────────────┬────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│ [7] Lần sau load cùng URL:                                      │
│     - Có manual_confirmed=True → instant load, SKIP re-analyze  │
│     - Không có → pipeline chạy lại như cũ                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Pipeline phát hiện lên tone cuối bài (V7.15)

**File**: `core/services/end_modulation_service.py`
**Class chính**: `EndModulationService`
**Algorithm version**: `v7.15_novelty_crosscheck`

### Constants quan trọng

```python
TAIL_SECONDS_DEFAULT = 150     # Phân tích 150s cuối bài
KEY_WINDOW_SEC = 5.0           # Window key likelihood
KEY_HOP_SEC = 0.5              # Hop key likelihood
CHROMA_HOP_LENGTH = 1024       # ~46ms/frame @ 22050Hz
ARGMAX_MEDIAN_KERNEL_FRAMES = 7  # Smoothing
APPLY_LEAD_SEC = 0             # Apply ngay tại raise_time, không sớm 3s nữa
SEMITONE_TARGETS = (1, 2, 3)   # Detect modulation +1/+2/+3 semitone
```

### Pipeline 8 bước

```
[A] Tải FULL audio qua ffmpeg (1 call, ~5-15s)
[B] HPSS — librosa.effects.hpss(margin=3.0): tách hài âm/percussive
[C] chroma_cqt + bins_per_octave=36 + median 1.5s smoothing
[D] Sliding key likelihood window 5s/hop 0.5s × 24 templates Krumhansl
[E] SSM novelty (Foote checkerboard kernel 8s)
[F] Beat tracking trên y_percussive (madmom→librosa fallback)
[G] V7.11 paradigm — Summed unified likelihood:
    Cho mỗi delta ∈ {1, 2, 3}:
        target_unified = (base_unified + delta) % 12
        target_score[t] = sum likelihood của Major i + relative Minor (i-3)
        diff[t] = target_score[t] - base_score[t]
        smoothed = rolling mean 5s
    Tìm first crossing pass:
        - tail persistence ≥ 35% (target dominate đến cuối)
        - sustained 30s/70% sau t_star
        - novelty ≥ 0.20 trong bán kính 5s (V7.15 cross-check)
[H] Bias compensation: t_star -= window/2 (~2.5s)
[I] Beat snap về beat gần nhất (±1.5s)
```

### Output schema

```python
{
    "ok": True,
    "has_modulation": bool,         # confidence >= 0.80
    "candidate_found": bool,         # >= 0.50
    "title": str, "duration": int, "duration_text": "MM:SS",
    "base_key": "F#", "base_scale": "Minor", "base_label": "F# Minor",
    "semitone_up": 1,
    "new_key": "G", "new_scale": "Minor", "new_label": "G Minor",
    "raise_time_sec": 227, "raise_time_text": "03:47",
    "apply_time_sec": 227,            # = raise_time vì APPLY_LEAD_SEC=0
    "apply_time_text": "03:47",
    "confidence": 0.665,
    "algorithm_version": "v7.15_novelty_crosscheck",
    "debug": {                        # Rich debug info
        "transitions_total": int,
        "candidates_raw": int,
        "rej_scale_diff": int,
        "rej_delta_invalid": int,
        "rej_base_mismatch": int,
        "rej_history_short": int,
        "rej_stability_before": int,
        "rej_after_imm_short": int,
        "rej_stability_after_imm": int,
        "rej_persistence": int,
        "transition_log": list[dict],
        "all_candidates": list[dict],
    },
}
```

### Kết quả test 4 bài thực tế (V7.15)

| URL | Bài thật | App báo | Sai số | Status |
|---|---|---|---|---|
| wmxNJRs2zrU | F#m→Gm at 03:47 | 03:47 | **0s** | ✅ Hoàn hảo |
| Tqv0x1_QJlo | C→D at 03:14 | 03:12 | -2s | ✅ Acceptable |
| p4AOrAjZKr0 | Gm→G#m at 03:59 | 03:56 | -3s | ✅ Acceptable |
| 2cvjiNrotgU | Gm→Am at 03:39 | 03:21 | **-18s** | ❌ Chấp nhận (transient bridge) |

→ **3/4 bài đạt mức ±0-3s** (đáp ứng yêu cầu của user). Bài 4 fail do transient bridge có drum fill — pipeline chưa phân biệt được transient với modulation thật.

### Audio caching (P1)

`EndModulationService._cached_processor` lưu Processor đã chạy HPSS+chroma+likelihood+novelty+beats theo `video_id`. Khi user FIX/SỬA TONE → re-analyze chỉ chạy lại modulation detection (~2-3s thay vì 15-25s).

```python
# UI gọi khi đổi bài
controller.invalidate_end_modulation_audio_cache()
```

---

## 6. Cơ chế đồng bộ thời gian YouTube

**File chính**: `ui/main_window.py:_get_current_youtube_time_seconds()` + `core/app_controller.py:get_external_youtube_playback_state()`

### 3 cơ chế lấy currentTime (theo thứ tự ưu tiên)

```python
1. internal_youtube  — YouTube nội bộ in-app
                       (đọc trực tiếp <video> element qua QWebEngine)
2. brave_cdp         — Brave external qua Chrome DevTools Protocol (websocket)
3. video_changed     — YouTube radio chuyển bài → clear active_end_modulation
4. unavailable       — KHÔNG đọc được → return None → app KHÔNG apply MIDI
                       (đã BỎ fallback_elapsed vì đếm wallclock không sync)
```

### Realtime extrapolation

Giảm lag từ 1000ms → ~50ms:

```python
# youtube_panel.py: lưu wallclock mỗi tick poll
self.last_poll_wallclock = time.time()

# main_window: extrapolate giữa các poll
base_time = self.youtube_browser.last_current_time
if is_playing:
    elapsed = time.time() - self.youtube_browser.last_poll_wallclock
    if elapsed <= 1.5:  # cap để tránh drift
        base_time += elapsed
```

### Tham số timing

| Component | Giá trị | Tác dụng |
|---|---|---|
| YouTube poll | **250ms** (giảm từ 1000ms) | Cập nhật currentTime nhanh hơn |
| Apply timer | **200ms** (giảm từ 700ms) | Check & gửi MIDI nhanh hơn |
| URL/title poll | mỗi 12 ticks (~3s) | Không cần thường xuyên |
| Total worst-case lag | **~400ms** | Trước fix: ~2-10s |

### CDP Setup

**Brave launcher** (`browser_launcher_service.py`):

```python
args = [
    brave_exe,
    f"--remote-debugging-port=9222",
    "--remote-allow-origins=*",  # CRITICAL: Brave/Chromium v107+ cần flag này
    f"--user-data-dir={profile_dir}",
    "--new-window",
    start_url,
]
```

**WebSocket connect** (`app_controller.py:get_external_youtube_playback_state`):

```python
ws = websocket.create_connection(
    ws_url,
    timeout=1.5,
    origin="http://localhost",  # bypass strict origin check
)
```

### CDP Diagnostic + Auto-fix

Khi CDP fail, app tự diagnose và offer 1-click fix:

```python
diag = controller.diagnose_brave_cdp()
# {is_brave_running: bool, cdp_port_open: bool, tabs_count: int, error: str}

if is_brave_running and not cdp_port_open:
    # Brave chạy nhưng thiếu CDP flag → offer auto-restart
    if user_confirms:
        controller.restart_brave_with_cdp()
        # = close_all_brave() + open_brave_for_auto_detect()
```

### Video change detection

Khi YouTube radio auto-play chuyển bài:
- CDP trả về `actual_video_id` ≠ `requested_video_id`
- App tự động clear `active_end_modulation`
- Invalidate audio cache
- Status line đổi sang ⚪
- Pipeline sẽ re-analyze cho bài mới khi user thao tác

### Status line indicator

| Emoji | Trạng thái | Ý nghĩa |
|---|---|---|
| 🟢 | brave_cdp / internal_youtube | Đồng bộ tốt |
| 🔴 | unavailable | Không kết nối → app KHÔNG apply MIDI (an toàn) |
| ⚪ | video_changed | YouTube đã chuyển bài |

### Console log throttled (mỗi 5s)

```
[End mod sync] time_source=brave_cdp | current_time=227.3s
```

---

## 7. UI Layout

**Window**: `setFixedSize(720, 430)` — KHÔNG giãn nở. Mọi widget phải fit trong layout cố định.

### Bố cục 3 cột

```
┌────────────────── Top Header (Bản Pro V3 | HSD) 40px ──────────────────┐
├────────── LEFT COL ─────┬─── MID COL ─────┬─── RIGHT COL ──────────────┤
│ TONE CARD (190px)       │ EFFECT CARD     │ VOLUME CARD                │
│  - MIC selector         │  - Vang Ngắn    │  - Mic Vol                 │
│  - TONE: F# Minor       │  - Dài          │  - Music Vol               │
│  - Progress 100%        │  - Echo         │  - Tune                    │
│  - Status line          │                 │                            │
│  - 3 nút TỰ ĐỘNG/FIX/   │                 │                            │
│    SỬA TONE             │                 │                            │
│  - Tăng/Giảm Tone       │                 │                            │
│  - [-] +0 [+]           │                 │                            │
│                         │                 │                            │
│ MODE CARD (190px)       │ MONITOR CARD    │ HỆ THỐNG CARD              │
│  - CHẾ ĐỘ HÁT           │  - VANG ON      │  - KARAOKE  - LIÊN HỆ      │
│  - LOFI / NHẠC TRẺ      │  - MIC ON       │  - LƯU BÀI HÁT - D.S       │
│  - BOLERO / REMIX       │                 │  - CÀI ĐẶT - THOÁT         │
│  - [Divider khi active] │                 │                            │
│  - 🎵 +1 → G Min @ 03:47│                 │                            │
│  - [✏️ SỬA] [🚫 HỦY]    │                 │                            │
└─────────────────────────┴─────────────────┴────────────────────────────┘
```

### Compact mode khi có active_end_modulation

Khi `active_end_modulation` được set, em **dynamic shrink** tone card 34px để mode card có chỗ cho 2 nút mới:

| Element | Default | Compact (khi có active) |
|---|---|---|
| 3 nút TỰ ĐỘNG/FIX TONE/SỬA TONE | 86×38 | **78×32** |
| Dòng "Tăng/Giảm Tone" | hiện | **ẩn** |
| Label "TONE: F# Minor" | height 34 | height **28** |
| Nút [-] và [+] tone | 56×34 | 56×**30** |
| Tổng tiết kiệm | | **34px** |

→ Window vẫn 720×430, không giãn nở.

### 2 nút end mod actions

| Nút | Style | Chức năng |
|---|---|---|
| **✏️ SỬA MOD CUỐI** | 90×28, blue #4d55c6, font 9px | Mở dialog edit `raise_time_text` + key/scale → save → set `manual_confirmed=True` |
| **🚫 HỦY LẦN NÀY** | 90×28, orange #c2410c, font 9px | Confirm dialog → clear `active_end_modulation` (memory only, cache giữ) → revert tone gốc tức thời |

Visibility: toggle theo `bool(active_end_modulation)` — ẩn hoàn toàn khi không có active.

---

## 8. Auto-save & Correction Learning

### Auto-save workflow (V2 — bỏ popup)

Khi pipeline tìm thấy candidate trong `_handle_end_modulation_result`:

```python
# KHÔNG hiện popup (theo yêu cầu user)
saved = controller.save_end_modulation_for_youtube(
    youtube_url=url,
    title=title,
    modulation_result=result,
    base_result=...,
)
end_mod = saved.get("end_modulation", {})
end_mod["manual_confirmed"] = False   # auto-save chưa được user verify
end_mod["auto_saved"] = True
self.active_end_modulation = end_mod
self._update_end_modulation_status_line()
# Console log thay popup:
print(f"[Auto-save end mod] {base_label} → {end_label} @ {raise_time} | confidence {pct}%")
```

### Correction learning (P2.2)

Khi user bấm **✏️ SỬA MOD CUỐI** → dialog → save:

```python
end_mod_new["manual_confirmed"] = True   # User đã verify
end_mod_new["auto_saved"] = False
```

Trong `_schedule_end_modulation_check`, KHI có cache với `manual_confirmed=True`:

```python
if saved.get("manual_confirmed"):
    # Tin tuyệt đối, KHÔNG re-analyze
    self.active_end_modulation = saved
    return  # skip pipeline
```

→ Lần sau hát cùng bài: instant load, không phân tích lại, dùng giá trị anh đã verify.

### URL fallback chain (4 nguồn) trong `on_edit_end_mod_clicked`

```
1. active_end_modulation.canonical_url  (đã save từ trước)
2. _get_active_youtube_context().url    (Brave/in-app đang phát)
3. current_song_playing.youtube_url     (user pick từ song list)
4. youtube_browser.current_video_id     (in-app browser internal)
```

→ Bất kỳ tình huống nào (paste URL, chọn từ list, Brave external) đều lấy được URL.

---

## 9. Audio Caching

**File**: `core/services/end_modulation_service.py`

```python
class EndModulationService:
    def __init__(self):
        self._cached_processor = None  # 1 video gần nhất

    def analyze_end_modulation(self, youtube_url, ...):
        video_id = self._extract_video_id_simple(youtube_url)
        if (cached and cached["video_id"] == video_id):
            # Cache hit — bỏ qua tải audio + HPSS + chroma + likelihood + novelty + beats
            proc = cached["processor"]
            # Chỉ chạy modulation detection (~2-3s)
        else:
            # Full pipeline (~15-25s)
            # Lưu cache cho lần sau
            self._cached_processor = {...}

    def invalidate_cache(self):
        self._cached_processor = None
```

UI gọi `controller.invalidate_end_modulation_audio_cache()` khi user đổi bài.

---

## 10. Quy tắc bảo mật + phong cách code

### Bảo mật (CRITICAL)

- ❌ **KHÔNG BAO GIỜ** đọc, sửa, in nội dung, hoặc commit file `service_account.json` / `service_account.enc`
- ❌ **KHÔNG BAO GIỜ** lưu credentials vào git
- ✅ Nếu cần test Google Sheets → dùng mock data
- ✅ File `.gitignore` đã loại trừ `service_account.enc`

### Quy tắc MIDI CC (CRITICAL)

- `apply_detected_tone()` trong `app_controller.py` **LUÔN** gửi key/scale đến CẢ MIC 1 (CC40/CC41) VÀ MIC 2 (CC66/CC67)
- Mọi handler nhận kết quả dò tone **PHẢI** gọi `_sync_end_modulation_base_key()` trước, rồi `_apply_runtime_tone_to_autotune()`
- **KHÔNG** ghi đè `base_tone_for_transpose` bằng tone đã transpose (gây double-transpose)
- `set_global_pitch()` chỉ gửi CC36 — không tự thêm CC khác nếu chưa xác nhận với user

### Quy trình làm việc

- Luôn chạy `python -m py_compile` để verify syntax sau mỗi edit
- Khi thêm thư viện mới → cập nhật cả `requirements.txt.txt` VÀ `THM Vocal Panel.spec` hiddenimports
- Khi sửa pipeline detect: test với ≥2-3 bài có key khác nhau
- Ưu tiên **độ chính xác** hơn tốc độ

### Phong cách code

- **Tiếng Việt** trong comment liên quan đến nghiệp vụ âm nhạc
- **Tiếng Anh** trong tên biến/hàm/class
- Giữ nguyên cấu trúc `assets/` — không di chuyển/đổi tên
- Không tạo file mới trừ khi thật sự cần

---

## 11. Lệnh hữu ích

```powershell
# Chạy app từ worktree (KHÔNG phải main folder)
cd C:\Users\truye\Documents\toolv2\.claude\worktrees\keen-liskov-3b589c
python main.py

# Đóng tất cả Brave (manual nếu cần test sạch)
Stop-Process -Name brave -Force -ErrorAction SilentlyContinue

# Test CDP port
Test-NetConnection -ComputerName 127.0.0.1 -Port 9222

# Cài dependencies vào venv user
"C:\Users\truye\Documents\toolv2\.venv\Scripts\python.exe" -m pip install <pkg>

# Compile-check toàn bộ
python -m py_compile ui/main_window.py ui/youtube_panel.py core/services/end_modulation_service.py core/app_controller.py core/services/browser_launcher_service.py

# Đóng gói thành .exe
pyinstaller "THM Vocal Panel.spec"
```

### Copy credentials sang worktree (1 lần khi tạo worktree mới)

```powershell
Copy-Item -Path "C:\Users\truye\Documents\toolv2\service_account.enc" `
          -Destination "C:\Users\truye\Documents\toolv2\.claude\worktrees\keen-liskov-3b589c\service_account.enc"
```

---

## 12. Lịch sử nâng cấp pipeline detection (V7.0 → V7.15)

| Version | Phát hiện chính | Fix |
|---|---|---|
| V7.0 | Pipeline gốc | Window 14s/hop 8s, lấy giữa cửa sổ → sai ±4-7s cố định |
| V7.1 | Persistence check + first crossing refine | Cải thiện nhẹ |
| V7.2 | Likelihood-driven thay novelty-driven | Vẫn bị bias window |
| V7.3 | **Window 4s = bias hệ thống** | Window 2s + chroma_cqt + bins=36 |
| V7.4 | Filter scale loại hết do relative pair | Nới ngưỡng + fallback novelty |
| V7.5 | Vẫn loại do scale_before==base check | Bỏ scale check + verbose logging |
| V7.6 | Major↔Minor confusion | **Unification helper** `_unify_to_major()` |
| V7.7 | Argmax flicker quá nhanh | Tăng smoothing tổng ~8s |
| V7.8 | Stability raw thấp do flicker | Stability check trên unified space |
| V7.9 | Transition record không sync với detection | Track transitions trong unified space + filter base_unified |
| V7.10 | Drift trung gian phá transition tracking | Rolling MODE thay median |
| **V7.11** | **PARADIGM SHIFT** | Summed unified likelihood + first crossing |
| V7.12 | Bias +2-3s do rolling mean | Bias compensation = window/2 |
| V7.13 | Transient bridge lừa first crossing | Sustained dominance check 30s/70% |
| V7.14 | Sustained 45s/75% phá bài 1 (over-strict) | Rollback + APPLY_LEAD_SEC=0 + delay 60s→20s |
| **V7.15** | **HIỆN TẠI** | Sustained 30s/70% + **novelty cross-check ≥ 0.20** |

### Chi tiết V7.11 paradigm shift (cốt lõi nhất)

Trước V7.11: track transitions trong argmax sequence (nhiễu vì argmax đi qua state trung gian).

V7.11+ đảo ngược paradigm:
- Tính `summed_likelihood[t, u]` = sum likelihood của 2 templates có unify == u (Major i + Minor (i-3))
- Cho mỗi delta ∈ {1, 2, 3}:
  - target_unified = (base_unified + delta) % 12
  - target_score[t] = summed_likelihood[t, target_unified]
  - diff[t] = target_score[t] - base_score[t]
  - Tìm first frame t* mà diff[t*] > 0 + sustained 30s/70% + novelty peak ≥ 0.20

→ Drift trung gian qua state khác **không ảnh hưởng** vì chỉ quan tâm 2 score.

---

## 13. Tình trạng công việc hiện tại

### ✅ Đã hoàn thành

#### Pipeline detection
- ✅ V7.15 stable cho 3/4 bài (sai ±0-3s)
- ✅ Algorithm version: `v7.15_novelty_crosscheck`
- ✅ Audio caching (P1) — re-analyze 2-3s thay vì 15-25s
- ✅ Correction learning (P2.2) — manual_confirmed → skip re-analyze

#### UI Workflow (3 tầng)
- ✅ **P0**: 2 nút SỬA MOD CUỐI / HỦY LẦN NÀY trong mode card
- ✅ **P0**: Compact tone card khi active end_mod (shrink 34px, không giãn window)
- ✅ **P0**: Visual feedback "🔄 Đang dò lên tone cuối bài..." trong status line
- ✅ **P0**: Sync indicator 🟢 / 🔴 / ⚪ trong status line
- ✅ **V2**: Bỏ popup, auto-save → status line + 2 nút (theo yêu cầu user)

#### Real-time apply
- ✅ Extrapolate currentTime giữa polls (1000ms → ~50ms lag)
- ✅ Apply timer 700ms → 200ms
- ✅ YouTube poll 1000ms → 250ms
- ✅ APPLY_LEAD_SEC = 0 (apply ngay tại raise_time)

#### CDP Brave
- ✅ Cài `websocket-client 1.9.0` vào `.venv` user + Python global + requirements
- ✅ Add `--remote-allow-origins=*` vào Brave launcher (fix 403 Forbidden)
- ✅ Add `origin="http://localhost"` vào websocket.create_connection
- ✅ 2-tier search tab (chính xác video_id → bất kỳ YouTube tab)
- ✅ Detect video change → auto clear active_end_modulation + invalidate cache
- ✅ BỎ fallback_elapsed (return None thay vì đếm sai)
- ✅ Diagnostic + Auto-fix: 1-click "RESTART BRAVE với CDP đúng" khi detect lỗi

#### Shutdown
- ✅ Đóng tất cả Brave khi user bấm THOÁT (`close_all_brave()` trong `controller.shutdown()`)

#### YouTube download
- ✅ yt-dlp multi-client bypass bot detection (android → ios → tv_embedded → web)
- ✅ User-Agent + Accept-Language headers

#### Bug fixes
- ✅ Font theme.qss 8px → 9px (fix DirectWrite "8514oem" warning)
- ✅ on_edit_end_mod_clicked: 4-tier URL fallback chain (fix "Không xác định được URL")
- ✅ Hardcode `apply_sec = raise_sec - 3` → `raise_sec` (sync với APPLY_LEAD_SEC=0)

### 🆕 Cập nhật 2026-05-07 — Đồng bộ git Option B

✅ **Đã merged 6 commit vào `main`** (e38dc18):
```
e38dc18 docs: thêm PROJECT_STATUS.md tổng hợp kiến trúc + lịch sử pipeline
0c44cbc feat(ui): UI workflow V2 - auto-save silent + 2 nút SỬA/HỦY + sync indicator
7230ea7 feat(realtime): extrapolate YouTube currentTime, giảm poll lag 1000->250ms
342afc2 feat(cdp): Brave CDP fix + diagnostic + auto-restart
86e32ef feat(download): yt-dlp multi-client bypass YouTube bot detection
2b529a4 feat(detect): V7.15 pipeline phát hiện lên tone cuối bài
```
→ Repo trước đây có 2 worktree (`happy-fermi-078208` và `keen-liskov-3b589c`) chứa code chưa commit. Đã commit toàn bộ và xoá worktree thừa. Giờ chỉ làm việc trên `main` ở `C:\Users\truye\Documents\toolv2\`.

---

### 🆕 Cập nhật 2026-05-07 session 2 — Fix đồng bộ MIDI MIC 2

#### Background: vấn đề MIC 2 không nhận tone

AutoTune Pro trong Cubase có 2 bộ riêng biệt: MIC 1 (CC40/CC41) và MIC 2 (CC66/CC67).
- MIC 1 luôn nhận đúng key/scale sau khi dò tone ✅
- MIC 2 **không được cập nhật** sau khi dò tone ❌ — vẫn giữ giá trị cũ

#### Root cause

4 handler xử lý kết quả dò tone chỉ cập nhật biến UI, KHÔNG gọi `apply_detected_tone()`:
```python
# CŨ (sai):
self.current_detected_tone = {"key": key, ...}
self._update_transposed_tone_label()

# MỚI (đúng):
self._sync_end_modulation_base_key(key, scale)
self._apply_runtime_tone_to_autotune(key, scale, stage="base")
```

Thêm vào đó: RUNTIME timer `_check_saved_end_modulation_auto_apply` chạy mỗi 200ms đọc
`active_end_modulation["base_key"]` (lưu từ file, ví dụ "C Minor") và ghi đè lên tone vừa dò.

#### Fix đã làm (file `ui/main_window.py`)

| # | Handler | Fix |
|---|---|---|
| 1 | `_handle_external_browser_tone_result` | Gọi `_sync_end_modulation_base_key` + `_apply_runtime_tone_to_autotune` |
| 2 | `_handle_fix_tone_result` | Gọi `_sync_end_modulation_base_key` + `_apply_runtime_tone_to_autotune` |
| 3 | SỬA TONE confirm (~line 3480) | Gọi `_sync_end_modulation_base_key` + `_apply_runtime_tone_to_autotune` |
| 4 | Thêm helper `_sync_end_modulation_base_key()` | Ngăn RUNTIME timer đè tone cũ |

```python
# THÊM MỚI — main_window.py sau _apply_runtime_tone_to_autotune
def _sync_end_modulation_base_key(self, key: str, scale: str):
    """Khi detect tone mới, cập nhật base_key trong active_end_modulation (in-memory).
    Ngăn RUNTIME timer đè lại tone cũ (C Minor) lên cả 2 mic mỗi 200ms."""
    if isinstance(self.active_end_modulation, dict):
        self.active_end_modulation["base_key"] = key
        self.active_end_modulation["base_scale"] = scale
```

#### Fix `close_cubase_gracefully()` (file `core/services/cubase_service.py`)

Trước đây là stub `return False`. Đã implement đầy đủ:
```python
def close_cubase_gracefully(self):
    """Gửi WM_CLOSE đến Cubase (không /F) để Cubase hỏi lưu project."""
    for proc in psutil.process_iter(["name", "pid"]):
        if "cubase" in (proc.info.get("name") or "").lower():
            pids.append(proc.info["pid"])
    for pid in pids:
        subprocess.run(["taskkill", "/PID", str(pid)], ...)
    return bool(pids)
```

#### CC Mapping đầy đủ (từ file `core/constants.py`)

```
MIC1: at_key=CC40, at_scale=CC41, pitch/tone=CC36 (SoundShifter)
MIC2: at_key=CC66, at_scale=CC67, pitch/tone=CC101 (trong mapping vật lý)
```

> **⚠️ Câu hỏi mở:** CC 101 (MIC2 pitch) chưa được gửi trong `set_global_pitch()`.
> Hiện tại chỉ gửi CC36. Cần hỏi user xem MIC2 có plugin SoundShifter riêng không.
> Nếu có → cần thêm `self.midi.send_cc(102, cc_value)` vào `set_global_pitch()`.
> **TRẠNG THÁI: CẦN TEST + HỎI USER trước khi implement.**

#### Trạng thái test

- ✅ Code đã sửa, đã commit vào worktree `happy-fermi-078208`
- ⚠️ **User chưa test lại lần cuối** để xác nhận MIC 2 nhận tone đồng thời với MIC 1

### ⚠️ Known issues

- ⚠️ Bài có transient bridge dài + có drum fill: pipeline V7.15 không phân biệt được transient với modulation thật (bài `2cvjiNrotgU` lệch -18s — chấp nhận được, không tinh chỉnh thêm theo yêu cầu user)
- ⚠️ Khi YouTube radio auto-play sang bài MỚI mà pipeline chưa kịp re-analyze: sync indicator có thể hiện ⚪ vài giây trước khi reset

---

## 14. Lộ trình tiếp theo

### 🔴 P0 — Việc cần làm tiếp theo (user request 2026-05-07)

#### 14.1. Dọn popup workflow + thêm 3 popup CDP đa Brave

User yêu cầu **chỉ giữ tối thiểu popup workflow**, chỉ hiển thị popup khi nhiều Brave chạy gây CDP fail. Hiện tại app còn ~10 popup workflow cần xử lý:

**Popup workflow cần BỎ** (auto-save silent thay popup):
- `main_window.py:1871` `_silent_save_edit_skip("Lên tone cuối bài", ...)` — popup LƯU/SỬA/BỎ QUA khi pipeline tìm thấy modulation
- `main_window.py:1906` `_silent_message("Đã lưu", ...)` — confirm sau khi lưu end mod
- `main_window.py:2437` `QMessageBox.information(self, "FIX TONE", msg)` — confirm sau FIX TONE
- `main_window.py:2461`, `2641`, `2753`, `2914` — các popup confirm khác (cần Read để xác định context)

**Popup lỗi NÊN GIỮ** (rare, critical):
- License KEY (main.py:53,60,66), Lỗi khởi động, Lỗi mở Brave, Lỗi Auto-Key, Lỗi FIX TONE, "Chưa có bài YouTube", Lỗi lưu, các popup trong Settings/Song Manager dialog, nút LIÊN HỆ

**Popup MỚI cần thêm** — 3 case CDP đa Brave (theo screenshot user gửi):

| Case | Tình trạng | Hành động popup |
|---|---|---|
| Brave đang chạy + port 9222 đóng | User mở Brave thường (không qua KARAOKE) | Hỏi: "Đồng ý cho app tự khởi động lại Brave?" → 1-click fix: kill + relaunch với flag đúng |
| Brave KHÔNG chạy | Chưa có Brave nào | Hướng dẫn bấm KARAOKE |
| Brave + CDP OK nhưng không có YouTube tab | User phát bài khác hoặc đóng tab | Hướng dẫn check tab |

→ Function `diagnose_cdp()` đã có sẵn trong `core/services/browser_launcher_service.py`. Cần wiring UI logic để hiện đúng popup theo từng case.

#### 14.2. Chuẩn bị cho việc bán hàng loạt

User lo ngại khi đóng gói bán hàng loạt:
- ❌ **Risk lớn nhất: yt-dlp + bot detection**. .exe pin yt-dlp version → khi YouTube break thì TOÀN BỘ user .exe chết đồng loạt
- 📋 Cần làm trước phát hành:
  1. **Auto-update yt-dlp** khi app khởi động (tải binary từ GitHub releases)
  2. **Cookies support** (Settings → "Import cookies.txt từ Brave")
  3. **License server riêng** (1 KEY = 1 máy, mỗi user có credential Google riêng — tránh share `service_account.enc` cho tất cả user)
  4. **Brave installer kèm** hoặc hướng dẫn rõ trong setup
  5. **Telemetry/log lỗi tập trung** để biết user gặp lỗi gì khi YouTube break
  6. **Auto-update app** khi có bản mới

### 🟢 P2 (nice-to-have)

- [ ] **Bulk correction → tự tune bias compensation**: thu thập correction history, nếu 5+ bài cùng pattern lệch +2s → tự update bias_compensation_frames trong pipeline
- [ ] **Pre-analyze ngay khi paste URL**: tải audio + analyze tone gốc + analyze end mod song song với việc user thao tác (giảm cảm nhận chờ)
- [ ] **Multi-modulation support**: detect 2 modulation trong cùng bài (ví dụ: lên +1 lúc 02:30, lên thêm +1 lúc 04:00)

### 🔵 P3 (long-term)

- [ ] **Dashboard analytics**: thống kê accuracy detection theo thời gian, theo genre
- [ ] **Web UI** thay PyQt (nếu cần multi-platform)
- [ ] **Cloud sync settings** giữa nhiều máy
- [ ] **Voice command** (bật mic, đổi tone, etc.)

---

## 🚀 Hướng dẫn cho Claude session mới

Khi user mở session mới và nạp file này:

1. **Đọc kỹ mục 4 (Workflow)** để hiểu flow tổng thể
2. **Đọc kỹ mục 5 (Pipeline V7.15)** để hiểu cơ chế detect lên tone
3. **Đọc kỹ mục 13 (Tình trạng)** để biết việc đã làm
4. **Tránh re-implement** những gì đã trong mục 13 ✅
5. **Trước khi tinh chỉnh pipeline**: hỏi user xem họ muốn đi hướng nào (V7.15 đã ổn 3/4 bài, không nên tinh chỉnh tham số nữa trừ khi có data thật mới)
6. **Trước khi sửa UI**: ghi nhớ window FIXED 720×430, không thể giãn nở
7. **Khi user báo bug realtime apply**: check sync indicator 🟢/🔴/⚪ trước, dùng diagnostic CDP

### Quy tắc tương tác

- Trả lời tiếng Việt, súc tích, có bố cục rõ ràng
- Code comments tiếng Việt cho nghiệp vụ, tiếng Anh cho generic
- Khi tinh chỉnh tham số: **giải thích lý do toán học/khoa học**, không đoán mò
- Khi user báo lỗi: **debug từng tầng** (UI → controller → service → external) thay vì đoán
- Luôn `python -m py_compile` sau edit để verify syntax

---

**Hết file. Phiên bản: 2026-05-07. Maintainer: User Truyền + Claude Sonnet.**
