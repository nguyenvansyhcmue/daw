from __future__ import annotations

import subprocess
import sys
import time
from urllib.parse import parse_qs, urlparse

import librosa
import numpy as np
import yt_dlp


SR = 22050
SEGMENT_SECONDS = 24

# Path cookies.txt để bypass YouTube bot detection.
# Set bởi app_controller.startup() từ app_settings.json.
_ytdlp_cookies_file: str = ""


def set_ytdlp_cookies_file(path: str) -> None:
    global _ytdlp_cookies_file
    _ytdlp_cookies_file = str(path or "").strip()

KEYS_MAJOR = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
KEYS_MINOR = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]

MAJOR_PROFILE = np.array([
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
    2.52, 5.19, 2.39, 3.66, 2.29, 2.88
], dtype=np.float32)

MINOR_PROFILE = np.array([
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
    2.54, 4.75, 3.98, 2.69, 3.34, 3.17
], dtype=np.float32)


def _windows_no_console_kwargs() -> dict:
    """
    Ẩn cửa sổ CMD khi app .exe gọi tiến trình ngoài như ffmpeg.
    Đây là phần fix lỗi khi dò tone bản đóng gói bị nháy cửa sổ CMD.
    """
    if not sys.platform.startswith("win"):
        return {}

    startupinfo = subprocess.STARTUPINFO()
    startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW

    return {
        "startupinfo": startupinfo,
        "creationflags": subprocess.CREATE_NO_WINDOW,
    }


def clean_youtube_url(url: str) -> str:
    url = (url or "").strip()
    parsed = urlparse(url)

    if "youtu.be" in parsed.netloc:
        video_id = parsed.path.strip("/")
        if video_id:
            return f"https://www.youtube.com/watch?v={video_id}"

    if "youtube.com" in parsed.netloc or "music.youtube.com" in parsed.netloc:
        qs = parse_qs(parsed.query)
        video_id = qs.get("v", [""])[0]
        if video_id:
            return f"https://www.youtube.com/watch?v={video_id}"

    return url


def _build_ydl_opts(player_clients: list[str]) -> dict:
    """
    YouTube đang siết bot detection. Cách bypass hiệu quả nhất là dùng
    extractor_args player_client với nhiều client backup, kèm User-Agent
    thật. Mỗi client (android, ios, web, tv_embedded) có endpoint khác
    nhau — nếu một client bị block, client khác vẫn hoạt động.
    Nếu _ytdlp_cookies_file được set, yt-dlp sẽ dùng cookies để xác thực
    thay vì anonymous request — bypass hầu hết các dạng bot detection.
    """
    import os
    opts: dict = {
        "format": "bestaudio/best",
        "quiet": True,
        "no_warnings": True,
        "noplaylist": True,
        "extract_flat": False,
        "socket_timeout": 15,
        "retries": 3,
        "extractor_args": {
            "youtube": {
                "player_client": player_clients,
                "skip": ["dash", "hls"] if "android" not in player_clients else [],
            }
        },
        "http_headers": {
            "User-Agent": (
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/120.0.0.0 Safari/537.36"
            ),
            "Accept-Language": "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7",
        },
    }
    if _ytdlp_cookies_file and os.path.isfile(_ytdlp_cookies_file):
        opts["cookiefile"] = _ytdlp_cookies_file
        # Khi có cookies, bỏ extractor_args để yt-dlp tự chọn client và format
        # Không can thiệp vào player_client/skip khi đã có cookies xác thực
        opts.pop("extractor_args", None)
    return opts


def get_stream_url(youtube_url: str):
    youtube_url = clean_youtube_url(youtube_url)

    # Thử nhiều combination player_client để bypass bot detection.
    # Thứ tự: ưu tiên client ít bị check nhất.
    client_chains = [
        ["android", "web"],          # android client thường ít bị bot detection
        ["ios", "web"],              # ios client backup
        ["tv_embedded"],             # tv_embedded thường còn hoạt động khi web bị block
        ["web"],                     # web client cuối cùng
    ]

    last_error: Exception | None = None
    for clients in client_chains:
        ydl_opts = _build_ydl_opts(clients)
        try:
            with yt_dlp.YoutubeDL(ydl_opts) as ydl:
                info = ydl.extract_info(youtube_url, download=False)

                if isinstance(info, dict) and "entries" in info:
                    info = info["entries"][0]

                stream_url = info.get("url", "")
                title = info.get("title", "")
                duration = int(info.get("duration", 0) or 0)

                if not stream_url:
                    raise RuntimeError("Không lấy được stream URL.")

                return stream_url, title, duration
        except Exception as e:
            last_error = e
            err_msg = str(e).lower()
            # Chỉ retry nếu lỗi liên quan bot detection / sign in
            if any(k in err_msg for k in ["sign in", "bot", "confirm", "429", "403"]):
                continue
            # Lỗi khác (network, video private, etc.) → không retry
            raise

    # Tất cả client chains đều fail → raise lỗi cuối với hint cách fix
    raise RuntimeError(
        f"Không lấy được stream URL sau khi thử nhiều client.\n"
        f"YouTube đang chặn bot. Cách fix:\n"
        f"  1. Cập nhật yt-dlp: pip install -U yt-dlp\n"
        f"  2. Hoặc dùng cookies: thêm option cookiefile vào ydl_opts\n"
        f"Lỗi gốc: {last_error}"
    )


def stream_to_numpy(stream_url: str, seconds: int, start_at: int):
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "error",
        "-ss", str(start_at),
        "-i", stream_url,
        "-t", str(seconds),
        "-f", "f32le",
        "-acodec", "pcm_f32le",
        "-ac", "1",
        "-ar", str(SR),
        "-",
    ]

    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        **_windows_no_console_kwargs(),
    )

    raw_audio, err = process.communicate(timeout=60)

    if process.returncode != 0:
        err_text = err.decode("utf-8", errors="ignore")
        raise RuntimeError(f"FFmpeg lỗi:\n{err_text}")

    audio = np.frombuffer(raw_audio, dtype=np.float32)

    if audio.size < SR * 5:
        raise RuntimeError("Audio quá ngắn hoặc stream không đọc được.")

    return audio


def normalize_audio(audio: np.ndarray) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float32)
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak > 0:
        audio = audio / max(peak, 1.0)
    return audio


