from __future__ import annotations

"""
Service phát hiện đoạn cuối bài karaoke có lên tone +1 / +2 / +3 semitone.

Pipeline V7 — Khoa học MIR (giai đoạn G1: vẫn giữ popup để test):

    [A] Tải FULL audio qua ffmpeg (giữ độ chính xác tối đa).
    [B] HPSS — tách harmonic/percussive để loại nhiễu trống.
    [C] Chroma CENS trên harmonic — feature ổn định nhất cho key detection.
    [D] Sliding key likelihood (window 4s, hop 0.5s) bằng Krumhansl-Schmuckler
        correlation với 24 key profile → matrix [T × 24].
    [E] Self-Similarity Matrix + novelty curve (Foote 2000) — phát hiện ranh
        giới structural của bản nhạc.
    [F] Modulation candidate extraction:
        - Với mỗi novelty peak p, so sánh key dominant trước/sau p.
        - Cùng scale + delta ∈ {1, 2, 3} → ứng viên.
    [G] Beat-aware snap dùng beat tracking trên y_percussive (nhánh trống đã
        được tách → beat chính xác). Có thể fallback từ madmom → librosa.
    [H] Multi-evidence confidence:
        confidence = 0.40 * novelty_peak_normalized
                   + 0.35 * key_likelihood_gap
                   + 0.15 * beat_snap_score
                   + 0.10 * bass_change_score
        confidence ≥ 0.80  → có thể auto apply (giai đoạn G3, hiện vẫn popup)
        0.50 ≤ ... < 0.80  → popup confirm (giai đoạn G1 hiện tại)
        < 0.50            → silent
"""

from dataclasses import dataclass, field
from typing import Callable, Optional
from urllib.parse import parse_qs, urlparse

import numpy as np

from core.services import tone_service as ts


# =============================================================================
# CONSTANTS
# =============================================================================

SR = ts.SR  # 22050

# Tail length: phần cuối bài cần phân tích (giây). Chỉ HPSS + chroma trên đoạn
# này để tiết kiệm thời gian; vẫn đủ để bắt modulation và 4–8 nhịp trước nó.
# V7.1: tăng từ 90 → 150s vì test thực tế có bài lên tone từ 03:14 trên bài 5
# phút (~65%), nằm ngoài tail 90s cũ. 150s đảm bảo bao trùm pattern Vpop.
TAIL_SECONDS_DEFAULT = 150

# Sliding key likelihood
# V7.7: tăng window 2 → 5s, hop 0.25 → 0.5s.
# Lý do: log V7.6 từ user thực tế cho thấy chroma_cqt + median 2.5s vẫn không
# đủ ổn định argmax trong audio Vpop nhiều vocal/harmonic — stability_before
# chỉ 0.15-0.27 (ngưỡng 0.35) → mọi transition bị reject.
# Window 5s smooth mạnh hơn nhiều, đổi lấy bias thời gian ~2-3s (vẫn trong
# yêu cầu sai số 1-2s của user, vì nhịp downbeat snap sẽ kéo về beat thật).
KEY_WINDOW_SEC = 5.0
KEY_HOP_SEC = 0.5
CHROMA_HOP_LENGTH = 1024  # ~46ms/frame @ 22050Hz

# Novelty (Foote checkerboard kernel)
NOVELTY_KERNEL_SEC = 8.0  # phải rộng hơn KEY_WINDOW_SEC để bắt structural change

# Modulation
SEMITONE_TARGETS = (1, 2, 3)
# V7.1: tăng lookback/lookahead 6 → 8s để có thêm context khi bài có lead-in dài
# trước modulation (drum fill 1–2 nhịp đệm trước khi key đổi).
KEY_BEFORE_LOOKBACK_SEC = 8.0
KEY_AFTER_LOOKAHEAD_SEC = 8.0
MIN_GAP_FROM_KEY_BEFORE = 1.0  # tránh transient ngay tại boundary

# V7.1: persistence check — sau peak modulation, key_after phải duy trì >= ngưỡng
# này đến cuối bài. Lọc peak giả ở outro/drum fill cuối cùng (case bài 2 sai 61s).
# V7.4: nới 0.50 → 0.35. Audio thật có outro instrumental/fade làm key estimate
# drift; persistence 50% quá strict cho real-world data.
MIN_PERSISTENCE_AFTER_PEAK = 0.15  # V7.8: nới 0.35→0.15 (audio thật chỉ 0.07-0.12 ở key_after raw, do flicker giữa relative pairs)

# V7.1: smooth likelihood trước khi scan crossing để tránh false crossing do noise.
LIKELIHOOD_SMOOTH_FRAMES = 3  # ~1.5s với hop 0.5s

# V7.1: refine từ peak novelty về first-crossing point (legacy, V7.2 không dùng).
REFINE_BACKSCAN_SEC = 12.0

# V7.2 — Likelihood-driven detection
# V7.10: dùng rolling MODE thay median.
# Median chọn middle value → mất context khi argmax flicker qua nhiều state
# (e.g. 9 → 6 → 10 trong 6s, median = 10, mất thông tin state ban đầu 9).
# Mode chọn giá trị xuất hiện nhiều nhất trong cửa sổ → robust với drift
# trung gian. Window 10s đủ rộng để bắt state chính, đủ hẹp để giữ resolution.
ARGMAX_MODE_WINDOW_SEC = 10.0
# Legacy: median kernel cho smoothed_seq (chỉ dùng để hiển thị label trong log)
ARGMAX_MEDIAN_KERNEL_FRAMES = 7

# Ngưỡng "stability before transition": ít nhất bao nhiêu giây trước transition
# phải có key_before là dominant với tỷ lệ MIN_STABILITY_BEFORE_RATIO.
# V7.7: tăng 6→8s vì window key đã tăng 2→5s, lookback cần rộng hơn để có
# đủ statistical sample.
STABILITY_BEFORE_SEC = 8.0
MIN_STABILITY_BEFORE_RATIO = 0.35  # V7.4: nới 0.55→0.35 cho real-world data

# Ngưỡng "stability immediately after transition": kiểm tra chặt hơn persistence
# tổng — yêu cầu N giây ngay sau transition phải dominant key_after để loại
# transition flicker (1 frame chợt đổi rồi quay lại).
STABILITY_AFTER_IMM_SEC = 4.0
MIN_STABILITY_AFTER_IMM_RATIO = 0.20  # V7.8: nới mạnh 0.45→0.20 (T#9 thực tế 0.12)

# Khoảng cách min giữa 2 transition cùng được coi là sự kiện riêng (giây).
# V7.10: giảm 6→4s vì rolling mode đã smooth nhiễu, transitions còn lại đáng
# tin cậy hơn, không cần gap quá rộng.
MIN_TRANSITION_GAP_SEC = 4.0

# Beat snap
BEAT_SNAP_RADIUS_SEC = 1.5

# Confidence
CONF_AUTO_APPLY = 0.80
CONF_POPUP_MIN = 0.50

# Apply lead time (gửi MIDI sớm hơn raise_time bao nhiêu giây).
# V7.14: bỏ lead time vì pipeline V7.13 đã xác định raise_time đủ chính xác,
# không cần shift sớm thêm. Apply tại đúng thời điểm phát hiện.
APPLY_LEAD_SEC = 0


# =============================================================================
# KEY UTILITIES (giữ tương thích với code cũ)
# =============================================================================

MAJOR_KEYS = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MINOR_KEYS = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]

KEY_TO_INDEX = {
    "C": 0,
    "C#": 1, "Db": 1,
    "D": 2,
    "D#": 3, "Eb": 3,
    "E": 4,
    "F": 5,
    "F#": 6, "Gb": 6,
    "G": 7,
    "G#": 8, "Ab": 8,
    "A": 9,
    "A#": 10, "Bb": 10,
    "B": 11,
}


def key_name(index: int, scale: str) -> str:
    index = int(index) % 12
    if str(scale).strip().lower() == "minor":
        return MINOR_KEYS[index]
    return MAJOR_KEYS[index]


def normalize_key_for_scale(key: str, scale: str) -> str:
    idx = KEY_TO_INDEX.get(str(key).strip(), 0)
    return key_name(idx, scale)


def label_from_index(index: int, scale: str) -> str:
    scale = "Minor" if str(scale).lower().startswith("min") else "Major"
    return f"{key_name(index, scale)} {scale}"


def parse_tone(key: str, scale: str) -> dict:
    scale = "Minor" if str(scale).lower().startswith("min") else "Major"
    key = normalize_key_for_scale(key, scale)
    return {
        "key": key,
        "key_index": KEY_TO_INDEX[key],
        "scale": scale,
        "label": f"{key} {scale}",
    }


def mmss(seconds: int | float) -> str:
    seconds = max(0, int(seconds or 0))
    return f"{seconds // 60:02d}:{seconds % 60:02d}"


def clean_youtube_url(url: str) -> str:
    url = (url or "").strip()
    if not url:
        return ""
    try:
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
    except Exception:
        pass
    return url


# =============================================================================
# DATA CLASSES
# =============================================================================

@dataclass
class ModulationCandidate:
    novelty_time_sec: float        # vị trí gốc do novelty curve gợi ý
    snapped_time_sec: float        # đã snap về beat
    snap_distance_sec: float       # khoảng cách từ novelty tới beat (≤ BEAT_SNAP_RADIUS_SEC)
    key_before_index: int
    key_after_index: int
    scale_before: str
    scale_after: str
    semitone_up: int               # delta ∈ {1, 2, 3}
    novelty_height: float          # đã chuẩn hoá [0,1]
    key_gap: float                 # độ chênh likelihood key trước/sau [0,1]
    beat_snap_score: float         # 1.0 nếu trùng beat, giảm dần ra biên [0,1]
    bass_change_score: float       # bằng chứng từ bass line (optional) [0,1]
    confidence: float              # tổng hợp [0,1]

    @property
    def label_before(self) -> str:
        return label_from_index(self.key_before_index, self.scale_before)

    @property
    def label_after(self) -> str:
        return label_from_index(self.key_after_index, self.scale_after)


@dataclass
class AnalysisDebug:
    tail_offset_sec: int = 0
    tail_length_sec: int = 0
    duration_sec: int = 0
    beat_count: int = 0
    novelty_peak_count: int = 0
    candidates_raw: int = 0
    candidates_filtered: int = 0
    used_madmom: bool = False
    notes: list[str] = field(default_factory=list)

    # V7.5 — Detailed reject stats để biết pipeline đi đâu trong audio thật
    transitions_total: int = 0
    rej_scale_diff: int = 0          # scale_before != scale_after
    rej_delta_invalid: int = 0       # delta ngoài SEMITONE_TARGETS
    rej_base_mismatch: int = 0       # V7.9: unified_before != base_unified
    rej_history_short: int = 0       # không đủ frames trước transition
    rej_stability_before: int = 0    # stability_before < threshold
    rej_after_imm_short: int = 0     # không đủ frames sau transition
    rej_stability_after_imm: int = 0
    rej_persistence: int = 0
    transition_log: list[dict] = field(default_factory=list)  # log từng transition đã xét


