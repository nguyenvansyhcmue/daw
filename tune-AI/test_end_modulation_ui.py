from __future__ import annotations

"""
Tool test riêng: phát hiện đoạn cuối bài karaoke có lên tone (+1 / +2 semitone) hay không.

Cách dùng:
    .\.venv\Scripts\python.exe test_end_modulation_ui.py

Đặt file này ở thư mục gốc project, cùng cấp với main.py, core/, ui/.

Nguyên tắc:
- Tool chỉ test, không gửi MIDI, không lưu cache app chính.
- Dùng tone gốc anh nhập hoặc tự lấy từ Detect Chính nếu anh muốn.
- Phân tích chủ yếu 55% cuối bài, chia nhiều cửa sổ ngắn.
- Tìm xem từ gần cuối bài có chuyển ổn định sang key +1 hoặc +2 semitone cùng scale không.
"""

import json
import math
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    from PyQt6.QtCore import Qt, pyqtSignal
    from PyQt6.QtWidgets import (
        QApplication,
        QCheckBox,
        QComboBox,
        QFileDialog,
        QFormLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QMessageBox,
        QPushButton,
        QPlainTextEdit,
        QProgressBar,
        QSpinBox,
        QVBoxLayout,
        QWidget,
    )
except Exception as e:
    raise RuntimeError("Thiếu PyQt6. Hãy chạy trong .venv của project.") from e

# Import lõi tone hiện có của app anh.
try:
    from core.services import tone_service as ts
except Exception as e:
    raise RuntimeError(
        "Không import được core.services.tone_service. "
        "Hãy đặt file test_end_modulation_ui.py ở thư mục gốc project, cùng cấp main.py."
    ) from e


MAJOR_KEYS = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MINOR_KEYS = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]

KEY_TO_INDEX = {
    "C": 0,
    "C#": 1,
    "Db": 1,
    "D": 2,
    "D#": 3,
    "Eb": 3,
    "E": 4,
    "F": 5,
    "F#": 6,
    "Gb": 6,
    "G": 7,
    "G#": 8,
    "Ab": 8,
    "A": 9,
    "A#": 10,
    "Bb": 10,
    "B": 11,
}


def no_sound_message(parent, title: str, text: str):
    """Popup không âm thanh Windows."""
    box = QMessageBox(parent)
    box.setWindowTitle(title)
    box.setText(text)
    box.setIcon(QMessageBox.Icon.NoIcon)
    box.setStandardButtons(QMessageBox.StandardButton.Ok)
    return box.exec()


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


@dataclass
class SegmentResult:
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


def get_duration_title(youtube_url: str) -> tuple[str, str, int]:
    stream_url, title, duration = ts.get_stream_url(youtube_url)
    return stream_url, title, int(duration or 0)


def detect_window(stream_url: str, start: int, seconds: int) -> dict:
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