def normalize_vector(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.maximum(x, 0)
    s = np.linalg.norm(x)
    if s <= 1e-9:
        return x
    return x / s


def estimate_tuning_safe(y, sr):
    try:
        tuning = librosa.estimate_tuning(y=y, sr=sr)
        if not np.isfinite(tuning):
            return 0.0
        return float(tuning)
    except Exception:
        return 0.0


def compute_chroma_features(audio: np.ndarray, sr: int = SR):
    audio = normalize_audio(audio)
    y_harmonic, _ = librosa.effects.hpss(audio)
    tuning = estimate_tuning_safe(y_harmonic, sr)

    chroma_cqt = librosa.feature.chroma_cqt(
        y=y_harmonic,
        sr=sr,
        n_chroma=12,
        bins_per_octave=36,
        tuning=tuning,
    )

    chroma_stft = librosa.feature.chroma_stft(
        y=y_harmonic,
        sr=sr,
        n_chroma=12,
        tuning=tuning,
    )

    cqt_mean = normalize_vector(np.mean(chroma_cqt, axis=1))
    stft_mean = normalize_vector(np.mean(chroma_stft, axis=1))
    chroma_mix = normalize_vector((cqt_mean * 0.72) + (stft_mean * 0.28))

    return chroma_mix


def score_24_keys(chroma_vec: np.ndarray):
    major_profile = normalize_vector(MAJOR_PROFILE)
    minor_profile = normalize_vector(MINOR_PROFILE)

    results = []

    for i in range(12):
        maj_profile = normalize_vector(np.roll(major_profile, i))
        min_profile = normalize_vector(np.roll(minor_profile, i))

        maj_score = float(np.dot(chroma_vec, maj_profile))
        min_score = float(np.dot(chroma_vec, min_profile))

        results.append({
            "key": KEYS_MAJOR[i],
            "key_index": i,
            "scale": "Major",
            "score": maj_score,
            "label": f"{KEYS_MAJOR[i]} Major",
        })

        results.append({
            "key": KEYS_MINOR[i],
            "key_index": i,
            "scale": "Minor",
            "score": min_score,
            "label": f"{KEYS_MINOR[i]} Minor",
        })

    results.sort(key=lambda x: x["score"], reverse=True)
    return results


def choose_segments(duration: int):
    if duration <= 0:
        return [0, 20, 45]

    candidates = []

    if duration > 40:
        candidates.append(12)
    else:
        candidates.append(0)

    if duration > 90:
        candidates.append(int(duration * 0.35))

    if duration > 150:
        candidates.append(int(duration * 0.55))

    if duration > 180:
        candidates.append(int(duration * 0.72))

    cleaned = []
    for s in candidates:
        s = max(0, min(int(s), max(0, duration - SEGMENT_SECONDS - 2)))
        if s not in cleaned:
            cleaned.append(s)

    return cleaned[:4]


def relative_major_of_minor(minor_index: int):
    return (minor_index + 3) % 12


def relative_minor_of_major(major_index: int):
    return (major_index - 3) % 12


def is_relative_pair(a, b):
    if a["scale"] == "Minor" and b["scale"] == "Major":
        return relative_major_of_minor(a["key_index"]) == b["key_index"]

    if a["scale"] == "Major" and b["scale"] == "Minor":
        return relative_minor_of_major(a["key_index"]) == b["key_index"]

    return False


def detect_segment(audio: np.ndarray):
    chroma = compute_chroma_features(audio)
    scores = score_24_keys(chroma)

    top1 = scores[0]
    top2 = scores[1]

    gap = top1["score"] - top2["score"]
    confidence = max(0.0, min(1.0, gap * 8.0))

    return {
        "top1": top1,
        "top2": top2,
        "top8": scores[:8],
        "confidence": confidence,
    }


def weighted_vote(segment_results):
    vote = {}
    details = []

    for idx, result in enumerate(segment_results):
        top1 = result["top1"]
        top2 = result["top2"]
        confidence = result["confidence"]

        weight = 1.0 + confidence
        vote[top1["label"]] = vote.get(top1["label"], 0.0) + top1["score"] * weight

        if is_relative_pair(top1, top2):
            vote[top2["label"]] = vote.get(top2["label"], 0.0) + top2["score"] * 0.45

        details.append({
            "segment": idx + 1,
            "top1": top1,
            "top2": top2,
            "confidence": confidence,
        })

    ranked = sorted(vote.items(), key=lambda x: x[1], reverse=True)
    final_label, final_score = ranked[0]
    second_label = ranked[1][0] if len(ranked) > 1 else ""
    second_score = ranked[1][1] if len(ranked) > 1 else 0.0

    gap = final_score - second_score
    total = max(final_score + second_score, 1e-9)
    final_confidence = max(0.0, min(1.0, gap / total * 2.2))

    return {
        "final_label": final_label,
        "final_score": final_score,
        "second_label": second_label,
        "second_score": second_score,
        "confidence": final_confidence,
        "ranked": ranked[:8],
        "details": details,
    }


def parse_final_label(label: str) -> dict:
    parts = str(label or "").strip().split()
    if len(parts) < 2:
        raise ValueError(f"Không parse được label tone: {label}")

    key = parts[0].strip()
    scale = parts[1].strip().capitalize()

    if scale.lower().startswith("maj"):
        scale = "Major"
        key_index = KEYS_MAJOR.index(key)
    elif scale.lower().startswith("min"):
        scale = "Minor"
        key_index = KEYS_MINOR.index(key)
    else:
        raise ValueError(f"Scale không hợp lệ: {scale}")

    return {
        "key": key,
        "key_index": key_index,
        "scale": scale,
        "label": f"{key} {scale}",
    }


def _ranked_score_map(ranked):
    data = {}
    for item in ranked or []:
        try:
            if isinstance(item, (list, tuple)) and len(item) >= 2:
                data[str(item[0])] = float(item[1])
        except Exception:
            pass
    return data


def _label_from_index(index: int, scale: str) -> str:
    index = int(index) % 12
    scale = str(scale or "Major").strip().capitalize()
    if scale == "Minor":
        return f"{KEYS_MINOR[index]} Minor"
    return f"{KEYS_MAJOR[index]} Major"


def _relative_label(key_index: int, scale: str) -> str:
    scale = str(scale or "Major").strip().capitalize()
    if scale == "Major":
        return _label_from_index(relative_minor_of_major(key_index), "Minor")
    return _label_from_index(relative_major_of_minor(key_index), "Major")


def _scale_intervals(scale: str) -> list[int]:
    scale = str(scale or "Major").strip().capitalize()
    if scale == "Minor":
        # Natural minor. Không dùng harmonic minor cứng để tránh kéo sai bài nhạc Việt có màu modal/borrowed chord.
        return [0, 2, 3, 5, 7, 8, 10]
    return [0, 2, 4, 5, 7, 9, 11]


def _triad_intervals(scale: str) -> list[int]:
    scale = str(scale or "Major").strip().capitalize()
    if scale == "Minor":
        return [0, 3, 7]
    return [0, 4, 7]


def _pc_sum(vec: np.ndarray, root: int, intervals: list[int]) -> float:
    vec = np.asarray(vec, dtype=np.float32)
    if vec.size < 12:
        return 0.0
    return float(sum(float(vec[(root + i) % 12]) for i in intervals))


def _pc_avg(vec: np.ndarray, root: int, intervals: list[int]) -> float:
    if not intervals:
        return 0.0
    return _pc_sum(vec, root, intervals) / max(1, len(intervals))


def _minmax_component(scores: dict[str, float]) -> dict[str, float]:
    if not scores:
        return {}
    vals = [float(v) for v in scores.values() if np.isfinite(float(v))]
    if not vals:
        return {k: 0.0 for k in scores}
    lo = min(vals)
    hi = max(vals)
    if hi - lo <= 1e-9:
        return {k: 0.5 for k in scores}
    return {k: max(0.0, min(1.0, (float(v) - lo) / (hi - lo))) for k, v in scores.items()}


def _candidate_pool_24() -> list[dict]:
    candidates = []
    for i in range(12):
        candidates.append({
            "key": KEYS_MAJOR[i],
            "key_index": i,
            "scale": "Major",
            "label": f"{KEYS_MAJOR[i]} Major",
        })
        candidates.append({
            "key": KEYS_MINOR[i],
            "key_index": i,
            "scale": "Minor",
            "label": f"{KEYS_MINOR[i]} Minor",
        })
    return candidates


def choose_fix_segments(duration: int):
    """
    FIX TONE V2 Balanced:
    - Không bỏ intro, vì nhạc Việt intro rất quan trọng.
    - Nhưng không để intro một mình quyết định toàn bài.
    - Phân tích nhiều vùng để bắt âm chủ, cadence và độ ổn định.
    """
    duration = int(duration or 0)
    if duration <= 0:
        return [
            {"start": 0, "seconds": SEGMENT_SECONDS, "role": "intro", "weight": 0.16},
            {"start": 22, "seconds": SEGMENT_SECONDS, "role": "main", "weight": 0.24},
            {"start": 48, "seconds": SEGMENT_SECONDS, "role": "main", "weight": 0.26},
        ]

    max_start = max(0, duration - SEGMENT_SECONDS - 2)
    raw = []

    # Intro luôn được xét.
    raw.append((0, "intro", 0.14))

    # Cuối intro / vào verse: rất quan trọng với nhạc Việt.
    if duration > 55:
        raw.append((min(18, max_start), "intro_late", 0.12))

    # Các vùng chính.
    if duration > 75:
        raw.append((int(duration * 0.28), "main_a", 0.20))
    if duration > 105:
        raw.append((int(duration * 0.45), "main_b", 0.21))
    if duration > 135:
        raw.append((int(duration * 0.62), "main_c", 0.18))

    # Gần cuối/cadence tổng thể.
    if duration > 95:
        raw.append((int(duration * 0.80), "ending", 0.15))

    cleaned = []
    used = []
    for start, role, weight in raw:
        s = max(0, min(int(start), max_start))
        # Tránh các đoạn quá sát nhau.
        if any(abs(s - u) < 8 for u in used):
            continue
        used.append(s)
        cleaned.append({
            "start": s,
            "seconds": SEGMENT_SECONDS,
            "role": role,
            "weight": float(weight),
        })

    if not cleaned:
        cleaned.append({"start": 0, "seconds": SEGMENT_SECONDS, "role": "intro", "weight": 1.0})

    # Chuẩn hóa weight.
    total = sum(x["weight"] for x in cleaned) or 1.0
    for x in cleaned:
        x["weight"] = float(x["weight"] / total)

    return cleaned[:6]


def compute_bass_chroma_features(audio: np.ndarray, sr: int = SR) -> np.ndarray:
    """
    Bass/root score nhẹ: cố gắng nhìn vùng thấp để phân biệt âm chủ.
    Nếu lỗi thì fallback về chroma harmonic thường.
    """
    audio = normalize_audio(audio)
    try:
        y_harmonic, _ = librosa.effects.hpss(audio)
        tuning = estimate_tuning_safe(y_harmonic, sr)
        bass_chroma = librosa.feature.chroma_cqt(
            y=y_harmonic,
            sr=sr,
            n_chroma=12,
            bins_per_octave=24,
            fmin=librosa.note_to_hz("C1"),
            tuning=tuning,
        )
        bass_mean = normalize_vector(np.mean(bass_chroma, axis=1))
        if np.linalg.norm(bass_mean) > 1e-9:
            return bass_mean
    except Exception:
        pass
    return compute_chroma_features(audio, sr=sr)


def _profile_score(label_info: dict, chroma_vec: np.ndarray) -> float:
    scores = score_24_keys(chroma_vec)
    target = label_info["label"]
    for item in scores:
        if item["label"] == target:
            return float(item["score"])
    return 0.0


def _tonal_center_raw(label_info: dict, chroma_vec: np.ndarray) -> float:
    root = int(label_info["key_index"]) % 12
    scale = label_info["scale"]
    triad = _triad_intervals(scale)
    scale_intervals = _scale_intervals(scale)

    root_energy = float(chroma_vec[root])
    triad_energy = _pc_avg(chroma_vec, root, triad)
    scale_energy = _pc_avg(chroma_vec, root, scale_intervals)

    # Tonic/root phải có điểm riêng, không chỉ bộ nốt chung chung.
    return (0.48 * root_energy) + (0.34 * triad_energy) + (0.18 * scale_energy)


def _cadence_raw(label_info: dict, cadence_chroma: np.ndarray) -> float:
    root = int(label_info["key_index"]) % 12
    triad = _triad_intervals(label_info["scale"])
    return (0.58 * float(cadence_chroma[root])) + (0.42 * _pc_avg(cadence_chroma, root, triad))


def _bass_root_raw(label_info: dict, bass_chroma: np.ndarray) -> float:
    root = int(label_info["key_index"]) % 12
    fifth = (root + 7) % 12
    third = (root + (3 if label_info["scale"] == "Minor" else 4)) % 12
    return (0.66 * float(bass_chroma[root])) + (0.22 * float(bass_chroma[fifth])) + (0.12 * float(bass_chroma[third]))


def _degree_function_raw(label_info: dict, chroma_vec: np.ndarray) -> float:
    """
    Chấm nhẹ theo bậc/hệ chức năng.
    Mục tiêu không phải nhận hợp âm 100%, mà để giảm lỗi nhầm tonic với bậc iv/v/vi.
    """
    root = int(label_info["key_index"]) % 12
    scale = label_info["scale"]
    scale_intervals = _scale_intervals(scale)
    triad = _triad_intervals(scale)

    scale_energy = _pc_sum(chroma_vec, root, scale_intervals)
    outside = max(0.0, float(np.sum(chroma_vec)) - scale_energy)
    tonic_triad = _pc_avg(chroma_vec, root, triad)

    if scale == "Minor":
        # i, iv, v/V, VI, VII, III là các trục hay gặp trong nhạc Việt.
        degree_roots = [0, 5, 7, 8, 10, 3]
        # Dấu hiệu phân biệt minor gần nhau: bậc 2 và bậc 6.
        characteristic = 0.5 * float(chroma_vec[(root + 2) % 12]) + 0.5 * float(chroma_vec[(root + 8) % 12])
    else:
        degree_roots = [0, 5, 7, 9, 4, 2]
        characteristic = 0.5 * float(chroma_vec[(root + 4) % 12]) + 0.5 * float(chroma_vec[(root + 11) % 12])

    degree_root_energy = sum(float(chroma_vec[(root + d) % 12]) for d in degree_roots) / len(degree_roots)

    return (
        0.34 * scale_energy
        # Bớt phạt note ngoài scale (-0.18 → -0.12) để bớt sai với chord sus2/sus4/add9
        # phổ biến trong nhạc trẻ Việt (Min, Đen Vâu, Vũ.).
        - 0.12 * outside
        + 0.26 * tonic_triad
        + 0.14 * degree_root_energy
        + 0.08 * characteristic
    )


def _leading_tone_raw(label_info: dict, chroma_vec: np.ndarray) -> float:
    """
    [DEPRECATED — không dùng trong vote nữa]

    Ý tưởng ban đầu: dùng bậc 7 nâng (harmonic minor) làm dấu hiệu minor "thật".
    Nhưng kiểm thử trên nhạc Việt (ballad/bolero) cho thấy đa số bài dùng natural minor
    không có bậc 7 nâng → component này score thấp cho minor và score cao cho relative major
    (vì relative major có bậc 7 thuộc key signature) → phản tác dụng, đẩy nhầm sang major.

    Giữ hàm này để tham khảo / phục hồi sau này nếu cần. Không gọi trong deep_balanced_vote_fix.
    """
    chroma_vec = np.asarray(chroma_vec, dtype=np.float32)
    if chroma_vec.size < 12:
        return 0.0

    root = int(label_info["key_index"]) % 12
    scale = label_info["scale"]

    if scale == "Minor":
        leading_sharp = float(chroma_vec[(root + 11) % 12])
        natural7 = float(chroma_vec[(root + 10) % 12])
        return max(0.0, leading_sharp * 1.5 - natural7 * 0.3)

    return float(chroma_vec[(root + 11) % 12])


def _harmonic_energy_ratio(audio: np.ndarray) -> float:
    """
    Tỷ lệ năng lượng harmonic / tổng. Dùng để phát hiện intro vocal-only / ad-lib / spoken word
    (chỉ có vocal/percussion, gần như không có nhạc cụ harmonic) — các đoạn này chroma rác.
    Ratio < ~0.30 nghĩa là segment gần như không có hoà âm rõ → nên giảm trọng số.
    """
    audio = np.asarray(audio, dtype=np.float32)
    if audio.size == 0:
        return 0.0
    try:
        y_harmonic, _ = librosa.effects.hpss(audio)
        h = float(np.sum(y_harmonic.astype(np.float64) ** 2))
        total = float(np.sum(audio.astype(np.float64) ** 2)) + 1e-9
        return max(0.0, min(1.0, h / total))
    except Exception:
        return 1.0


def _load_final_outro_chroma(stream_url: str, duration: int, seconds: int = 8) -> np.ndarray | None:
    """
    Tải 8s cuối thật của bài và tính chroma — chỉ báo TONIC mạnh nhất.
    Bài Vpop/karaoke phần lớn kết thúc ở I chord (tonic), giúp phân biệt:
        - V → I confusion (Ab Major bị nhầm là tonic của bài Db Major thật)
        - relative major confusion (G Major bị nhầm là tonic của bài E Minor thật)
    Trả None nếu:
        - duration quá ngắn
        - đoạn cuối quá im (fade-out)
        - chroma cuối "flat" (không có tonic dominant rõ — bài kết deceptive cadence
          hoặc fade-out trên relative major; component này không đáng tin)
    Khi None: vote bỏ qua component an toàn, không kéo lệch.
    """
    if duration <= 0 or duration < seconds + 4:
        return None

    # Tránh đúng frame cuối (đôi khi có nhiễu/silence buffer); lùi 1s.
    start_at = max(0, duration - seconds - 1)
    try:
        audio = stream_to_numpy(stream_url, seconds=seconds, start_at=start_at)
    except Exception:
        return None

    if audio is None or audio.size == 0:
        return None

    # Check năng lượng RMS — nếu fade-out gần silence, chroma sẽ rác → bỏ qua.
    try:
        rms = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))
        if rms < 0.005:
            return None
    except Exception:
        return None

    try:
        chroma = compute_chroma_features(audio)
    except Exception:
        return None

    # Clarity check: tonic phải đậm hơn trung bình rõ ràng.
    # Bài kết I chord rõ ràng → max/mean ~2.0-3.0; bài fade-out / deceptive ending → ~1.2-1.5.
    # Threshold 1.7 lọc được phần lớn case "không đáng tin".
    try:
        max_v = float(np.max(chroma))
        mean_v = float(np.mean(chroma)) + 1e-9
        clarity = max_v / mean_v
        if clarity < 1.7:
            return None
    except Exception:
        return None

    return chroma