# =============================================================================
# CORE: AUDIO PROCESSING
# =============================================================================

class _Processor:
    """
    Đối tượng phụ trợ giữ state trong 1 lần phân tích để các bước [B]→[H] dùng
    chung. Tách ra ngoài class chính để dễ test riêng.
    """

    def __init__(self, audio_full: np.ndarray, sr: int, tail_offset_sec: int):
        # audio_full: numpy mono float32, đã được tải từ ffmpeg
        self.sr = int(sr)
        self.tail_offset_sec = int(tail_offset_sec)
        self.audio_full = audio_full
        self.tail = audio_full[self.tail_offset_sec * self.sr:]

        # Lazy fields
        self._harmonic = None
        self._percussive = None
        self._chroma = None
        self._chroma_times = None
        self._key_likelihood = None
        self._key_likelihood_times = None
        self._beat_times = None  # tuyệt đối tính theo full audio

    # ---------------- HPSS ----------------

    def harmonic_percussive(self):
        if self._harmonic is None:
            import librosa  # lazy import để giảm cold start

            audio = self.tail.astype(np.float32, copy=False)
            peak = float(np.max(np.abs(audio))) if audio.size else 0.0
            if peak > 0:
                audio = audio / max(peak, 1e-6)

            # margin=3.0 giúp tách trống mạnh hơn so với mặc định
            self._harmonic, self._percussive = librosa.effects.hpss(audio, margin=3.0)
        return self._harmonic, self._percussive

    # ---------------- CHROMA ----------------

    def chroma(self):
        if self._chroma is None:
            import librosa
            harmonic, _ = self.harmonic_percussive()
            tuning = 0.0
            try:
                t = librosa.estimate_tuning(y=harmonic, sr=self.sr)
                if np.isfinite(t):
                    tuning = float(t)
            except Exception:
                pass

            # V7.3: ĐỔI từ chroma_cens sang chroma_cqt.
            # Lý do: chroma_cens có smoothing nội tại (CENS = "Energy Normalized
            # Statistics" gồm quantization + smoothing). Dù đặt win_len_smooth=11
            # ở V7.2 vẫn còn ~1s smoothing → cộng các smoothing downstream làm
            # tổng shift +7s như anh test thực tế.
            #
            # chroma_cqt là raw chroma từ Constant-Q transform, không smoothing
            # nội tại. Smoothing sẽ được kiểm soát thủ công ở các bước sau
            # (key likelihood mean, argmax median filter) — minh bạch hơn,
            # chính xác hơn cho task detect boundary.
            chroma = librosa.feature.chroma_cqt(
                y=harmonic,
                sr=self.sr,
                hop_length=CHROMA_HOP_LENGTH,
                tuning=tuning,
                n_chroma=12,
                bins_per_octave=36,  # tăng pitch resolution
            )

            # V7.7: chroma medfilt 1.5s. Smoothing chính sẽ do KEY_WINDOW_SEC=5s
            # đảm nhận. Ở đây chỉ cần làm sạch noise spike ngắn.
            try:
                from scipy.signal import medfilt
                frame_dt_local = CHROMA_HOP_LENGTH / float(self.sr)
                k = max(3, int(round(1.5 / frame_dt_local)))
                if k % 2 == 0:
                    k += 1
                # medfilt từng row (mỗi pitch class) độc lập
                smoothed = np.zeros_like(chroma)
                for r in range(chroma.shape[0]):
                    smoothed[r] = medfilt(chroma[r], kernel_size=k)
                chroma = smoothed
            except Exception:
                pass

            # chroma shape: [12, T]
            self._chroma = chroma.astype(np.float32, copy=False)
            n_frames = chroma.shape[1]
            frame_dt = CHROMA_HOP_LENGTH / float(self.sr)
            # times tương đối trong tail
            self._chroma_times = np.arange(n_frames, dtype=np.float64) * frame_dt
        return self._chroma, self._chroma_times

    # ---------------- KEY LIKELIHOOD ----------------

    def key_likelihood(self):
        """
        Trả về:
          likelihood: matrix [T × 24], giá trị Krumhansl correlation đã clip về [0,1]
          times: vector T, thời gian tương đối trong tail (giây) tại tâm cửa sổ
          key_meta: list[dict] gồm 24 mục {key, key_index, scale, label}
        """
        if self._key_likelihood is None:
            chroma, times = self.chroma()
            frame_dt = times[1] - times[0] if len(times) > 1 else CHROMA_HOP_LENGTH / float(self.sr)
            win_frames = max(1, int(round(KEY_WINDOW_SEC / frame_dt)))
            hop_frames = max(1, int(round(KEY_HOP_SEC / frame_dt)))

            # Profile 24 templates đã chuẩn hoá [12 × 24]
            templates = self._build_24_key_templates()  # shape [12, 24]

            n_frames = chroma.shape[1]
            centers = []
            samples = []
            start = 0
            while start + win_frames <= n_frames:
                window = chroma[:, start:start + win_frames]
                vec = window.mean(axis=1)
                norm = np.linalg.norm(vec)
                if norm > 1e-9:
                    vec = vec / norm
                samples.append(vec)
                center_idx = start + win_frames // 2
                centers.append(times[min(center_idx, n_frames - 1)])
                start += hop_frames

            if not samples:
                self._key_likelihood = np.zeros((0, 24), dtype=np.float32)
                self._key_likelihood_times = np.zeros((0,), dtype=np.float64)
            else:
                X = np.stack(samples, axis=0)  # [T, 12]
                # Mỗi cột của templates đã được chuẩn hoá → dot = cosine
                scores = X @ templates  # [T, 24]
                # Krumhansl correlation có thể âm; clip về [0,1] rồi rescale
                scores = np.clip(scores, 0.0, None)
                # Normalize per-row sum để có softmax-like distribution
                row_sum = scores.sum(axis=1, keepdims=True)
                row_sum = np.where(row_sum < 1e-9, 1.0, row_sum)
                self._key_likelihood = (scores / row_sum).astype(np.float32)
                self._key_likelihood_times = np.array(centers, dtype=np.float64)
        return self._key_likelihood, self._key_likelihood_times

    @staticmethod
    def _build_24_key_templates():
        """
        Trả về matrix [12, 24]: cột i (i=0..11) là Major-i (đã chuẩn hoá),
        cột i+12 là Minor-i. Theo thứ tự khớp với KEYS_MAJOR/KEYS_MINOR (i là semitone từ C).
        """
        major = ts.MAJOR_PROFILE.astype(np.float32)
        minor = ts.MINOR_PROFILE.astype(np.float32)

        cols = []
        for i in range(12):
            v = np.roll(major, i)
            v = v / max(np.linalg.norm(v), 1e-9)
            cols.append(v.astype(np.float32))
        for i in range(12):
            v = np.roll(minor, i)
            v = v / max(np.linalg.norm(v), 1e-9)
            cols.append(v.astype(np.float32))
        return np.stack(cols, axis=1)  # [12, 24]

    @staticmethod
    def template_index(key_index: int, scale: str) -> int:
        return int(key_index) % 12 + (12 if str(scale).lower().startswith("min") else 0)

    @staticmethod
    def index_to_key_scale(template_index: int) -> tuple[int, str]:
        template_index = int(template_index) % 24
        if template_index < 12:
            return template_index, "Major"
        return template_index - 12, "Minor"

    # ---------------- NOVELTY ----------------

    def novelty_curve(self):
        """
        Foote 2000 self-similarity novelty bằng checkerboard kernel.
        Trả về novelty[t] kèm times[t] (giây tương đối trong tail), đã chuẩn hoá [0, 1].
        """
        chroma, times = self.chroma()
        if chroma.shape[1] < 8:
            return np.zeros(0, dtype=np.float32), np.zeros(0, dtype=np.float64)

        # Cosine similarity matrix
        norm = np.linalg.norm(chroma, axis=0, keepdims=True)
        norm = np.where(norm < 1e-9, 1.0, norm)
        chroma_n = chroma / norm
        ssm = chroma_n.T @ chroma_n  # [T, T]

        frame_dt = times[1] - times[0] if len(times) > 1 else CHROMA_HOP_LENGTH / float(self.sr)
        kernel_half = max(2, int(round(NOVELTY_KERNEL_SEC / 2.0 / frame_dt)))
        kernel_size = 2 * kernel_half
        # Checkerboard kernel với Gaussian taper
        rng = np.arange(-kernel_half, kernel_half)
        sign = np.where(np.outer(rng, rng) >= 0, 1.0, -1.0)
        # Đặt 4 góc (++ và --) = +1, 2 góc còn lại (+− và −+) = -1
        # Outer >= 0 cho góc ++ và --, ngược lại cho +- và -+
        sigma = kernel_half / 2.0
        gauss = np.exp(-(rng[:, None] ** 2 + rng[None, :] ** 2) / (2.0 * sigma ** 2))
        kernel = (sign * gauss).astype(np.float32)
        # zero-mean để loại bias
        kernel -= kernel.mean()

        T = ssm.shape[0]
        novelty = np.zeros(T, dtype=np.float32)
        for n in range(kernel_half, T - kernel_half):
            patch = ssm[n - kernel_half:n + kernel_half, n - kernel_half:n + kernel_half]
            novelty[n] = float(np.sum(patch * kernel))

        # Chỉ giữ phần dương (boundary)
        novelty = np.clip(novelty, 0.0, None)
        if novelty.max() > 1e-9:
            novelty = novelty / novelty.max()
        return novelty, times

    # ---------------- BEAT TRACKING ----------------

    def beat_times_absolute(self):
        """
        Trả về beat_times tính bằng giây ABSOLUTE so với đầu bài (không phải tail).
        Ưu tiên madmom, fallback librosa.beat.beat_track trên y_percussive.
        """
        if self._beat_times is not None:
            return self._beat_times

        _, percussive = self.harmonic_percussive()

        # Thử madmom (downbeat tracking chính xác hơn)
        beats_local = self._try_madmom_beats(percussive)
        if beats_local is None:
            beats_local = self._librosa_beats(percussive)

        # Cộng offset của tail để có thời gian absolute
        if beats_local is not None and len(beats_local) > 0:
            self._beat_times = np.asarray(beats_local, dtype=np.float64) + float(self.tail_offset_sec)
        else:
            self._beat_times = np.zeros(0, dtype=np.float64)
        return self._beat_times

    def _try_madmom_beats(self, percussive: np.ndarray):
        try:
            from madmom.features.beats import RNNBeatProcessor, BeatTrackingProcessor  # type: ignore
        except Exception:
            return None

        try:
            # madmom yêu cầu sample rate 44100, ta resample tạm
            import librosa
            audio_44k = librosa.resample(percussive, orig_sr=self.sr, target_sr=44100)
            beat_act = RNNBeatProcessor()(audio_44k)
            tracker = BeatTrackingProcessor(fps=100)
            beats = tracker(beat_act)
            if beats is None or len(beats) == 0:
                return None
            return np.asarray(beats, dtype=np.float64)
        except Exception:
            return None

    def _librosa_beats(self, percussive: np.ndarray):
        import librosa
        try:
            tempo, beat_frames = librosa.beat.beat_track(
                y=percussive,
                sr=self.sr,
                hop_length=512,
                tightness=120,
                trim=False,
            )
            beat_times = librosa.frames_to_time(beat_frames, sr=self.sr, hop_length=512)
            return np.asarray(beat_times, dtype=np.float64)
        except Exception:
            return np.zeros(0, dtype=np.float64)