def build_end_windows(duration: int, window_sec: int = 14, hop_sec: int = 8, start_ratio: float = 0.55):
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

    # Bảo đảm có vài đoạn rất cuối, vì karaoke Việt hay lên tone gần cuối.
    for ratio in [0.70, 0.78, 0.84, 0.90]:
        ss = max(0, min(int(duration * ratio), end_max))
        if all(abs(ss - x) >= max(4, hop_sec // 2) for x in starts):
            starts.append(ss)

    starts = sorted(set(starts))
    return starts


def analyze_end_modulation(
    youtube_url: str,
    base_key: str,
    base_scale: str,
    window_sec: int = 14,
    hop_sec: int = 8,
    start_ratio: float = 0.55,
    progress_cb=None,
) -> dict:
    base = parse_tone(base_key, base_scale)

    if progress_cb:
        progress_cb(3, "Đang lấy stream YouTube...")

    stream_url, title, duration = get_duration_title(youtube_url)
    if duration <= 0:
        raise RuntimeError("Không lấy được thời lượng bài hát từ YouTube.")

    starts = build_end_windows(duration, window_sec=window_sec, hop_sec=hop_sec, start_ratio=start_ratio)
    if not starts:
        raise RuntimeError("Không tạo được cửa sổ phân tích cuối bài.")

    target_plus = {
        1: (base["key_index"] + 1) % 12,
        2: (base["key_index"] + 2) % 12,
    }

    segment_results: list[SegmentResult] = []

    for i, start in enumerate(starts):
        if progress_cb:
            pct = int(5 + (i / max(1, len(starts))) * 80)
            progress_cb(pct, f"Đang phân tích đoạn {i + 1}/{len(starts)}: {mmss(start)}")

        try:
            d = detect_window(stream_url, start, window_sec)
        except Exception as e:
            # Bỏ qua đoạn lỗi mạng/stream nhỏ, không làm hỏng toàn bài.
            continue

        delta = (int(d["key_index"]) - int(base["key_index"])) % 12
        # Chỉ xét lên +1 / +2 cùng scale là ứng viên thật sự.
        same_scale = str(d["scale"]).lower() == str(base["scale"]).lower()
        is_candidate = same_scale and delta in (1, 2)

        segment_results.append(
            SegmentResult(
                start=start,
                end=min(duration, start + window_sec),
                label=str(d["label"]),
                key=str(d["key"]),
                key_index=int(d["key_index"]),
                scale=str(d["scale"]),
                confidence=float(d["confidence"]),
                score=float(d["score"]),
                delta_from_base=int(delta),
                is_candidate_up=bool(is_candidate),
            )
        )

    if progress_cb:
        progress_cb(88, "Đang tìm điểm lên tone ổn định...")

    if not segment_results:
        raise RuntimeError("Không phân tích được đoạn nào ở phần cuối bài.")

    # Tìm candidate +1 / +2 có chuỗi ổn định nhất gần cuối.
    best = None

    for semitone_up in (1, 2):
        target_idx = target_plus[semitone_up]
        target_label = label_from_index(target_idx, base["scale"])

        # đánh dấu các segment trùng target label cùng scale.
        flags = []
        for r in segment_results:
            flags.append(
                r.scale.lower() == base["scale"].lower()
                and r.key_index == target_idx
                and r.confidence >= 0.08
            )

        # Tìm điểm bắt đầu mà từ đó về sau có tỷ lệ target cao.
        for start_i in range(len(segment_results)):
            tail = segment_results[start_i:]
            tail_flags = flags[start_i:]
            if len(tail) < 2:
                continue

            hit_count = sum(1 for x in tail_flags if x)
            tail_ratio = hit_count / max(1, len(tail_flags))
            avg_conf = sum(r.confidence for r, ok in zip(tail, tail_flags) if ok) / max(1, hit_count)

            # Ưu tiên nửa cuối và chuỗi càng gần ending càng đáng tin.
            start_time = tail[0].start
            late_bonus = min(1.0, max(0.0, start_time / max(1, duration)))

            # Kiểm tra trước điểm chuyển đa phần không phải đã là target.
            before = segment_results[:start_i]
            before_flags = flags[:start_i]
            before_ratio = sum(1 for x in before_flags if x) / max(1, len(before_flags)) if before else 0.0

            # Điểm tổng: cần sau điểm chuyển có target rõ, trước đó chưa target quá nhiều.
            score = (
                0.48 * tail_ratio
                + 0.24 * avg_conf
                + 0.18 * late_bonus
                + 0.10 * max(0.0, tail_ratio - before_ratio)
            )

            if hit_count >= 2 and tail_ratio >= 0.45:
                item = {
                    "semitone_up": semitone_up,
                    "new_key_index": target_idx,
                    "new_key": key_name(target_idx, base["scale"]),
                    "new_scale": base["scale"],
                    "new_label": target_label,
                    "raise_time_sec": start_time,
                    "raise_time_text": mmss(start_time),
                    "hit_count": hit_count,
                    "tail_count": len(tail),
                    "tail_ratio": tail_ratio,
                    "avg_confidence": avg_conf,
                    "before_ratio": before_ratio,
                    "score": score,
                }
                if best is None or item["score"] > best["score"]:
                    best = item

    # Nếu không có chuỗi rõ ràng, thử heuristic: 2 segment cuối cùng cùng là +1/+2.
    if best is None and len(segment_results) >= 2:
        last = segment_results[-3:]
        for semitone_up in (1, 2):
            target_idx = target_plus[semitone_up]
            hits = [r for r in last if r.key_index == target_idx and r.scale.lower() == base["scale"].lower()]
            if len(hits) >= 2:
                start_time = hits[0].start
                avg_conf = sum(r.confidence for r in hits) / len(hits)
                best = {
                    "semitone_up": semitone_up,
                    "new_key_index": target_idx,
                    "new_key": key_name(target_idx, base["scale"]),
                    "new_scale": base["scale"],
                    "new_label": label_from_index(target_idx, base["scale"]),
                    "raise_time_sec": start_time,
                    "raise_time_text": mmss(start_time),
                    "hit_count": len(hits),
                    "tail_count": len(last),
                    "tail_ratio": len(hits) / len(last),
                    "avg_confidence": avg_conf,
                    "before_ratio": 0.0,
                    "score": 0.55 + 0.25 * avg_conf,
                }
                break

    if progress_cb:
        progress_cb(96, "Đang tổng hợp kết quả...")

    timeline = [
        {
            "time": mmss(r.start),
            "start": r.start,
            "end": r.end,
            "label": r.label,
            "confidence": round(r.confidence, 3),
            "delta": r.delta_from_base,
            "candidate_up": r.is_candidate_up,
        }
        for r in segment_results
    ]

    if best is None:
        return {
            "ok": True,
            "has_modulation": False,
            "title": title,
            "duration": duration,
            "duration_text": mmss(duration),
            "base_key": base["key"],
            "base_scale": base["scale"],
            "base_label": base["label"],
            "message": "Chưa thấy dấu hiệu lên tone +1/+2 đủ ổn định ở cuối bài.",
            "timeline": timeline,
        }

    # Confidence không nên quá ảo. Dùng score + tỷ lệ hit.
    confidence = max(0.0, min(0.98, 0.45 * best["score"] + 0.35 * best["tail_ratio"] + 0.20 * best["avg_confidence"]))

    return {
        "ok": True,
        "has_modulation": confidence >= 0.48,
        "title": title,
        "duration": duration,
        "duration_text": mmss(duration),
        "base_key": base["key"],
        "base_scale": base["scale"],
        "base_label": base["label"],
        "semitone_up": best["semitone_up"],
        "new_key": best["new_key"],
        "new_key_index": best["new_key_index"],
        "new_scale": best["new_scale"],
        "new_label": best["new_label"],
        "raise_time_sec": int(best["raise_time_sec"]),
        "raise_time_text": best["raise_time_text"],
        "confidence": round(confidence, 3),
        "score": round(float(best["score"]), 3),
        "hit_count": best["hit_count"],
        "tail_count": best["tail_count"],
        "tail_ratio": round(best["tail_ratio"], 3),
        "message": f"Có khả năng lên tone +{best['semitone_up']} tại {best['raise_time_text']} → {best['new_label']}",
        "timeline": timeline,
    }


class EndModulationTestWindow(QWidget):
    result_signal = pyqtSignal(object)
    status_signal = pyqtSignal(int, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("THM - Test phát hiện lên tone cuối bài")
        self.resize(980, 760)
        self.last_result: dict[str, Any] | None = None
        self.running = False

        self.result_signal.connect(self.on_result)
        self.status_signal.connect(self.on_status)
        self.build_ui()
        self.apply_style()

    def build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(14, 14, 14, 14)
        root.setSpacing(10)

        title = QLabel("TEST PHÁT HIỆN LÊN TONE CUỐI BÀI")
        title.setObjectName("Title")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        root.addWidget(title)

        form_box = QGroupBox("Thông tin test")
        form = QFormLayout(form_box)
        form.setSpacing(8)

        self.edt_url = QLineEdit()
        self.edt_url.setPlaceholderText("Dán link YouTube karaoke...")
        form.addRow("YouTube URL:", self.edt_url)

        row_tone = QHBoxLayout()
        self.cbo_key = QComboBox()
        self.cbo_scale = QComboBox()
        self.cbo_scale.addItems(["Major", "Minor"])
        self.cbo_key.addItems(MAJOR_KEYS)
        self.cbo_scale.currentTextChanged.connect(self.reload_key_options)
        row_tone.addWidget(QLabel("Key gốc:"))
        row_tone.addWidget(self.cbo_key)
        row_tone.addWidget(QLabel("Scale:"))
        row_tone.addWidget(self.cbo_scale)
        row_tone.addStretch()
        form.addRow("Tone đầu bài:", row_tone)

        row_opts = QHBoxLayout()
        self.spin_window = QSpinBox()
        self.spin_window.setRange(8, 30)
        self.spin_window.setValue(14)
        self.spin_window.setSuffix(" s")

        self.spin_hop = QSpinBox()
        self.spin_hop.setRange(4, 20)
        self.spin_hop.setValue(8)
        self.spin_hop.setSuffix(" s")

        self.spin_start = QSpinBox()
        self.spin_start.setRange(40, 75)
        self.spin_start.setValue(55)
        self.spin_start.setSuffix(" %")

        row_opts.addWidget(QLabel("Window:"))
        row_opts.addWidget(self.spin_window)
        row_opts.addWidget(QLabel("Hop:"))
        row_opts.addWidget(self.spin_hop)
        row_opts.addWidget(QLabel("Bắt đầu phân tích từ:"))
        row_opts.addWidget(self.spin_start)
        row_opts.addStretch()
        form.addRow("Tùy chọn:", row_opts)

        root.addWidget(form_box)

        btn_row = QHBoxLayout()
        self.btn_run = QPushButton("PHÂN TÍCH LÊN TONE CUỐI BÀI")
        self.btn_run.clicked.connect(self.run_test)
        self.btn_save = QPushButton("LƯU LOG TEST")
        self.btn_save.clicked.connect(self.save_log)
        self.btn_clear = QPushButton("XÓA MÀN HÌNH")
        self.btn_clear.clicked.connect(lambda: self.txt_result.clear())
        btn_row.addWidget(self.btn_run)
        btn_row.addWidget(self.btn_save)
        btn_row.addWidget(self.btn_clear)
        root.addLayout(btn_row)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setFormat("%p%")
        root.addWidget(self.progress)

        self.lbl_status = QLabel("Sẵn sàng.")
        self.lbl_status.setObjectName("Status")
        root.addWidget(self.lbl_status)

        self.txt_result = QPlainTextEdit()
        self.txt_result.setReadOnly(True)
        root.addWidget(self.txt_result, 1)

        help_text = QLabel(
            "Lưu ý: Tool này chỉ test riêng phát hiện lên tone cuối bài, không gửi MIDI, không lưu cache app chính. "
            "Nên mở video trong Brave và để phát được vài giây trước khi test nếu YouTube hay chặn."
        )
        help_text.setWordWrap(True)
        help_text.setObjectName("Help")
        root.addWidget(help_text)

    def apply_style(self):
        self.setStyleSheet("""
            QWidget { background: #f5f7fb; color: #111827; font-size: 13px; }
            QLabel#Title { font-size: 22px; font-weight: 900; color: #0f172a; padding: 8px; }
            QLabel#Status { color: #2563eb; font-weight: 700; }
            QLabel#Help { color: #475569; }
            QGroupBox { background: white; border: 1px solid #d7dce5; border-radius: 10px; margin-top: 8px; padding: 10px; font-weight: 800; }
            QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
            QLineEdit, QComboBox, QSpinBox { background: white; border: 1px solid #cbd5e1; border-radius: 8px; padding: 7px; min-height: 22px; }
            QPushButton { background: #e8eefc; border: 1px solid #cbd5e1; border-radius: 10px; padding: 10px 14px; font-weight: 900; }
            QPushButton:hover { background: #dbeafe; }
            QPlainTextEdit { background: #0f172a; color: #e5e7eb; border-radius: 10px; padding: 10px; font-family: Consolas, monospace; font-size: 12px; }
            QProgressBar { background: #e5e7eb; border-radius: 8px; height: 18px; text-align: center; font-weight: 800; }
            QProgressBar::chunk { background: #38bdf8; border-radius: 8px; }
        """)

    def reload_key_options(self, scale: str):
        current = self.cbo_key.currentText()
        self.cbo_key.blockSignals(True)
        self.cbo_key.clear()
        self.cbo_key.addItems(MINOR_KEYS if scale == "Minor" else MAJOR_KEYS)
        normalized = normalize_key_for_scale(current, scale)
        if normalized in [self.cbo_key.itemText(i) for i in range(self.cbo_key.count())]:
            self.cbo_key.setCurrentText(normalized)
        self.cbo_key.blockSignals(False)

    def run_test(self):
        if self.running:
            return

        url = self.edt_url.text().strip()
        if not url:
            no_sound_message(self, "Thiếu link", "Anh hãy dán link YouTube trước.")
            return

        self.running = True
        self.btn_run.setEnabled(False)
        self.progress.setValue(0)
        self.lbl_status.setText("Đang chạy...")
        self.txt_result.appendPlainText("\n" + "=" * 80)
        self.txt_result.appendPlainText(f"Bắt đầu test: {time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.txt_result.appendPlainText(f"URL: {url}")
        self.txt_result.appendPlainText(f"Tone gốc: {self.cbo_key.currentText()} {self.cbo_scale.currentText()}")

        args = {
            "youtube_url": url,
            "base_key": self.cbo_key.currentText(),
            "base_scale": self.cbo_scale.currentText(),
            "window_sec": int(self.spin_window.value()),
            "hop_sec": int(self.spin_hop.value()),
            "start_ratio": float(self.spin_start.value()) / 100.0,
        }

        def worker():
            try:
                result = analyze_end_modulation(
                    **args,
                    progress_cb=lambda pct, msg: self.status_signal.emit(int(pct), str(msg)),
                )
                self.result_signal.emit(result)
            except Exception as e:
                self.result_signal.emit({"ok": False, "error": str(e)})

        threading.Thread(target=worker, daemon=True).start()

    def on_status(self, pct: int, msg: str):
        self.progress.setValue(max(0, min(100, int(pct))))
        self.lbl_status.setText(msg)

    def on_result(self, result: object):
        self.running = False
        self.btn_run.setEnabled(True)
        self.progress.setValue(100)
        self.last_result = result if isinstance(result, dict) else {"ok": False, "error": str(result)}

        r = self.last_result
        if not r.get("ok"):
            self.lbl_status.setText("Có lỗi.")
            err = str(r.get("error", "Không rõ lỗi"))
            self.txt_result.appendPlainText("\nLỖI:")
            self.txt_result.appendPlainText(err)
            if "Sign in to confirm" in err or "not a bot" in err:
                self.txt_result.appendPlainText(
                    "\nGợi ý: YouTube đang yêu cầu đăng nhập/cookie. "
                    "Hãy mở Brave bằng nút KARAOKE, đăng nhập YouTube, mở video phát 5 giây rồi test lại."
                )
            return

        if r.get("has_modulation"):
            self.lbl_status.setText("Phát hiện có khả năng lên tone cuối bài.")
            self.txt_result.appendPlainText("\nKẾT QUẢ: CÓ KHẢ NĂNG LÊN TONE")
            self.txt_result.appendPlainText(f"Tên bài: {r.get('title', '')}")
            self.txt_result.appendPlainText(f"Thời lượng: {r.get('duration_text')}")
            self.txt_result.appendPlainText(f"Tone đầu: {r.get('base_label')}")
            self.txt_result.appendPlainText(f"Tone cuối: {r.get('new_label')}")
            self.txt_result.appendPlainText(f"Lên: +{r.get('semitone_up')} semitone")
            self.txt_result.appendPlainText(f"Thời điểm dự kiến: {r.get('raise_time_text')}")
            self.txt_result.appendPlainText(f"Tin cậy: {r.get('confidence')}")
            self.txt_result.appendPlainText(f"Thông báo: {r.get('message')}")
        else:
            self.lbl_status.setText("Chưa thấy dấu hiệu lên tone ổn định.")
            self.txt_result.appendPlainText("\nKẾT QUẢ: CHƯA PHÁT HIỆN LÊN TONE ỔN ĐỊNH")
            self.txt_result.appendPlainText(f"Tên bài: {r.get('title', '')}")
            self.txt_result.appendPlainText(f"Thời lượng: {r.get('duration_text')}")
            self.txt_result.appendPlainText(f"Tone đầu: {r.get('base_label')}")
            self.txt_result.appendPlainText(f"Thông báo: {r.get('message')}")

        self.txt_result.appendPlainText("\nTIMELINE PHÂN TÍCH:")
        for item in r.get("timeline", []):
            marker = " <= ứng viên lên tone" if item.get("candidate_up") else ""
            self.txt_result.appendPlainText(
                f"- {item.get('time')} | {item.get('label')} | conf={item.get('confidence')} | delta={item.get('delta')}{marker}"
            )

        self.txt_result.appendPlainText("=" * 80)

    def save_log(self):
        if not self.last_result:
            no_sound_message(self, "Chưa có dữ liệu", "Chưa có kết quả để lưu.")
            return

        default = Path.cwd() / "data" / f"end_modulation_test_{time.strftime('%Y%m%d_%H%M%S')}.json"
        default.parent.mkdir(parents=True, exist_ok=True)

        path, _ = QFileDialog.getSaveFileName(self, "Lưu log test", str(default), "JSON (*.json)")
        if not path:
            return

        payload = {
            "saved_at": time.strftime("%Y-%m-%d %H:%M:%S"),
            "url": self.edt_url.text().strip(),
            "base_key": self.cbo_key.currentText(),
            "base_scale": self.cbo_scale.currentText(),
            "options": {
                "window_sec": int(self.spin_window.value()),
                "hop_sec": int(self.spin_hop.value()),
                "start_percent": int(self.spin_start.value()),
            },
            "result": self.last_result,
            "text_log": self.txt_result.toPlainText(),
        }

        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)

        no_sound_message(self, "Đã lưu", f"Đã lưu log test:\n{path}")


def main():
    app = QApplication(sys.argv)
    w = EndModulationTestWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