def _analyze_fix_audio_segment(audio: np.ndarray, role: str, weight: float) -> dict:
    chroma = compute_chroma_features(audio, sr=SR)
    bass_chroma = compute_bass_chroma_features(audio, sr=SR)

    # Cadence: đoạn cuối segment. Không bỏ intro; cadence của intro_late/main/ending vẫn có ích.
    cadence_len = min(len(audio), int(8 * SR))
    if cadence_len > int(3 * SR):
        cadence_audio = audio[-cadence_len:]
        cadence_chroma = compute_chroma_features(cadence_audio, sr=SR)
    else:
        cadence_chroma = chroma

    # Tỷ lệ năng lượng harmonic — dùng để giảm trọng số intro vocal-only / ad-lib / spoken word.
    # Chỉ tính cho segment intro để khỏi tốn HPSS lần nữa với main segments.
    is_intro = str(role).startswith("intro")
    harmonic_ratio = _harmonic_energy_ratio(audio) if is_intro else 1.0

    # Trọng số hiệu dụng: nếu intro gần như không có nhạc cụ harmonic (ratio < 0.30),
    # giảm weight về ~5% để tránh chroma rác kéo lệch vote.
    effective_weight = float(weight)
    if is_intro and harmonic_ratio < 0.30:
        effective_weight = max(0.05 * float(weight), 0.01)
    elif is_intro and harmonic_ratio < 0.45:
        # Vùng "mỏng" — vẫn dùng nhưng giảm 50%.
        effective_weight = 0.50 * float(weight)

    return {
        "role": role,
        "weight": float(weight),
        "effective_weight": effective_weight,
        "harmonic_ratio": float(harmonic_ratio),
        "chroma": chroma,
        "bass_chroma": bass_chroma,
        "cadence_chroma": cadence_chroma,
        "top8": [(x["label"], float(x["score"])) for x in score_24_keys(chroma)[:8]],
    }