# =============================================================================
# MODULATION DETECTOR
# =============================================================================

class _ModulationDetector:
    @staticmethod
    def _unify_to_major(key_index: int, scale: str) -> int:
        """
        V7.6 — Chuyển key về 'Major space' để so sánh delta bất kể scale.
        Quy ước:
            Major i  → i
            Minor i  → (i + 3) mod 12   (ví dụ A Minor[9] → C Major[0])
        Lý do: Major i và Minor (i-3 mod 12) chia sẻ pitch class set, nên
        chroma không phân biệt được. Quy về cùng "tonality root" giúp delta
        modulation tính đúng dù chroma báo Major hay Minor.
        """
        i = int(key_index) % 12
        s = str(scale).lower()
        if s.startswith("min"):
            return (i + 3) % 12
        return i

    def _find_modulation_unified(
        self,
        likelihood: np.ndarray,  # [T, 24]
        lk_times: np.ndarray,    # [T]
        debug: AnalysisDebug,
    ) -> list[ModulationCandidate]:
        """
        V7.11 — Summed unified likelihood + first crossing approach.

        Thuật toán hoàn toàn mới:
            1. Cho mỗi unified value u ∈ [0, 11], tính summed_lik[t, u] =
               sum likelihood của 2 templates có unify == u (Major i và Minor i-3).
            2. Smooth summed_lik với rolling mean ~8s để loại noise.
            3. base_score[t] = summed_lik[t, base_unified]
            4. Cho mỗi delta ∈ {1, 2, 3}:
                target_score[t] = summed_lik[t, (base_unified + delta) % 12]
                diff[t] = target_score[t] - base_score[t]
                Tìm first frame t* trong tail nửa-sau:
                    - diff[t*] > 0 (target dominant)
                    - mean(diff[t*:] > 0) >= MIN_PERSISTENCE_AFTER_PEAK
                → ứng viên modulation +delta tại t*
            5. Trong các ứng viên, chọn cái có persistence cao nhất × confidence.

        Robust hơn transition-based vì:
            - Không cần argmax (vốn rất noisy)
            - Drift trung gian qua state khác không ảnh hưởng
            - Direct mathematical: when does target become dominant
        """
        if likelihood.size == 0:
            return []

        T = likelihood.shape[0]
        base_unified = self._unify_to_major(self.base_key_index, self.base_scale)

        # ----- Bước 1: Sum likelihood theo unified value -----
        unified_lik = np.zeros((T, 12), dtype=np.float32)
        for tpl in range(24):
            scale = "Major" if tpl < 12 else "Minor"
            u = self._unify_to_major(tpl, scale)
            unified_lik[:, u] += likelihood[:, tpl]

        # ----- Bước 2: Smooth rolling mean ~5s -----
        # V7.12: giảm 8s → 5s. Smoothing rộng cho rolling mean 'mode=same' tạo
        # bias thời gian ~half_window về phía sau (vì giá trị tại t là mean
        # của [t-half, t+half], có chứa giá trị "sau" t nên đỉnh dịch sau).
        # Window 5s → bias ~2.5s, em sẽ compensate ở bước tìm crossing.
        SMOOTH_WINDOW_SEC = 5.0
        frame_dt = float(lk_times[1] - lk_times[0]) if T > 1 else KEY_HOP_SEC
        win = max(3, int(round(SMOOTH_WINDOW_SEC / frame_dt)))
        kernel = np.ones(win, dtype=np.float32) / float(win)
        smoothed = np.zeros_like(unified_lik)
        for u in range(12):
            smoothed[:, u] = np.convolve(unified_lik[:, u], kernel, mode='same')

        # Bias compensation: rolling mean với mode='same' delay tín hiệu ~win//2
        # frames. Khi tìm crossing point, em sẽ shift t_star về trước nửa cửa sổ.
        bias_compensation_frames = win // 2

        base_score = smoothed[:, base_unified]

        # V7.15 — Fetch novelty curve sớm để dùng cho cross-check trong loop
        novelty, n_times = self.proc.novelty_curve()

        # ----- Bước 3-4: Cho mỗi delta, tìm first sustained crossing -----
        candidates: list[ModulationCandidate] = []
        sustain_frames = max(3, int(round(8.0 / frame_dt)))

        for delta in SEMITONE_TARGETS:
            target_unified_val = (base_unified + delta) % 12
            target_score = smoothed[:, target_unified_val]
            diff = target_score - base_score

            # V7.11: search từ phần đầu tail (đã offset 150s cuối bài) trở đi.
            # Chỉ skip vài giây đầu để có history cho stability check.
            search_start = max(int(round(STABILITY_BEFORE_SEC / frame_dt)), 1)
            t_star = -1

            # V7.15 — Sustained dominance + Novelty cross-check.
            # Pick FIRST crossing pass cả 3 filter:
            #   1. Tail persistence: target dominant ≥ 35% đến cuối bài
            #   2. Near-term sustained: target dominant ≥ 70% trong 30s ngay sau
            #   3. Novelty cross-check: novelty curve có peak ≥ 0.20 trong
            #      bán kính 5s (modulation thật có drum fill, transient bridge
            #      thường không có).
            SUSTAIN_NEAR_SEC = 30.0
            SUSTAIN_NEAR_RATIO = 0.70
            MIN_NOVELTY_AT_TSTAR = 0.20
            sustain_near_frames = max(sustain_frames, int(round(SUSTAIN_NEAR_SEC / frame_dt)))

            for i in range(search_start, T - sustain_frames):
                if diff[i] <= 0:
                    continue
                # 1. Tail persistence
                tail_window = diff[i:]
                tail_pos_ratio = float(np.mean(tail_window > 0))
                if tail_pos_ratio < MIN_PERSISTENCE_AFTER_PEAK:
                    continue
                # 2. Near-term sustained
                near_end = min(T, i + sustain_near_frames)
                near_window = diff[i:near_end]
                if len(near_window) < sustain_near_frames * 0.5:
                    if tail_pos_ratio < 0.60:
                        continue
                else:
                    near_pos_ratio = float(np.mean(near_window > 0))
                    if near_pos_ratio < SUSTAIN_NEAR_RATIO:
                        continue
                # 3. Novelty cross-check
                t_local_check = float(lk_times[i])
                nov_at_t = self._novelty_at(t_local_check, novelty, n_times, radius_sec=5.0)
                if nov_at_t < MIN_NOVELTY_AT_TSTAR:
                    continue
                # Pass all → ghi nhận
                t_star = i
                break

            if t_star < 0:
                continue

            # V7.12: compensate bias do rolling mean smoothing.
            # Rolling mean với mode='same' shift đỉnh signal sang sau ~win/2
            # frames (vì giá trị tại t là mean của [t-half, t+half], chứa data
            # tương lai → cross over xảy ra "sớm hơn" so với reality).
            # Compensate bằng shift về trước win/2 frames.
            t_star_compensated = max(0, t_star - bias_compensation_frames)

            # ----- Đánh giá candidate -----
            t_local = float(lk_times[t_star_compensated])
            t_abs = float(self.proc.tail_offset_sec) + t_local

            # Beat snap
            beats_abs = self.proc.beat_times_absolute()
            snapped_abs, snap_dist = self._snap_to_beat(t_abs, beats_abs)
            snap_score = max(0.0, 1.0 - snap_dist / BEAT_SNAP_RADIUS_SEC)

            # Persistence (tỷ lệ frames target dominate sau t*)
            persistence = float(np.mean(diff[t_star:] > 0))

            # Key gap = mean(diff > 0 trong tail)
            key_gap_norm = float(np.clip(np.mean(diff[t_star:]) * 10.0, 0.0, 1.0))

            # Strength score = mean(target_score) trong tail
            strength = float(np.clip(np.mean(target_score[t_star:]) * 5.0, 0.0, 1.0))

            # Novelty cross-evidence (đã fetch ở đầu method)
            nov_score = self._novelty_at(t_local, novelty, n_times, radius_sec=3.0)

            # Bass evidence
            bass_score = self._bass_change_score(t_local)

            base_conf = (
                0.25 * strength
                + 0.20 * persistence
                + 0.15 * key_gap_norm
                + 0.10 * snap_score
                + 0.10 * nov_score
                + 0.05 * bass_score
            )
            if delta in (1, 2):
                base_conf *= 1.10
            confidence = max(0.0, min(1.0, base_conf))

            # Tính key_after theo base_scale của user
            target_key_idx = target_unified_val if self.base_scale.lower() == "major" else (target_unified_val - 3) % 12
            target_scale = self.base_scale.title()  # "Major" or "Minor"

            cand = ModulationCandidate(
                novelty_time_sec=t_abs,
                snapped_time_sec=snapped_abs,
                snap_distance_sec=snap_dist,
                key_before_index=int(self.base_key_index),
                key_after_index=int(target_key_idx),
                scale_before=self.base_scale.title(),
                scale_after=target_scale,
                semitone_up=int(delta),
                novelty_height=float(nov_score),
                key_gap=float(key_gap_norm),
                beat_snap_score=float(snap_score),
                bass_change_score=float(bass_score),
                confidence=float(confidence),
            )
            candidates.append(cand)

            # Log debug entry
            log_entry = {
                "t_local": round(t_local, 2),
                "delta": int(delta),
                "target_unified": int(target_unified_val),
                "persistence": round(persistence, 3),
                "strength": round(strength, 3),
                "key_gap": round(key_gap_norm, 3),
                "snap_score": round(snap_score, 3),
                "confidence": round(confidence, 3),
                "result": "accepted_unified",
            }
            debug.transition_log.append(log_entry)

        # Sort candidates theo confidence
        candidates.sort(key=lambda c: c.confidence, reverse=True)
        return candidates

    @staticmethod
    def _mode_smooth(seq: np.ndarray, window_sec: float, hop_sec: float) -> np.ndarray:
        """
        V7.10 — Rolling mode (giá trị xuất hiện nhiều nhất) trong cửa sổ window_sec.

        Khác median: chọn dominant value thay vì middle value → robust hơn
        với categorical noise có nhiều state. Phù hợp khi argmax đi qua các
        state intermediate ngắn (drift) — drift không thắng được state chính
        về số lượng.

        Output cùng độ dài input. Tại mỗi vị trí, lấy mode của cửa sổ
        [i - half, i + half + 1].
        """
        seq = np.asarray(seq, dtype=np.int32)
        n = len(seq)
        if n == 0:
            return seq.copy()
        win_frames = max(1, int(round(window_sec / max(hop_sec, 1e-6))))
        half = win_frames // 2

        out = seq.copy()
        for i in range(n):
            start = max(0, i - half)
            end = min(n, i + half + 1)
            chunk = seq[start:end]
            values, counts = np.unique(chunk, return_counts=True)
            out[i] = int(values[np.argmax(counts)])
        return out

    @staticmethod
    def _to_unified_seq(template_seq: np.ndarray) -> np.ndarray:
        """
        V7.8 — Chuyển dãy template_index (0-23) sang dãy unified key (0-11).
        template < 12: Major → giữ nguyên
        template >= 12: Minor i → (i+3) % 12
        Vector hoá để nhanh.
        """
        seq = np.asarray(template_seq, dtype=np.int32)
        is_minor = seq >= 12
        major_keys = seq % 12  # key_index 0-11 cho cả 2 nhóm
        unified = np.where(is_minor, (major_keys + 3) % 12, major_keys)
        return unified.astype(np.int32)

    def __init__(self, processor: _Processor, base_key_index: int, base_scale: str):
        self.proc = processor
        self.base_key_index = int(base_key_index) % 12
        self.base_scale = "Minor" if str(base_scale).lower().startswith("min") else "Major"

    def find_candidates(self, debug: AnalysisDebug) -> list[ModulationCandidate]:
        """
        V7.2 — Likelihood-driven primary detection.

        Paradigm:
          - PRIMARY: tìm sustained transition trong argmax(likelihood). Đây là
            điểm chính xác nhất của moment lên tone vì đây là điểm key dominant
            ĐỔI thực sự, không phụ thuộc novelty.
          - SECONDARY: novelty curve làm cộng evidence (tăng confidence nếu
            transition trùng với peak structural change).
        """
        likelihood, lk_times = self.proc.key_likelihood()
        novelty, n_times = self.proc.novelty_curve()
        beats_abs = self.proc.beat_times_absolute()
        debug.beat_count = int(len(beats_abs))

        if likelihood.size == 0:
            debug.notes.append("empty_likelihood")
            return []

        # Đếm novelty peaks (chỉ để debug, không dùng làm anchor primary nữa)
        peaks_for_debug = self._find_novelty_peaks(novelty, n_times)
        debug.novelty_peak_count = len(peaks_for_debug)

        # ----------------------------------------------------------------
        # V7.11 — PRIMARY: Summed unified likelihood + first crossing.
        # Đổi paradigm hoàn toàn: thay vì track transitions (vốn nhiễu vì
        # argmax đi qua state trung gian), TRỰC TIẾP track likelihood của
        # target key (base+1, base+2, base+3) qua thời gian. Modulation =
        # thời điểm đầu tiên target_score > base_score VÀ duy trì.
        # ----------------------------------------------------------------
        candidates = self._find_modulation_unified(likelihood, lk_times, debug)
        debug.transitions_total = len(candidates)
        debug.candidates_raw = len(candidates)

        # V7.5 — Print pipeline state ra console để user thấy ngay lý do silent.
        try:
            tail_off = int(self.proc.tail_offset_sec)
            print(
                f"[End mod {EndModulationService.ALGORITHM_VERSION}] "
                f"base={label_from_index(self.base_key_index, self.base_scale)} "
                f"base_unified={self._unify_to_major(self.base_key_index, self.base_scale)} "
                f"tail_offset={tail_off}s "
                f"novelty_peaks={len(peaks_for_debug)} "
                f"raw_cands={len(candidates)}"
            )
            # V7.11 — In top candidates với format mới
            for i, entry in enumerate(debug.transition_log[:15]):
                t_abs_val = tail_off + entry.get("t_local", 0)
                t_text = f"{int(t_abs_val)//60:02d}:{int(t_abs_val)%60:02d}"
                # Format mới của V7.11
                if "delta" in entry and "target_unified" in entry:
                    print(
                        f"  C#{i+1} {t_text}  +{entry.get('delta')}  "
                        f"target_unified={entry.get('target_unified')}  "
                        f"persist={entry.get('persistence')}  "
                        f"strength={entry.get('strength')}  "
                        f"key_gap={entry.get('key_gap')}  "
                        f"conf={entry.get('confidence')}  "
                        f"{entry.get('result','?')}"
                    )
                else:
                    # Fallback format cũ (transition-based, không còn dùng V7.11)
                    print(
                        f"  T#{i+1} {t_text}  {entry.get('label_before','?')} -> "
                        f"{entry.get('label_after','?')}  d={entry.get('delta_raw','?')}  "
                        f"{entry.get('result','?')}"
                    )
        except Exception:
            pass

        # ----------------------------------------------------------------
        # V7.4 — Fallback novelty-driven nếu likelihood-driven empty.
        # Likelihood-driven có ưu điểm là chính xác về thời gian nhưng
        # bị strict với audio thật (vocal flicker, bass overlap). Khi không
        # tìm được candidate nào, thử novelty-driven (V7.1 algorithm) để
        # đảm bảo user có thông tin để xác nhận, không bị silent.
        # ----------------------------------------------------------------
        if not candidates and len(peaks_for_debug) > 0:
            debug.notes.append("fallback_novelty_driven")
            for peak_time, peak_height in peaks_for_debug:
                cand = self._evaluate_peak(
                    peak_time_local=peak_time,
                    peak_height=peak_height,
                    likelihood=likelihood,
                    lk_times=lk_times,
                    beats_abs=beats_abs,
                )
                if cand is not None:
                    candidates.append(cand)
            debug.candidates_raw = len(candidates)

        # ----------------------------------------------------------------
        # V7.1 — Sắp xếp & dedupe candidate.
        #
        # Pattern thật của lên tone cuối bài: chỉ có 1 sự kiện chuyển key.
        # Nhưng SSM novelty có thể có nhiều peak rải rác (đỉnh đầu là moment
        # bắt đầu, đỉnh sau là verse cuối, fill-in trống...). Nếu các peak
        # CÙNG cho ra cùng key_after → đó là cùng 1 sự kiện modulation, ta
        # chỉ giữ peak SỚM NHẤT (đại diện cho moment bắt đầu thực sự).
        #
        # Bước:
        # 1. Group candidates theo (key_after_index, scale_after).
        # 2. Mỗi group: chọn snapped_time SỚM NHẤT (vì đây là moment đầu),
        #    nhưng giữ confidence MAX của group (đại diện cho độ tin cậy
        #    tổng hợp của sự kiện).
        # 3. Sort các group đại diện theo confidence giảm dần để best đứng đầu.
        # ----------------------------------------------------------------
        if not candidates:
            return []

        groups: dict[tuple[int, str], list[ModulationCandidate]] = {}
        for c in candidates:
            key = (int(c.key_after_index), str(c.scale_after).lower())
            groups.setdefault(key, []).append(c)

        merged: list[ModulationCandidate] = []
        for key, members in groups.items():
            # Sớm nhất theo thời gian
            earliest = min(members, key=lambda x: x.snapped_time_sec)
            # Confidence đại diện = max của cả group (sự kiện duy nhất, evidence
            # cộng dồn; nếu 1 peak khác trong group có conf cao thì cũng nói lên
            # độ tin cậy của chính sự kiện này).
            best_conf = max(m.confidence for m in members)
            best_novelty = max(m.novelty_height for m in members)
            best_key_gap = max(m.key_gap for m in members)
            # Tạo candidate đại diện: time của earliest, score của best
            rep = ModulationCandidate(
                novelty_time_sec=earliest.novelty_time_sec,
                snapped_time_sec=earliest.snapped_time_sec,
                snap_distance_sec=earliest.snap_distance_sec,
                key_before_index=earliest.key_before_index,
                key_after_index=earliest.key_after_index,
                scale_before=earliest.scale_before,
                scale_after=earliest.scale_after,
                semitone_up=earliest.semitone_up,
                novelty_height=best_novelty,
                key_gap=best_key_gap,
                beat_snap_score=earliest.beat_snap_score,
                bass_change_score=earliest.bass_change_score,
                confidence=best_conf,
            )
            merged.append(rep)

        merged.sort(key=lambda c: c.confidence, reverse=True)
        return merged

    def _find_novelty_peaks(self, novelty: np.ndarray, times: np.ndarray):
        """Trả về list (time_local_sec, normalized_height)."""
        if novelty.size < 5:
            return []
        try:
            from scipy.signal import find_peaks
        except Exception:
            find_peaks = None

        # V7.5: giảm threshold 0.20 → 0.10 vì audio thật có novelty peak yếu
        # hơn synthetic (vocal blend nhẹ giữa các đoạn).
        if find_peaks is not None:
            # Khoảng cách tối thiểu 4s giữa 2 peak để tránh bắt cùng 1 sự kiện
            frame_dt = times[1] - times[0] if len(times) > 1 else 0.1
            distance = max(1, int(round(4.0 / frame_dt)))
            idxs, props = find_peaks(novelty, height=0.10, distance=distance)
            return [(float(times[i]), float(novelty[i])) for i in idxs]

        # Fallback đơn giản: local maxima ngưỡng 0.10
        peaks = []
        for i in range(1, len(novelty) - 1):
            if novelty[i] >= 0.10 and novelty[i] >= novelty[i - 1] and novelty[i] >= novelty[i + 1]:
                peaks.append((float(times[i]), float(novelty[i])))
        return peaks

    def _evaluate_peak(
        self,
        peak_time_local: float,
        peak_height: float,
        likelihood: np.ndarray,
        lk_times: np.ndarray,
        beats_abs: np.ndarray,
    ) -> Optional[ModulationCandidate]:
        # Cửa sổ trước/sau peak
        before_mask = (lk_times >= peak_time_local - KEY_BEFORE_LOOKBACK_SEC) & \
                      (lk_times <= peak_time_local - MIN_GAP_FROM_KEY_BEFORE)
        after_mask = (lk_times >= peak_time_local + MIN_GAP_FROM_KEY_BEFORE) & \
                     (lk_times <= peak_time_local + KEY_AFTER_LOOKAHEAD_SEC)

        if not before_mask.any() or not after_mask.any():
            return None

        avg_before = likelihood[before_mask].mean(axis=0)
        avg_after = likelihood[after_mask].mean(axis=0)

        idx_before = int(np.argmax(avg_before))
        idx_after = int(np.argmax(avg_after))

        key_b, scale_b = _Processor.index_to_key_scale(idx_before)
        key_a, scale_a = _Processor.index_to_key_scale(idx_after)

        # Yêu cầu cùng scale (modulation +1/+2/+3 giữ nguyên scale, đây là
        # pattern phổ biến nhất ở Vpop). Nếu khác scale → có thể là detection
        # noise → bỏ qua ở phase này.
        if scale_b != scale_a:
            return None

        delta = (key_a - key_b) % 12
        if delta not in SEMITONE_TARGETS:
            return None

        # V7.5 — BỎ filter scale_before == base_scale.
        # Lý do: chroma_cqt thường nhầm lẫn relative Major/Minor pairs (F# Minor
        # và A Major chia sẻ pitch class set). Trong audio thật, dù base anh
        # nhập là Minor, pipeline có thể detect Major (relative) cho key_before
        # — vẫn là thông tin đúng về modulation, chỉ khác ghi nhãn scale.
        # Filter này khiến tất cả candidates bị reject khi pipeline detect sai
        # scale → 0 candidates → silent fail.

        # ----------------------------------------------------------------
        # V7.1 — Persistence check.
        # Modulation cuối bài thì key_after phải DUY TRÌ đến hết bài.
        # Peak giả ở outro/drum fill có persistence thấp (sau peak chỉ còn vài
        # giây nên key mới không kịp duy trì) → loại được trường hợp app báo
        # 05:01 trong khi modulation thật ở 04:00.
        # ----------------------------------------------------------------
        post_mask = lk_times >= peak_time_local + MIN_GAP_FROM_KEY_BEFORE
        if not post_mask.any():
            return None
        post_argmax = np.argmax(likelihood[post_mask], axis=1)
        persistence = float(np.mean(post_argmax == idx_after))
        if persistence < MIN_PERSISTENCE_AFTER_PEAK:
            return None

        # Key gap: chênh likelihood giữa key_after và key_before tại cửa sổ sau
        gap_after = float(avg_after[idx_after] - avg_after[idx_before])
        gap_before = float(avg_before[idx_before] - avg_before[idx_after])
        key_gap = max(0.0, min(1.0, (gap_after + gap_before) * 4.0))

        # ----------------------------------------------------------------
        # V7.1 — Refine từ peak novelty về first-crossing point.
        # Peak novelty chỉ ra vùng có structural change (~ giữa fill-in trống).
        # Moment chính xác bắt đầu key change là điểm SỚM NHẤT trong cửa sổ
        # backscan mà likelihood(key_after) bắt đầu vượt likelihood(key_before).
        # Đây là cách khắc phục bài 1 sai +7s (peak ở 03:54 nhưng crossing
        # thực sự ở ~03:47).
        # ----------------------------------------------------------------
        refined_time_local = self._refine_to_first_crossing(
            peak_time_local=peak_time_local,
            idx_after=idx_after,
            idx_before=idx_before,
            likelihood=likelihood,
            lk_times=lk_times,
        )

        # Beat snap: dùng vị trí ĐÃ refine (không phải peak novelty thô)
        refined_time_abs = float(self.proc.tail_offset_sec) + float(refined_time_local)
        snapped_abs, snap_dist = self._snap_to_beat(refined_time_abs, beats_abs)
        snap_score = max(0.0, 1.0 - (snap_dist / BEAT_SNAP_RADIUS_SEC))

        # Bass change score (optional): so sánh năng lượng band 50-250 Hz trước
        # và sau peak. Nếu bass nhảy đột biến trùng moment → cộng evidence.
        bass_score = self._bass_change_score(refined_time_local)

        # ----------------------------------------------------------------
        # V7.1 — Confidence formula update.
        # Thêm persistence weight 0.20 vì đây là evidence rất mạnh:
        # modulation thật → key_after duy trì >50% thời gian sau peak.
        # Cộng prior bonus cho delta ∈ {1, 2} (>95% bài Vpop dùng pattern này).
        # ----------------------------------------------------------------
        base_confidence = (
            0.30 * peak_height
            + 0.30 * key_gap
            + 0.10 * snap_score
            + 0.10 * bass_score
            + 0.20 * persistence
        )
        if delta in (1, 2):
            base_confidence *= 1.10
        confidence = max(0.0, min(1.0, base_confidence))

        peak_time_abs = float(self.proc.tail_offset_sec) + float(peak_time_local)

        return ModulationCandidate(
            novelty_time_sec=peak_time_abs,
            snapped_time_sec=snapped_abs,
            snap_distance_sec=snap_dist,
            key_before_index=key_b,
            key_after_index=key_a,
            scale_before=scale_b,
            scale_after=scale_a,
            semitone_up=int(delta),
            novelty_height=float(peak_height),
            key_gap=float(key_gap),
            beat_snap_score=float(snap_score),
            bass_change_score=float(bass_score),
            confidence=float(confidence),
        )

    def _snap_to_beat(self, t_abs: float, beats_abs: np.ndarray) -> tuple[float, float]:
        if beats_abs is None or len(beats_abs) == 0:
            return float(t_abs), float(BEAT_SNAP_RADIUS_SEC)
        diffs = np.abs(beats_abs - float(t_abs))
        i = int(np.argmin(diffs))
        dist = float(diffs[i])
        if dist <= BEAT_SNAP_RADIUS_SEC:
            return float(beats_abs[i]), dist
        # Ngoài bán kính → giữ vị trí gốc, snap distance = bán kính (snap_score=0)
        return float(t_abs), float(BEAT_SNAP_RADIUS_SEC)

    # =================================================================
    # V7.2 — LIKELIHOOD-DRIVEN DETECTION HELPERS
    # =================================================================

    @staticmethod
    def _median_smooth(seq: np.ndarray, kernel: int = 3) -> np.ndarray:
        """
        Median smooth dãy categorical (key index). Dùng scipy.signal.medfilt
        nếu có, fallback rolling-median bằng numpy.

        Median tốt hơn mean cho dãy categorical vì giữ giá trị thật, không
        sinh giá trị trung gian không tồn tại.
        """
        kernel = max(1, int(kernel))
        if kernel % 2 == 0:
            kernel += 1
        if len(seq) < kernel:
            return seq.copy()
        try:
            from scipy.signal import medfilt
            return medfilt(seq.astype(np.float32), kernel_size=kernel).astype(np.int32)
        except Exception:
            half = kernel // 2
            out = seq.copy()
            for i in range(half, len(seq) - half):
                out[i] = int(np.median(seq[i - half:i + half + 1]))
            return out

    def _find_sustained_transitions(
        self,
        smoothed_seq: np.ndarray,
        lk_times: np.ndarray,
    ) -> list[tuple[float, int, int]]:
        """
        Tìm tất cả điểm transition trong dãy argmax đã smooth, với mỗi
        transition trả về (t_local, idx_before, idx_after).

        Lọc:
          - Bỏ qua transition trong vùng quá đầu/cuối (không đủ context)
          - Khoảng cách min giữa 2 transition liên tiếp = MIN_TRANSITION_GAP_SEC
            (ngừa flicker liên tục)
        """
        if len(smoothed_seq) < 4:
            return []

        frame_dt = float(lk_times[1] - lk_times[0]) if len(lk_times) > 1 else KEY_HOP_SEC
        min_gap_frames = max(1, int(round(MIN_TRANSITION_GAP_SEC / frame_dt)))

        transitions: list[tuple[float, int, int]] = []
        last_t_idx = -10**9

        for i in range(1, len(smoothed_seq)):
            if int(smoothed_seq[i]) == int(smoothed_seq[i - 1]):
                continue
            if i - last_t_idx < min_gap_frames:
                continue
            transitions.append((
                float(lk_times[i]),
                int(smoothed_seq[i - 1]),
                int(smoothed_seq[i]),
            ))
            last_t_idx = i
        return transitions

    def _find_sustained_transitions_unified(
        self,
        smoothed_unified: np.ndarray,
        smoothed_seq: np.ndarray,
        lk_times: np.ndarray,
    ) -> list[tuple[float, int, int]]:
        """
        V7.9 — Tìm transitions trong UNIFIED space (Major↔Minor relative pairs
        cùng unified value).

        Trả về (t_local, idx_before_template, idx_after_template) — nhưng
        idx_before/idx_after được chọn là TEMPLATE đại diện cho unified value
        đó tại thời điểm transition (lấy từ smoothed_seq raw).

        Lọc:
          - Chỉ ghi nhận khi unified[i] != unified[i-1]
          - Khoảng cách min giữa 2 transition = MIN_TRANSITION_GAP_SEC
        """
        if len(smoothed_unified) < 4:
            return []

        frame_dt = float(lk_times[1] - lk_times[0]) if len(lk_times) > 1 else KEY_HOP_SEC
        min_gap_frames = max(1, int(round(MIN_TRANSITION_GAP_SEC / frame_dt)))

        transitions: list[tuple[float, int, int]] = []
        last_t_idx = -10**9

        for i in range(1, len(smoothed_unified)):
            if int(smoothed_unified[i]) == int(smoothed_unified[i - 1]):
                continue
            if i - last_t_idx < min_gap_frames:
                continue
            # Lấy template đại diện từ smoothed_seq (raw template) tại i-1 và i
            transitions.append((
                float(lk_times[i]),
                int(smoothed_seq[i - 1]),
                int(smoothed_seq[i]),
            ))
            last_t_idx = i
        return transitions

    def _novelty_at(
        self,
        t_local: float,
        novelty: np.ndarray,
        novelty_times: np.ndarray,
        radius_sec: float = 2.0,
    ) -> float:
        """Lấy novelty value LỚN NHẤT trong bán kính radius_sec quanh t_local."""
        if novelty.size == 0 or len(novelty_times) == 0:
            return 0.0
        mask = np.abs(novelty_times - float(t_local)) <= float(radius_sec)
        if not mask.any():
            return 0.0
        return float(novelty[mask].max())

    def _evaluate_transition(
        self,
        t_local: float,
        idx_before: int,
        idx_after: int,
        likelihood: np.ndarray,
        lk_times: np.ndarray,
        smoothed_seq: np.ndarray,
        novelty: np.ndarray,
        novelty_times: np.ndarray,
        beats_abs: np.ndarray,
        debug: Optional[AnalysisDebug] = None,
        smoothed_unified: Optional[np.ndarray] = None,
    ) -> Optional[ModulationCandidate]:
        """
        V7.2 — Đánh giá 1 transition điểm (likelihood-driven).

        Lọc:
          1. Same scale, delta ∈ SEMITONE_TARGETS, scale_before khớp base_scale.
          2. Stability before: ≥ MIN_STABILITY_BEFORE_RATIO frames trong
             STABILITY_BEFORE_SEC trước phải dominant key_before.
          3. Stability after immediate: ≥ MIN_STABILITY_AFTER_IMM_RATIO frames
             trong STABILITY_AFTER_IMM_SEC ngay sau phải dominant key_after.
             Đây là filter loại flicker 1-frame.
          4. Persistence (đến cuối tail): ≥ MIN_PERSISTENCE_AFTER_PEAK.

        Confidence:
          0.25 * key_gap_local      (gap likelihood tại frame transition)
        + 0.20 * persistence
        + 0.15 * stability_before
        + 0.15 * stability_after_imm
        + 0.10 * novelty_at_t       (cộng evidence từ novelty)
        + 0.10 * snap_score
        + 0.05 * bass_score
        × 1.10 nếu delta ∈ {1, 2}
        """
        key_b, scale_b = _Processor.index_to_key_scale(idx_before)
        key_a, scale_a = _Processor.index_to_key_scale(idx_after)
        delta_raw = (idx_after - idx_before) % 12

        # Log transition đang xét (V7.5)
        log_entry = {
            "t_local": round(float(t_local), 2),
            "label_before": label_from_index(key_b, scale_b),
            "label_after": label_from_index(key_a, scale_a),
            "delta_raw": int(delta_raw),
            "result": "?",
        }

        # V7.6 — Relative-pair unification.
        # chroma_cqt không phân biệt được Major vs relative Minor (chia sẻ pitch
        # class set: A Major ↔ F# Minor, C Major ↔ A Minor...). Nếu transition
        # là Major↔Minor relative pair, semantically tương đương cùng scale.
        # Cách xử lý: chuyển mọi key về Major đại diện (Minor i → Major (i+3)%12),
        # rồi so sánh trên cùng "tonality space" Major.
        # Đồng thời lựa cách interpret `scale` ưu tiên khớp với base_scale của
        # bài để khi save/apply hiển thị đúng Major/Minor anh đã nhập.

        unified_b = self._unify_to_major(idx_before, scale_b)  # 0-11
        unified_a = self._unify_to_major(idx_after, scale_a)
        delta_unified = (unified_a - unified_b) % 12

        # V7.9 — Filter: key_before phải khớp base trong unified space.
        # Lý do: transition F# Major → A Major (unified 6→9, delta=3) không
        # phải modulation từ tone gốc nếu base = F# Minor (unified=9). Đó là
        # transition giả hoặc bridge/instrumental break, không phải lên tone
        # cuối bài.
        # Cho phép tolerance 0 (exact match) vì median smooth đã ổn định.
        base_unified = self._unify_to_major(self.base_key_index, self.base_scale)
        if unified_b != base_unified:
            log_entry["result"] = f"rej_base_mismatch(unified_b={unified_b}, base={base_unified})"
            log_entry["unified_before"] = int(unified_b)
            log_entry["unified_after"] = int(unified_a)
            if debug is not None:
                # Thêm counter vào AnalysisDebug nếu chưa có
                if not hasattr(debug, "rej_base_mismatch"):
                    debug.rej_base_mismatch = 0
                debug.rej_base_mismatch += 1
                debug.transition_log.append(log_entry)
            return None

        # Lọc theo delta đã unify (cho phép Major↔Minor relative pair).
        if delta_unified not in SEMITONE_TARGETS:
            # Backup: thử delta theo scale gốc nếu cùng scale (không qua unify)
            if scale_b == scale_a:
                delta_native = (idx_after - idx_before) % 12
                if delta_native not in SEMITONE_TARGETS:
                    log_entry["result"] = f"rej_delta_invalid(unified={delta_unified}, native={delta_native})"
                    if debug is not None:
                        debug.rej_delta_invalid += 1
                        debug.transition_log.append(log_entry)
                    return None
                delta = delta_native
            else:
                log_entry["result"] = f"rej_delta_invalid(unified={delta_unified})"
                if debug is not None:
                    debug.rej_delta_invalid += 1
                    debug.transition_log.append(log_entry)
                return None
        else:
            delta = delta_unified

        # V7.6: scale của candidate sẽ ưu tiên scale GỐC anh đã nhập (base_scale)
        # vì semantically tương đương qua relative pair, mà UI/MIDI cần biết
        # đúng scale anh đang dùng để gửi CC chính xác.
        # Tính lại key_after_index theo unified delta + base_scale:
        target_unified = (self._unify_to_major(self.base_key_index, self.base_scale) + delta) % 12
        if self.base_scale.lower() == "minor":
            # Minor i ↔ Major (i+3); ngược: Major j ↔ Minor (j-3)
            target_key_index_in_base_scale = (target_unified - 3) % 12
            target_scale = "Minor"
        else:
            target_key_index_in_base_scale = target_unified
            target_scale = "Major"

        # Override key_a/scale_a/idx_after để match base_scale
        # (idx_after dùng trong log + likelihood lookup phải giữ giá trị gốc
        #  để stability/persistence check chạy đúng matrix likelihood)
        # Nhưng key_a HIỂN THỊ và lưu cache → dùng base_scale
        key_a_for_save = key_name(target_key_index_in_base_scale, target_scale)
        scale_a_for_save = target_scale
        key_b_for_save = self.base_scale.title() and key_name(self.base_key_index, self.base_scale)
        scale_b_for_save = "Minor" if self.base_scale.lower() == "minor" else "Major"
        # Không override key_b/scale_b cho local operations bên dưới
        # nhưng ghi label đẹp vào log_entry
        log_entry["unified_delta"] = int(delta)
        log_entry["display_before"] = f"{key_b_for_save} {scale_b_for_save}"
        log_entry["display_after"] = f"{key_a_for_save} {scale_a_for_save}"

        # Index của transition trong lk_times
        t_idx = int(np.argmin(np.abs(lk_times - float(t_local))))
        frame_dt = float(lk_times[1] - lk_times[0]) if len(lk_times) > 1 else KEY_HOP_SEC

        # Stability before — V7.10: dùng smoothed_unified (đã rolling mode 10s).
        # Fallback về unify(smoothed_seq) nếu chưa được pass vào.
        if smoothed_unified is not None:
            unified_seq = smoothed_unified
        else:
            unified_seq = self._to_unified_seq(smoothed_seq)
        target_unified_b = self._unify_to_major(idx_before, scale_b)

        n_before = max(1, int(round(STABILITY_BEFORE_SEC / frame_dt)))
        before_slice = unified_seq[max(0, t_idx - n_before):t_idx]
        if len(before_slice) < n_before * 0.5:
            log_entry["result"] = "rej_history_short"
            if debug is not None:
                debug.rej_history_short += 1
                debug.transition_log.append(log_entry)
            return None  # không đủ history
        stability_before = float(np.mean(before_slice == target_unified_b))
        log_entry["stability_before_unified"] = round(stability_before, 3)
        if stability_before < MIN_STABILITY_BEFORE_RATIO:
            log_entry["result"] = f"rej_stability_before({stability_before:.2f}<{MIN_STABILITY_BEFORE_RATIO})"
            if debug is not None:
                debug.rej_stability_before += 1
                debug.transition_log.append(log_entry)
            return None

        # Stability immediate after
        # V7.8 — Stability/persistence trên UNIFIED SPACE.
        # Lý do: argmax có thể flick giữa các relative pairs (B Major template=11,
        # G# Minor template=20 — cả hai unify=11). Trong raw template space coi
        # chúng khác nhau → stability thấp. Trong unified space → coi cùng key.
        # unified_seq đã được tạo ở phần stability_before phía trên, tái dùng.
        target_unified_a = self._unify_to_major(idx_after, scale_a)

        n_after_imm = max(1, int(round(STABILITY_AFTER_IMM_SEC / frame_dt)))
        after_imm_slice = unified_seq[t_idx:t_idx + n_after_imm]
        if len(after_imm_slice) < n_after_imm * 0.5:
            log_entry["result"] = "rej_after_imm_short"
            if debug is not None:
                debug.rej_after_imm_short += 1
                debug.transition_log.append(log_entry)
            return None
        stability_after_imm = float(np.mean(after_imm_slice == target_unified_a))
        log_entry["stability_after_imm_unified"] = round(stability_after_imm, 3)
        if stability_after_imm < MIN_STABILITY_AFTER_IMM_RATIO:
            log_entry["result"] = f"rej_stability_after_imm({stability_after_imm:.2f}<{MIN_STABILITY_AFTER_IMM_RATIO})"
            if debug is not None:
                debug.rej_stability_after_imm += 1
                debug.transition_log.append(log_entry)
            return None

        # Persistence (đến hết tail) — cũng trên unified space
        post_slice = unified_seq[t_idx:]
        persistence = float(np.mean(post_slice == target_unified_a)) if len(post_slice) > 0 else 0.0
        log_entry["persistence_unified"] = round(persistence, 3)
        if persistence < MIN_PERSISTENCE_AFTER_PEAK:
            log_entry["result"] = f"rej_persistence({persistence:.2f}<{MIN_PERSISTENCE_AFTER_PEAK})"
            if debug is not None:
                debug.rej_persistence += 1
                debug.transition_log.append(log_entry)
            return None

        log_entry["result"] = "accepted"
        if debug is not None:
            debug.transition_log.append(log_entry)

        # Local key gap (tại frame transition)
        key_gap = float(likelihood[t_idx, idx_after] - likelihood[t_idx, idx_before])
        # Khi sát boundary, gap có thể nhỏ → scale lên cho proportional với 1
        key_gap_norm = max(0.0, min(1.0, key_gap * 5.0))

        # Novelty cross-evidence
        nov_score = self._novelty_at(t_local, novelty, novelty_times, radius_sec=2.5)

        # Beat snap (transition point đã chính xác, snap chỉ tinh chỉnh ±beat)
        t_abs = float(self.proc.tail_offset_sec) + float(t_local)
        snapped_abs, snap_dist = self._snap_to_beat(t_abs, beats_abs)
        snap_score = max(0.0, 1.0 - (snap_dist / BEAT_SNAP_RADIUS_SEC))

        # Bass change
        bass_score = self._bass_change_score(t_local)

        base_conf = (
            0.25 * key_gap_norm
            + 0.20 * persistence
            + 0.15 * stability_before
            + 0.15 * stability_after_imm
            + 0.10 * nov_score
            + 0.10 * snap_score
            + 0.05 * bass_score
        )
        if delta in (1, 2):
            base_conf *= 1.10
        confidence = max(0.0, min(1.0, base_conf))

        # V7.6 — Lưu key_before/key_after theo BASE_SCALE anh đã nhập (đã unified
        # qua relative pair). Đảm bảo UI hiển thị + cache + MIDI gửi đúng scale
        # bài hát thật, không phải scale chroma_cqt detect (có thể là relative).
        return ModulationCandidate(
            novelty_time_sec=t_abs,
            snapped_time_sec=snapped_abs,
            snap_distance_sec=snap_dist,
            key_before_index=int(self.base_key_index),
            key_after_index=int(target_key_index_in_base_scale),
            scale_before=scale_b_for_save,
            scale_after=scale_a_for_save,
            semitone_up=int(delta),
            novelty_height=float(nov_score),
            key_gap=float(key_gap_norm),
            beat_snap_score=float(snap_score),
            bass_change_score=float(bass_score),
            confidence=float(confidence),
        )

    def _refine_to_first_crossing(
        self,
        peak_time_local: float,
        idx_after: int,
        idx_before: int,
        likelihood: np.ndarray,
        lk_times: np.ndarray,
    ) -> float:
        """
        V7.1 — Tìm điểm SỚM NHẤT mà likelihood(key_after) bắt đầu vượt
        likelihood(key_before) trong cửa sổ backscan trước peak.

        Lý do: novelty curve đỉnh ở moment cấu trúc đổi mạnh nhất (thường là
        giữa fill-in trống), nhưng key đã bắt đầu chuyển vài giây trước đó.
        First crossing point = điểm thật sự bắt đầu lên tone.

        Algorithm:
            1. Tính diff[t] = likelihood[t, key_after] - likelihood[t, key_before]
            2. Smooth diff bằng moving-average LIKELIHOOD_SMOOTH_FRAMES frames
               để tránh false crossing do noise.
            3. Trong khoảng [peak - REFINE_BACKSCAN_SEC, peak], tìm index lớn
               nhất sao cho diff <= 0 (key_before còn dominate). Crossing point
               là frame ngay sau index đó.
            4. Nếu suốt backscan diff đều > 0 (key_after đã dominate từ trước
               cửa sổ) → trả về biên trái của backscan (đó là điểm sớm nhất
               có thể quan sát được).
            5. Nếu diff không bao giờ vượt 0 trong cửa sổ → fallback về peak
               ban đầu.
        """
        if likelihood.shape[0] == 0 or len(lk_times) == 0:
            return float(peak_time_local)

        diff = likelihood[:, idx_after] - likelihood[:, idx_before]

        # Smooth bằng moving-average. mode='same' giữ độ dài, có thể bị bias
        # ở 2 đầu mảng nhưng không ảnh hưởng vùng giữa nơi có crossing.
        k = max(1, int(LIKELIHOOD_SMOOTH_FRAMES))
        if len(diff) >= k and k >= 2:
            kernel = np.ones(k, dtype=np.float32) / float(k)
            diff_s = np.convolve(diff, kernel, mode='same')
        else:
            diff_s = diff.astype(np.float32, copy=False)

        # Index của peak trong lk_times
        peak_idx = int(np.argmin(np.abs(lk_times - float(peak_time_local))))
        # Biên trái của backscan
        backscan_t = float(peak_time_local) - REFINE_BACKSCAN_SEC
        left_idx = int(np.argmin(np.abs(lk_times - backscan_t)))
        if left_idx > peak_idx:
            left_idx = peak_idx

        # Scan từ peak ngược về left_idx, tìm index lớn nhất có diff_s <= 0
        last_below = -1
        for i in range(peak_idx, left_idx - 1, -1):
            if diff_s[i] <= 0.0:
                last_below = i
                break

        if last_below < 0:
            # Suốt backscan, key_after đã dominate → crossing nằm trước
            # cửa sổ. Trả về biên trái backscan (best guess sớm nhất).
            return float(lk_times[left_idx])

        # Crossing là frame ngay sau last_below
        cross_idx = min(last_below + 1, len(lk_times) - 1)
        # Đảm bảo không vượt quá peak (an toàn về sau)
        cross_idx = min(cross_idx, peak_idx)
        return float(lk_times[cross_idx])

    def _bass_change_score(self, peak_time_local: float) -> float:
        """
        So sánh năng lượng dải tần bass (50-250Hz) trong 4s trước vs 4s sau peak.
        Nếu thay đổi tương đối lớn → bass line đổi → cộng evidence.
        """
        try:
            harmonic, _ = self.proc.harmonic_percussive()
            sr = self.proc.sr
            half = 2.0
            t = float(peak_time_local)
            i_pre_a = max(0, int((t - 2 * half) * sr))
            i_pre_b = max(0, int((t - 0.2) * sr))
            i_post_a = min(len(harmonic), int((t + 0.2) * sr))
            i_post_b = min(len(harmonic), int((t + 2 * half) * sr))
            if i_pre_b - i_pre_a < sr or i_post_b - i_post_a < sr:
                return 0.0

            pre = harmonic[i_pre_a:i_pre_b]
            post = harmonic[i_post_a:i_post_b]

            # Năng lượng bass dùng FFT
            def bass_energy(x):
                fft = np.fft.rfft(x)
                freqs = np.fft.rfftfreq(len(x), d=1.0 / sr)
                mask = (freqs >= 50.0) & (freqs <= 250.0)
                return float(np.sum(np.abs(fft[mask]) ** 2))

            e_pre = bass_energy(pre)
            e_post = bass_energy(post)
            denom = e_pre + e_post
            if denom <= 1e-9:
                return 0.0
            ratio = abs(e_post - e_pre) / denom
            # ratio nằm [0, 1]; map về [0, 1] trực tiếp
            return float(min(1.0, ratio * 1.5))
        except Exception:
            return 0.0