def _history_bonus_for_label(label: str, current_label: str, correction_hint: dict | None) -> float:
    correction_hint = correction_hint or {}
    hint_label = str(correction_hint.get("corrected_label", "") or "").strip()
    detected_label = str(correction_hint.get("detected_label", "") or correction_hint.get("from_label", "") or "").strip()

    if not hint_label:
        return 0.0

    # Chỉ cộng điểm nếu hint đúng với label hiện tại đang cần sửa.
    if current_label and detected_label and detected_label != current_label:
        return 0.0

    if label == hint_label:
        try:
            count = int(correction_hint.get("count", 1) or 1)
        except Exception:
            count = 1
        return min(1.0, 0.55 + 0.15 * max(0, count - 1))

    return 0.0


def deep_balanced_vote_fix(segment_infos: list[dict], current_result: dict | None = None, correction_hint: dict | None = None, final_chroma: np.ndarray | None = None) -> dict:
    """
    FIX TONE V2 Balanced / Deep Musician Mode.
    Không đưa nhiều lựa chọn cho khách. Bên trong chấm 24 key/scale bằng nhiều bằng chứng:
    intro, main segments, tonal center, cadence, bass/root, bậc/chức năng, relative và lịch sử sửa sai nhẹ.
    """
    current_result = current_result or {}
    correction_hint = correction_hint or {}
    current_label = str(current_result.get("label", "") or "").strip()

    candidates = _candidate_pool_24()
    labels = [c["label"] for c in candidates]

    raw_components = {
        "global_chroma": {label: 0.0 for label in labels},
        "intro": {label: 0.0 for label in labels},
        "main_segments": {label: 0.0 for label in labels},
        "tonal_center": {label: 0.0 for label in labels},
        "cadence": {label: 0.0 for label in labels},
        "bass_root": {label: 0.0 for label in labels},
        "degree_function": {label: 0.0 for label in labels},
        # final_outro: chroma 8s cuoi BAI THAT — chong V↔I confusion (Vpop ket I chord).
        "final_outro": {label: 0.0 for label in labels},
        "relative": {label: 0.0 for label in labels},
        "history": {label: 0.0 for label in labels},
    }

    intro_weight_total = 0.0
    main_weight_total = 0.0

    for seg in segment_infos:
        role = str(seg.get("role", "main"))
        # Ưu tiên effective_weight (đã hạ trọng số intro vocal-only); fallback weight gốc cho tương thích.
        weight = float(seg.get("effective_weight", seg.get("weight", 1.0)) or 1.0)
        chroma = seg["chroma"]
        cadence_chroma = seg["cadence_chroma"]
        bass_chroma = seg["bass_chroma"]

        is_intro = role.startswith("intro")
        if is_intro:
            intro_weight_total += weight
        else:
            main_weight_total += weight

        for cand in candidates:
            label = cand["label"]
            profile = _profile_score(cand, chroma)
            raw_components["global_chroma"][label] += profile * weight

            if is_intro:
                raw_components["intro"][label] += profile * weight
            else:
                raw_components["main_segments"][label] += profile * weight

            raw_components["tonal_center"][label] += _tonal_center_raw(cand, chroma) * weight
            raw_components["cadence"][label] += _cadence_raw(cand, cadence_chroma) * weight
            raw_components["bass_root"][label] += _bass_root_raw(cand, bass_chroma) * weight
            raw_components["degree_function"][label] += _degree_function_raw(cand, chroma) * weight

    # Không để intro/main bị phạt chỉ vì tổng weight nhỏ hơn.
    if intro_weight_total > 1e-9:
        for label in labels:
            raw_components["intro"][label] /= intro_weight_total
    if main_weight_total > 1e-9:
        for label in labels:
            raw_components["main_segments"][label] /= main_weight_total
    else:
        raw_components["main_segments"] = dict(raw_components["global_chroma"])

    # Relative không phải ép đổi, chỉ là điểm cân bằng riêng.
    if current_label:
        try:
            cur = parse_final_label(current_label)
            rel_label = _relative_label(cur["key_index"], cur["scale"])
            if rel_label in raw_components["relative"]:
                raw_components["relative"][rel_label] = 1.0
            # Cũng giữ điểm cho current để tránh relative lấn át khi current thật sự đúng.
            if current_label in raw_components["relative"]:
                raw_components["relative"][current_label] = 0.55
        except Exception:
            pass

    for label in labels:
        raw_components["history"][label] = _history_bonus_for_label(label, current_label, correction_hint)

    # Tinh final_outro raw — chroma 8s cuoi bai THAT cho moi candidate.
    # Chi co data khi caller (detect_from_youtube_fix_mode) truyen final_chroma vao;
    # neu None (bai qua ngan / fade-out) thi raw all 0 → minmax all 0.5 → khong keo lech ai.
    if final_chroma is not None:
        for cand in candidates:
            label = cand["label"]
            raw_components["final_outro"][label] = _cadence_raw(cand, final_chroma)

    norm_components = {name: _minmax_component(values) for name, values in raw_components.items()}

    # Cân chỉnh trọng số V2 deep cho nhạc Việt — phiên bản 4 (sau khi test 7 bài V→I):
    # - bass_root 0.22 (tăng 0.18 → 0.22): chỉ báo TONIC chính xác nhất.
    # - tonal_center 0.20: root + triad + scale energy.
    # - main_segments 0.14: chroma profile các đoạn chính.
    # - cadence 0.10: cadence cuối segment.
    # - global_chroma 0.08: profile thuần KHÔNG phân biệt được tonic.
    # - degree_function 0.08: scale match + outside penalty.
    # - final_outro 0.06 (giảm 0.10 → 0.06): có bài deceptive cadence
    #   (vd G Minor kết Eb chord) → giảm weight để không phá vote khi outro misleading,
    #   vẫn giữ vì giúp fix V→I confusion.
    # - intro 0.06.
    # - relative 0.04, history 0.02.
    # Tổng = 1.00.
    weights = {
        "global_chroma": 0.08,
        "intro": 0.06,
        "main_segments": 0.14,
        "tonal_center": 0.20,
        "cadence": 0.10,
        "bass_root": 0.22,
        "degree_function": 0.08,
        "final_outro": 0.06,
        "relative": 0.04,
        "history": 0.02,
    }

    final_scores = {}
    breakdown = {}
    for cand in candidates:
        label = cand["label"]
        total = 0.0
        bd = {}
        for name, w in weights.items():
            val = float(norm_components.get(name, {}).get(label, 0.0))
            bd[name] = round(val, 4)
            total += w * val
        final_scores[label] = float(total)
        breakdown[label] = bd

    ranked = sorted(final_scores.items(), key=lambda x: x[1], reverse=True)
    best_label, best_score = ranked[0]
    second_label, second_score = ranked[1] if len(ranked) > 1 else ("", 0.0)

    current_score = float(final_scores.get(current_label, 0.0)) if current_label else 0.0
    margin_vs_second = float(best_score - second_score)
    margin_vs_current = float(best_score - current_score) if current_label else margin_vs_second

    # Chốt an toàn: không đổi nếu không đủ bằng chứng tốt hơn current.
    selected_label = best_label
    fix_method = "deep_balanced"

    if current_label and best_label != current_label:
        # FIX TONE cần mạnh hơn fast detect, nhưng vẫn tránh đổi bừa.
        if margin_vs_current < 0.018 and margin_vs_second < 0.012:
            selected_label = current_label
            fix_method = "no_better_candidate"
        else:
            fix_method = "deep_balanced_changed"
    elif current_label and best_label == current_label:
        fix_method = "deep_balanced_keep_current"

    parsed = parse_final_label(selected_label)

    # Confidence riêng cho FIX: vừa dựa vào margin, vừa dựa vào score tổng.
    selected_score = float(final_scores.get(selected_label, best_score))
    confidence = max(0.0, min(1.0, (selected_score * 0.70) + (max(margin_vs_second, 0.0) * 2.2)))

    return {
        "key": parsed["key"],
        "key_index": parsed["key_index"],
        "scale": parsed["scale"],
        "label": parsed["label"],
        "confidence": round(float(confidence), 3),
        "ranked": [(label, round(float(score), 5)) for label, score in ranked[:10]],
        "fix_method": fix_method,
        "previous_label": current_label,
        "deep_best_label": best_label,
        "deep_best_score": round(float(best_score), 5),
        "current_score": round(float(current_score), 5),
        "margin_vs_current": round(float(margin_vs_current), 5),
        "margin_vs_second": round(float(margin_vs_second), 5),
        "breakdown": breakdown.get(selected_label, {}),
        "segment_roles": [
            {"role": str(s.get("role", "")), "weight": round(float(s.get("weight", 0.0)), 3), "top8": s.get("top8", [])[:3]}
            for s in segment_infos
        ],
        "source": "youtube_fix_v2_balanced",
    }


def resolve_fix_tone_candidate(fresh_result: dict, current_result: dict | None = None, correction_hint: dict | None = None) -> dict:
    """
    Giữ hàm này để tương thích ngược.
    FIX TONE V2 không còn dùng logic relative/top2 đơn giản nữa khi có thể phân tích sâu.
    Nếu chỉ có fresh_result, trả về fresh_result an toàn.
    """
    fixed = dict(fresh_result or {})
    fixed.setdefault("source", "youtube_fix")
    fixed.setdefault("fix_method", "fresh_detect_fallback")
    fixed.setdefault("previous_label", str((current_result or {}).get("label", "") or ""))
    return fixed


def detect_from_youtube_fix_mode(youtube_url: str, current_result: dict | None = None, correction_hint: dict | None = None, verbose: bool = False):
    """
    FIX TONE V2 Balanced / Deep Musician Mode.
    - Phân tích sâu hơn auto detect thường.
    - Không bỏ intro, nhưng cân bằng intro với main/cadence.
    - Xét relative, tonal center, bass/root, cadence, bậc/hệ chức năng.
    - Correction history chỉ cộng điểm nhẹ và chỉ dùng trong FIX TONE.
    - Trả về một kết quả duy nhất để apply thử, KHÔNG tự lưu cache.
    """
    t0 = time.time()
    clean_url = clean_youtube_url(youtube_url)

    if verbose:
        print("[FIX V2] URL sạch:", clean_url)
        print("[FIX V2] Đang lấy stream URL...")

    stream_url, title, duration = get_stream_url(clean_url)
    segments = choose_fix_segments(duration)

    if verbose:
        print("[FIX V2] Tiêu đề:", title)
        print("[FIX V2] Duration:", duration)
        print("[FIX V2] Segments:", segments)

    segment_infos = []
    for seg in segments:
        audio = stream_to_numpy(
            stream_url=stream_url,
            seconds=int(seg.get("seconds", SEGMENT_SECONDS)),
            start_at=int(seg.get("start", 0)),
        )
        info = _analyze_fix_audio_segment(audio, role=seg.get("role", "main"), weight=float(seg.get("weight", 1.0)))
        segment_infos.append(info)

        if verbose:
            print("[FIX V2]", seg.get("role"), "start=", seg.get("start"), "top=", info.get("top8", [])[:3])

    # Tải thêm 8s cuối thật của bài → chống V↔I confusion (final cadence ở I chord).
    final_chroma = _load_final_outro_chroma(stream_url, duration, seconds=8)
    if verbose:
        print(f"[FIX V2] Final outro chroma: {'loaded' if final_chroma is not None else 'None (skipped)'}")

    result = deep_balanced_vote_fix(segment_infos, current_result=current_result, correction_hint=correction_hint, final_chroma=final_chroma)
    result["title"] = title
    result["duration"] = duration
    result["elapsed"] = round(time.time() - t0, 2)
    result["correction_hint"] = correction_hint or {}
    return result