# =============================================================================
# PUBLIC SERVICE (giữ nguyên signature cũ)
# =============================================================================

@dataclass
class SegmentResult:
    """Giữ tương thích với code cũ ở các nơi import từ end_modulation_service."""
    start: int
    end: int
    label: str
    key: str
    key_index: int
    scale: str
    confidence: float
    score: float
    delta_from_base: int
    is_candidate_up: bool


class EndModulationService:
    """
    Phát hiện lên tone cuối bài.

    API chính:
        analyze_end_modulation(youtube_url, base_key, base_scale, progress_cb=None)

    Output dict (giữ nguyên schema cũ để UI/controller không phải đổi):
        ok, has_modulation, candidate_found, debug_reason,
        title, duration, duration_text,
        base_key, base_scale, base_label,
        semitone_up, new_key, new_key_index, new_scale, new_label,
        window_start_sec, window_end_sec,
        raise_time_sec, raise_time_text,
        apply_time_sec, apply_time_text,
        confidence, score,
        message, timeline,
        # mới:
        algorithm_version, debug
    """

    ALGORITHM_VERSION = "v7.15_novelty_crosscheck"

    def __init__(self):
        # P1 — Audio cache layer.
        # Khi user FIX/SỬA TONE → re-analyze chỉ vì base_key/base_scale thay đổi.
        # Audio + HPSS + chroma + likelihood/novelty/beats KHÔNG phụ thuộc vào
        # base → có thể cache 1 lần và tái sử dụng.
        # Cache theo video_id để invalidate khi đổi bài.
        # Schema: {
        #   "video_id": str,
        #   "title": str,
        #   "duration": int,
        #   "tail_offset_sec": int,
        #   "tail_length_sec": int,
        #   "processor": _Processor (đã chạy HPSS+chroma+likelihood+novelty+beats)
        # }
        self._cached_processor: dict | None = None
        self._cache_max_count = 1  # giữ 1 video gần nhất, tránh tốn RAM

    # ---- helpers giữ nguyên public ----

    def get_duration_title(self, youtube_url: str) -> tuple[str, str, int]:
        stream_url, title, duration = ts.get_stream_url(clean_youtube_url(youtube_url))
        return stream_url, title, int(duration or 0)

    def detect_window(self, stream_url: str, start: int, seconds: int) -> dict:
        """Vẫn giữ để các tool debug cũ không bị vỡ."""
        audio = ts.stream_to_numpy(stream_url, seconds=seconds, start_at=start)
        seg = ts.detect_segment(audio)
        top1 = seg["top1"]
        return {
            "key": top1["key"],
            "key_index": int(top1["key_index"]),
            "scale": top1["scale"],
            "label": top1["label"],
            "confidence": float(seg.get("confidence", 0.0) or 0.0),
            "score": float(top1.get("score", 0.0) or 0.0),
            "top2": seg.get("top2"),
            "top8": seg.get("top8"),
        }

    # ---- pipeline mới ----

    def _extract_video_id_simple(self, youtube_url: str) -> str:
        """Helper extract video_id từ URL. Fallback rỗng nếu fail."""
        try:
            from urllib.parse import urlparse, parse_qs
            url = clean_youtube_url(youtube_url)
            parsed = urlparse(url)
            if "youtu.be" in parsed.netloc:
                return parsed.path.strip("/")
            qs = parse_qs(parsed.query)
            return qs.get("v", [""])[0]
        except Exception:
            return ""

    def invalidate_cache(self):
        """Xóa cache (gọi khi user đổi bài)."""
        self._cached_processor = None

    def analyze_end_modulation(
        self,
        youtube_url: str,
        base_key: str,
        base_scale: str,
        window_sec: int = 14,        # giữ tương thích chữ ký, không dùng
        hop_sec: int = 8,            # giữ tương thích chữ ký, không dùng
        start_ratio: float = 0.55,   # giữ tương thích chữ ký, không dùng
        progress_cb: Optional[Callable[[int, str], None]] = None,
    ) -> dict:
        base = parse_tone(base_key, base_scale)
        debug = AnalysisDebug()

        if progress_cb:
            progress_cb(3, "Đang lấy stream YouTube...")

        # P1 — Cache reuse logic.
        # Nếu cùng video_id với cache trước đó → reuse processor (audio + HPSS
        # + chroma + likelihood + novelty + beats đều đã tính). Bỏ qua bước
        # tải audio + tách HPSS (~10-15s) → re-analyze chỉ mất 2-3s.
        video_id = self._extract_video_id_simple(youtube_url)
        cache_hit = False
        if (
            video_id
            and isinstance(self._cached_processor, dict)
            and self._cached_processor.get("video_id") == video_id
        ):
            cache_hit = True
            cached = self._cached_processor
            proc = cached["processor"]
            title = cached.get("title", "")
            duration = int(cached.get("duration", 0) or 0)
            debug.duration_sec = duration
            debug.tail_offset_sec = int(cached.get("tail_offset_sec", 0) or 0)
            debug.tail_length_sec = int(cached.get("tail_length_sec", 0) or 0)
            debug.notes.append("cache_hit_reuse_processor")
            if progress_cb:
                progress_cb(70, "Tái sử dụng cache (audio + HPSS + chroma đã tính)...")
        else:
            stream_url, title, duration = self.get_duration_title(youtube_url)
            debug.duration_sec = int(duration)
            if duration <= 0:
                raise RuntimeError("Không lấy được thời lượng bài hát từ YouTube.")

            # ------------------------------------------------------------
            # [A] Tải audio: full bài.
            # ------------------------------------------------------------
            if progress_cb:
                progress_cb(8, "Đang tải audio đầy đủ từ YouTube...")
            audio = ts.stream_to_numpy(stream_url, seconds=int(duration) + 2, start_at=0)

            tail_seconds = max(TAIL_SECONDS_DEFAULT, int(duration * 0.30))
            tail_seconds = min(tail_seconds, duration)
            tail_offset = max(0, duration - tail_seconds)

            debug.tail_offset_sec = int(tail_offset)
            debug.tail_length_sec = int(tail_seconds)

            # ------------------------------------------------------------
            # [B][C] HPSS + chroma → tính lazy bên trong _Processor.
            # ------------------------------------------------------------
            if progress_cb:
                progress_cb(20, "Đang tách nguồn âm (HPSS) và tính chroma...")

            proc = _Processor(audio_full=audio, sr=SR, tail_offset_sec=tail_offset)
            proc.harmonic_percussive()

            if progress_cb:
                progress_cb(45, "Đang quét key likelihood theo thời gian...")
            proc.key_likelihood()

            # ------------------------------------------------------------
            # [E] Self-similarity novelty.
            # ------------------------------------------------------------
            if progress_cb:
                progress_cb(60, "Đang phát hiện ranh giới cấu trúc (SSM novelty)...")
            proc.novelty_curve()

            # P1 — Lưu cache cho lần sau (nếu cùng video_id)
            if video_id:
                self._cached_processor = {
                    "video_id": video_id,
                    "title": title,
                    "duration": int(duration),
                    "tail_offset_sec": debug.tail_offset_sec,
                    "tail_length_sec": debug.tail_length_sec,
                    "processor": proc,
                }

        # ------------------------------------------------------------
        # [G] Beat tracking.
        # ------------------------------------------------------------
        if progress_cb:
            progress_cb(72, "Đang phát hiện beat...")
        beats_abs = proc.beat_times_absolute()
        debug.used_madmom = self._beats_came_from_madmom(proc)

        # ------------------------------------------------------------
        # [F][H] Modulation candidates.
        # ------------------------------------------------------------
        if progress_cb:
            progress_cb(85, "Đang xác định ứng viên lên tone...")

        detector = _ModulationDetector(
            processor=proc,
            base_key_index=base["key_index"],
            base_scale=base["scale"],
        )
        candidates = detector.find_candidates(debug)
        debug.candidates_filtered = sum(1 for c in candidates if c.confidence >= CONF_POPUP_MIN)

        # ------------------------------------------------------------
        # Tổng hợp kết quả + timeline (cho UI debug).
        # ------------------------------------------------------------
        if progress_cb:
            progress_cb(96, "Đang tổng hợp kết quả...")

        timeline = self._build_timeline(proc, base, candidates)

        if not candidates:
            return self._empty_result(
                base=base,
                title=title,
                duration=duration,
                timeline=timeline,
                debug=debug,
                message="Chưa thấy ứng viên lên tone +1/+2/+3 ở cuối bài.",
                debug_reason="no_candidate",
            )

        best = candidates[0]
        # Trong giai đoạn G1 (test): vẫn để UI hiển thị popup ngay cả khi
        # confidence thấp; nhưng `has_modulation` chỉ True khi confidence >= ngưỡng auto.
        has_mod = bool(best.confidence >= CONF_AUTO_APPLY)
        candidate_found = bool(best.confidence >= CONF_POPUP_MIN)

        # Nếu candidate rất yếu → trả về như "không có"
        if not candidate_found and best.confidence < (CONF_POPUP_MIN * 0.6):
            return self._empty_result(
                base=base,
                title=title,
                duration=duration,
                timeline=timeline,
                debug=debug,
                message=f"Có dấu hiệu rất yếu (conf={best.confidence:.2f}) — chưa đáng tin.",
                debug_reason="low_confidence_below_popup_min",
            )

        raise_time_sec = int(round(best.snapped_time_sec))
        raise_time_sec = max(0, min(raise_time_sec, int(duration)))
        apply_time_sec = max(0, raise_time_sec - APPLY_LEAD_SEC)

        new_key = key_name(best.key_after_index, best.scale_after)
        new_label = label_from_index(best.key_after_index, best.scale_after)

        # Cửa sổ phát hiện = ±1.5s quanh thời điểm sau snap (cho UI hiển thị range).
        window_start = max(0, raise_time_sec - 2)
        window_end = min(int(duration), raise_time_sec + 2)

        if has_mod:
            debug_reason = "mir_v7_high_confidence_auto_ready"
            message = f"Có lên tone +{best.semitone_up} tại {mmss(raise_time_sec)} → {new_label} (độ tin cậy cao)."
        else:
            debug_reason = "mir_v7_medium_confidence_user_confirm"
            message = f"Có khả năng lên tone +{best.semitone_up} tại {mmss(raise_time_sec)} → {new_label}. Cần xác nhận."

        return {
            "ok": True,
            "has_modulation": has_mod,
            "candidate_found": True,
            "debug_reason": debug_reason,
            "title": title,
            "duration": int(duration),
            "duration_text": mmss(duration),
            "base_key": base["key"],
            "base_scale": base["scale"],
            "base_label": base["label"],
            "semitone_up": int(best.semitone_up),
            "new_key": new_key,
            "new_key_index": int(best.key_after_index),
            "new_scale": best.scale_after,
            "new_label": new_label,
            "window_start_sec": int(window_start),
            "window_end_sec": int(window_end),
            "raise_time_sec": int(raise_time_sec),
            "raise_time_text": mmss(raise_time_sec),
            "apply_time_sec": int(apply_time_sec),
            "apply_time_text": mmss(apply_time_sec),
            "confidence": round(float(best.confidence), 3),
            "score": round(float(best.confidence), 3),
            "hit_count": 1,
            "tail_count": int(len(candidates)),
            "tail_ratio": 1.0 if has_mod else 0.5,
            "message": message,
            "timeline": timeline,
            "algorithm_version": self.ALGORITHM_VERSION,
            "debug": {
                "tail_offset_sec": debug.tail_offset_sec,
                "tail_length_sec": debug.tail_length_sec,
                "duration_sec": debug.duration_sec,
                "beat_count": debug.beat_count,
                "novelty_peak_count": debug.novelty_peak_count,
                "transitions_total": debug.transitions_total,
                "candidates_raw": debug.candidates_raw,
                "candidates_filtered": debug.candidates_filtered,
                "rej_scale_diff": debug.rej_scale_diff,
                "rej_delta_invalid": debug.rej_delta_invalid,
                "rej_base_mismatch": debug.rej_base_mismatch,
                "rej_history_short": debug.rej_history_short,
                "rej_stability_before": debug.rej_stability_before,
                "rej_after_imm_short": debug.rej_after_imm_short,
                "rej_stability_after_imm": debug.rej_stability_after_imm,
                "rej_persistence": debug.rej_persistence,
                "transition_log": list(debug.transition_log[:30]),
                "used_madmom": bool(debug.used_madmom),
                "all_candidates": [
                    {
                        "snapped_time_sec": round(c.snapped_time_sec, 2),
                        "snapped_time_text": mmss(c.snapped_time_sec),
                        "novelty_time_sec": round(c.novelty_time_sec, 2),
                        "snap_distance_sec": round(c.snap_distance_sec, 3),
                        "label_before": c.label_before,
                        "label_after": c.label_after,
                        "semitone_up": c.semitone_up,
                        "novelty_height": round(c.novelty_height, 3),
                        "key_gap": round(c.key_gap, 3),
                        "beat_snap_score": round(c.beat_snap_score, 3),
                        "bass_change_score": round(c.bass_change_score, 3),
                        "confidence": round(c.confidence, 3),
                    }
                    for c in candidates[:6]
                ],
                "notes": list(debug.notes),
            },
        }

    # =============================================================
    # INTERNAL HELPERS
    # =============================================================

    def _beats_came_from_madmom(self, proc: _Processor) -> bool:
        # Cách đơn giản: thử import madmom để biết có sẵn hay không. Module
        # _Processor đã handle thực tế bên trong, chỗ này chỉ flag để debug.
        try:
            import madmom  # noqa: F401
            return True
        except Exception:
            return False

    def _empty_result(
        self,
        base: dict,
        title: str,
        duration: int,
        timeline: list,
        debug: AnalysisDebug,
        message: str,
        debug_reason: str,
    ) -> dict:
        return {
            "ok": True,
            "has_modulation": False,
            "candidate_found": False,
            "debug_reason": debug_reason,
            "title": title,
            "duration": int(duration),
            "duration_text": mmss(duration),
            "base_key": base["key"],
            "base_scale": base["scale"],
            "base_label": base["label"],
            "message": message,
            "timeline": timeline,
            "algorithm_version": self.ALGORITHM_VERSION,
            "debug": {
                "tail_offset_sec": debug.tail_offset_sec,
                "tail_length_sec": debug.tail_length_sec,
                "duration_sec": debug.duration_sec,
                "beat_count": debug.beat_count,
                "novelty_peak_count": debug.novelty_peak_count,
                "transitions_total": debug.transitions_total,
                "candidates_raw": debug.candidates_raw,
                "candidates_filtered": debug.candidates_filtered,
                "rej_scale_diff": debug.rej_scale_diff,
                "rej_delta_invalid": debug.rej_delta_invalid,
                "rej_base_mismatch": debug.rej_base_mismatch,
                "rej_history_short": debug.rej_history_short,
                "rej_stability_before": debug.rej_stability_before,
                "rej_after_imm_short": debug.rej_after_imm_short,
                "rej_stability_after_imm": debug.rej_stability_after_imm,
                "rej_persistence": debug.rej_persistence,
                "transition_log": list(debug.transition_log[:30]),
                "used_madmom": bool(debug.used_madmom),
                "all_candidates": [],
                "notes": list(debug.notes),
            },
        }

    def _build_timeline(
        self,
        proc: _Processor,
        base: dict,
        candidates: list[ModulationCandidate],
    ) -> list[dict]:
        """
        Trả về timeline thô để UI có thể vẽ/log. Mỗi entry là 1 cửa sổ key
        likelihood (cùng grid với likelihood matrix), kèm key dominant tại đó
        và delta so với base.
        """
        timeline: list[dict] = []
        try:
            likelihood, lk_times = proc.key_likelihood()
            base_idx = int(base["key_index"]) % 12
            base_scale = base["scale"]
            for i in range(likelihood.shape[0]):
                row = likelihood[i]
                tpl = int(np.argmax(row))
                key_i, scale_i = _Processor.index_to_key_scale(tpl)
                delta = (key_i - base_idx) % 12 if scale_i.lower() == base_scale.lower() else -1
                cand_up = scale_i.lower() == base_scale.lower() and (delta in SEMITONE_TARGETS)
                t_abs = int(round(proc.tail_offset_sec + lk_times[i]))
                timeline.append({
                    "time": mmss(t_abs),
                    "start": int(t_abs),
                    "end": int(t_abs + KEY_WINDOW_SEC),
                    "label": label_from_index(key_i, scale_i),
                    "confidence": round(float(row[tpl]), 3),
                    "delta": int(delta),
                    "candidate_up": bool(cand_up),
                })
        except Exception:
            pass
        return timeline

    # =============================================================
    # LEGACY (giữ tương thích, KHÔNG dùng trong pipeline mới nhưng
    # vẫn export để các tool debug cũ không bị vỡ import)
    # =============================================================

    def build_end_windows(
        self,
        duration: int,
        window_sec: int = 14,
        hop_sec: int = 8,
        start_ratio: float = 0.55,
    ) -> list[int]:
        duration = int(duration or 0)
        window_sec = int(window_sec)
        hop_sec = int(hop_sec)

        if duration <= 0:
            return []

        start_min = max(0, int(duration * float(start_ratio)))
        end_max = max(0, duration - window_sec - 2)

        if end_max <= start_min:
            start_min = max(0, duration - window_sec * 4)

        starts = []
        s = start_min
        while s <= end_max:
            starts.append(int(s))
            s += hop_sec

        for ratio in [0.70, 0.78, 0.84, 0.90]:
            ss = max(0, min(int(duration * ratio), end_max))
            if all(abs(ss - x) >= max(4, hop_sec // 2) for x in starts):
                starts.append(ss)

        return sorted(set(starts))