def detect_segment_enhanced(audio: np.ndarray) -> dict:
    """
    V1+ enhanced: tính thêm bass_chroma + cadence_chroma cho mỗi segment.
    Giữ logic top1/top2/top8 từ score_24_keys (như V1) để KPI không đổi.
    Chậm hơn detect_segment cũ ~30-40% / segment vì có thêm 2 chroma extraction,
    nhưng giúp weighted_vote_enhanced phân biệt minor ↔ relative major chuẩn hơn.
    """
    chroma = compute_chroma_features(audio)
    bass_chroma = compute_bass_chroma_features(audio)

    # Cadence segment (8s cuối) — V → I cadence là chỉ báo tonic mạnh.
    cadence_len = min(len(audio), int(8 * SR))
    if cadence_len > int(3 * SR):
        cadence_audio = audio[-cadence_len:]
        cadence_chroma = compute_chroma_features(cadence_audio)
    else:
        cadence_chroma = chroma

    scores = score_24_keys(chroma)
    top1 = scores[0]
    top2 = scores[1]
    gap = top1["score"] - top2["score"]
    confidence = max(0.0, min(1.0, gap * 8.0))

    return {
        "top1": top1,
        "top2": top2,
        "top8": scores[:8],
        "all_scores": scores,
        "confidence": confidence,
        "chroma": chroma,
        "bass_chroma": bass_chroma,
        "cadence_chroma": cadence_chroma,
    }


def weighted_vote_enhanced(segment_results: list[dict], final_chroma: np.ndarray | None = None) -> dict:
    """
    V1+ enhanced vote: kết hợp 5 components có min-max normalize:
        - chroma_profile (18%): score từ Krumhansl-Kessler 24 keys.
        - bass_root      (30%): chroma vùng bass khớp tonic — chống nhầm minor↔relative major.
        - tonal_center   (27%): root + triad + scale energy.
        - cadence        (12%): chroma 8s cuối CỦA SEGMENT khớp tonic.
        - final_outro    (13%): chroma 8s cuối CỦA BÀI THẬT — chống nhầm V↔I (DOMINANT-TONIC).
    `final_chroma` có thể None khi bài quá ngắn / fade-out — component sẽ bị bỏ qua an toàn.
    Chỉ tính cho candidate set gồm top8 hợp nhất các segment (đa số ~10-14 candidate)
    để giữ tốc độ; tonic thật luôn xuất hiện trong top8 ít nhất 1 segment.
    """
    # Tap hop candidate tu top8 cua tat ca segments.
    candidate_set = set()
    candidates_info: dict[str, dict] = {}
    for sr in segment_results:
        for item in sr.get("top8", []):
            label = item["label"]
            candidate_set.add(label)
            candidates_info.setdefault(label, {
                "key": item["key"],
                "key_index": item["key_index"],
                "scale": item["scale"],
                "label": label,
            })

    if not candidate_set:
        return {
            "final_label": "",
            "final_score": 0.0,
            "second_label": "",
            "second_score": 0.0,
            "confidence": 0.0,
            "ranked": [],
            "details": [],
        }

    raw = {
        "chroma_profile": {l: 0.0 for l in candidate_set},
        "bass_root": {l: 0.0 for l in candidate_set},
        "tonal_center": {l: 0.0 for l in candidate_set},
        "cadence": {l: 0.0 for l in candidate_set},
        "final_outro": {l: 0.0 for l in candidate_set},
    }

    details = []
    for idx, sr in enumerate(segment_results):
        chroma = sr.get("chroma")
        bass_chroma = sr.get("bass_chroma")
        cadence_chroma = sr.get("cadence_chroma")
        confidence = float(sr.get("confidence", 0.0) or 0.0)
        seg_weight = 1.0 + confidence

        score_map = {item["label"]: float(item["score"]) for item in sr.get("all_scores", sr.get("top8", []))}

        for label in candidate_set:
            cand = candidates_info[label]
            raw["chroma_profile"][label] += score_map.get(label, 0.0) * seg_weight
            if bass_chroma is not None:
                raw["bass_root"][label] += _bass_root_raw(cand, bass_chroma) * seg_weight
            if chroma is not None:
                raw["tonal_center"][label] += _tonal_center_raw(cand, chroma) * seg_weight
            if cadence_chroma is not None:
                raw["cadence"][label] += _cadence_raw(cand, cadence_chroma) * seg_weight

        details.append({
            "segment": idx + 1,
            "top1": sr.get("top1"),
            "top2": sr.get("top2"),
            "confidence": confidence,
        })

    # Tinh final_outro raw — score cadence cua 8s cuoi bai THAT cho moi candidate.
    # Day la chi bao TONIC manh nhat (bai gan nhu luon ket o I chord) → chong V↔I confusion.
    if final_chroma is not None:
        for label in candidate_set:
            cand = candidates_info[label]
            raw["final_outro"][label] = _cadence_raw(cand, final_chroma)

    norm = {k: _minmax_component(v) for k, v in raw.items()}

    # Trong so V1+ enhanced (5 components) — phien ban final (đã verify trên 7 bài):
    # - bass_root 0.30: chỉ báo TONIC chính xác nhất.
    # - tonal_center 0.27: root + triad + scale energy.
    # - chroma_profile 0.18: profile thuần (Krumhansl-Kessler).
    # - final_outro 0.13: cadence cuối bài THẬT — chống V↔I confusion.
    # - cadence 0.12: cadence cuối segment.
    # KẾT QUẢ TEST: 5/7 PASS:
    #   ✅ E Min, D Min (regression vẫn pass)
    #   ✅ F# Min, C# Min, Eb Min (3/5 V→I + relative fix tự động)
    #   ❌ Db Major (outro silent fade-out, không phải V→I thuần)
    #   ❌ G Minor (outro deceptive cadence kết Eb chord)
    # 2 bài còn lại là case nội tại khó — user dùng FIX TONE → SỬA TONE thủ công.
    weights = {
        "chroma_profile": 0.18,
        "bass_root": 0.30,
        "tonal_center": 0.27,
        "cadence": 0.12,
        "final_outro": 0.13,
    }

    final_scores = {}
    for label in candidate_set:
        final_scores[label] = sum(weights[k] * norm[k].get(label, 0.0) for k in weights)

    ranked = sorted(final_scores.items(), key=lambda x: x[1], reverse=True)
    final_label, final_score = ranked[0]
    second_label, second_score = ranked[1] if len(ranked) > 1 else ("", 0.0)
    gap = float(final_score - second_score)
    total = max(final_score + second_score, 1e-9)
    final_confidence = max(0.0, min(1.0, gap / total * 2.2))

    return {
        "final_label": final_label,
        "final_score": float(final_score),
        "second_label": second_label,
        "second_score": float(second_score),
        "confidence": final_confidence,
        "ranked": [(label, float(score)) for label, score in ranked[:8]],
        "details": details,
    }


def detect_from_youtube(youtube_url: str, verbose: bool = False):
    """
    V1+ enhanced: dò nhanh có hỗ trợ bass_root + tonal_center + cadence
    để chống nhầm minor ↔ relative major (loại lỗi phổ biến với nhạc Việt).
    Vẫn nhanh hơn detect_from_youtube_fix_mode (V2 deep) ~2-2.5x.
    """
    t0 = time.time()
    clean_url = clean_youtube_url(youtube_url)

    if verbose:
        print("URL sạch:", clean_url)
        print("Đang lấy stream URL...")

    stream_url, title, duration = get_stream_url(clean_url)

    if verbose:
        print("Tiêu đề:", title)
        print("Duration:", duration, "giây")

    # V1+ enhanced: dùng 6 segments của choose_fix_segments thay vì 4 của choose_segments.
    # Quan trọng: thêm ending segment (~80% duration) — đoạn này cadence (V→I) rõ nhất,
    # giúp xác định tonic chống nhầm minor ↔ relative major.
    fix_segments = choose_fix_segments(duration)
    segments = [int(s["start"]) for s in fix_segments]

    if verbose:
        print("Các đoạn sẽ phân tích:", segments)

    segment_results = []

    for i, start_at in enumerate(segments, start=1):
        if verbose:
            print(f"\nĐoạn {i}: start={start_at}s, length={SEGMENT_SECONDS}s")

        audio = stream_to_numpy(
            stream_url=stream_url,
            seconds=SEGMENT_SECONDS,
            start_at=start_at,
        )

        # V1+ enhanced segment: thêm bass_chroma + cadence_chroma cho vote chính xác hơn.
        result = detect_segment_enhanced(audio)
        segment_results.append(result)

        if verbose:
            top1 = result["top1"]
            top2 = result["top2"]
            print(f"Top 1: {top1['label']} | score={top1['score']:.4f}")
            print(f"Top 2: {top2['label']} | score={top2['score']:.4f}")
            print(f"Confidence đoạn: {result['confidence']:.2f}")

    # Tải thêm 8s cuối thật của bài → chống V↔I confusion (final cadence ở I chord).
    final_chroma = _load_final_outro_chroma(stream_url, duration, seconds=8)
    if verbose:
        print(f"\nFinal outro chroma: {'loaded' if final_chroma is not None else 'None (skipped)'}")

    # V1+ enhanced vote: 5 components (chroma_profile/bass_root/tonal_center/cadence/final_outro).
    final = weighted_vote_enhanced(segment_results, final_chroma=final_chroma)
    parsed = parse_final_label(final["final_label"])

    elapsed = time.time() - t0

    return {
        "key": parsed["key"],
        "key_index": parsed["key_index"],
        "scale": parsed["scale"],
        "label": parsed["label"],
        "confidence": round(float(final["confidence"]), 3),
        "title": title,
        "duration": duration,
        "elapsed": round(elapsed, 2),
        "ranked": final.get("ranked", []),
        "details": final.get("details", []),
        "source": "youtube",
    }


class ToneService:
    def __init__(self):
        self.major_profile = MAJOR_PROFILE
        self.minor_profile = MINOR_PROFILE
        self.note_names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

    def _normalize(self, v: np.ndarray) -> np.ndarray:
        s = np.sum(v)
        if s <= 0:
            return v
        return v / s

    def _score_profile(self, chroma_avg: np.ndarray, profile: np.ndarray) -> list:
        chroma_avg = self._normalize(chroma_avg)
        scores = []

        for i in range(12):
            rotated = np.roll(profile, i)
            rotated = self._normalize(rotated)

            if np.std(chroma_avg) == 0 or np.std(rotated) == 0:
                corr = -1.0
            else:
                corr = np.corrcoef(chroma_avg, rotated)[0, 1]

            scores.append(corr)

        return scores

    def detect_tone_from_file(self, file_path: str) -> dict:
        y, sr = librosa.load(file_path, sr=SR, mono=True)

        if len(y) < sr * 3:
            raise ValueError("File quá ngắn để dò tone ổn định.")

        chroma = compute_chroma_features(y, sr=sr)
        scores = score_24_keys(chroma)
        best = scores[0]
        second = scores[1] if len(scores) > 1 else best
        gap = best["score"] - second["score"]
        confidence = max(0.0, min(1.0, gap * 8.0))

        return {
            "key": best["key"],
            "key_index": int(best["key_index"]),
            "scale": best["scale"],
            "label": best["label"],
            "confidence": round(float(confidence), 3),
            "ranked": [(item["label"], float(item["score"])) for item in scores[:8]],
        }

    def detect_tone_from_youtube_url(self, youtube_url: str, verbose: bool = False) -> dict:
        return detect_from_youtube(youtube_url, verbose=verbose)

    def detect_tone_fix_mode(self, youtube_url: str, current_result: dict | None = None, correction_hint: dict | None = None, verbose: bool = False) -> dict:
        return detect_from_youtube_fix_mode(youtube_url, current_result=current_result, correction_hint=correction_hint, verbose=verbose)
