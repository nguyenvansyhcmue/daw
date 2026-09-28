import time
from urllib.parse import parse_qs, urlparse, urlencode, urlunparse

from PyQt6.QtCore import QTimer, QUrl, pyqtSignal
from PyQt6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)
from PyQt6.QtWebEngineWidgets import QWebEngineView


class YouTubeBrowserDialog(QDialog):
    song_matched = pyqtSignal(dict)
    video_changed = pyqtSignal(str, str, str)        # video_id, title, url
    playback_state_changed = pyqtSignal(bool, float) # is_playing, current_time_sec

    def __init__(self, controller, parent=None):
        super().__init__(parent)
        self.controller = controller

        self.current_video_id = ""
        self.current_title = ""
        self.current_url = ""
        self.last_matched_song_id = None

        self.last_is_playing = False
        self.last_current_time = 0.0
        # Fix delay realtime apply — lưu thời điểm poll cuối cùng để
        # extrapolate currentTime giữa các poll (giảm lag 1s → ~50ms).
        self.last_poll_wallclock = 0.0

        self.pending_autoplay = False
        self.last_loaded_target_url = "https://www.youtube.com"
        self.last_info_poll_tick = 0

        self.setWindowTitle("KARAOKE - YouTube")
        self.resize(1220, 780)

        self._build_ui()
        self._wire_events()
        self.go_home()

        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self.poll_page_state)
        # Realtime fix: 1000ms → 250ms để cải thiện độ chính xác currentTime.
        # JS runJavaScript là async, không block UI. CPU cost ~negligible.
        self.poll_timer.start(250)

    # =========================================================
    # UI
    # =========================================================

    def _build_ui(self):
        self.setStyleSheet("""
            QDialog {
                background: #f4f6fb;
            }
            QLabel {
                color: #111827;
                font-size: 14px;
                background: transparent;
            }
            QLineEdit {
                background: white;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 8px;
                padding: 8px 10px;
                font-size: 14px;
            }
            QPushButton {
                background: #e9eef8;
                color: #111827;
                border: 1px solid #cfd8e3;
                border-radius: 8px;
                padding: 8px 12px;
                font-size: 13px;
                font-weight: 600;
            }
            QPushButton:hover {
                background: #dbe7ff;
            }
        """)

        layout = QVBoxLayout(self)

        top_bar = QHBoxLayout()
        self.btn_back = QPushButton("◀")
        self.btn_forward = QPushButton("▶")
        self.btn_reload = QPushButton("⟳")
        self.btn_home = QPushButton("HOME")
        self.edt_url = QLineEdit()
        self.edt_url.setPlaceholderText("Nhập link YouTube hoặc từ khóa rồi Enter...")
        self.btn_go = QPushButton("GO")

        top_bar.addWidget(self.btn_back)
        top_bar.addWidget(self.btn_forward)
        top_bar.addWidget(self.btn_reload)
        top_bar.addWidget(self.btn_home)
        top_bar.addWidget(self.edt_url, 1)
        top_bar.addWidget(self.btn_go)

        layout.addLayout(top_bar)

        self.lbl_info = QLabel("Đang mở YouTube...")
        layout.addWidget(self.lbl_info)

        self.web = QWebEngineView()
        layout.addWidget(self.web, 1)

    def _wire_events(self):
        self.btn_back.clicked.connect(self.web.back)
        self.btn_forward.clicked.connect(self.web.forward)
        self.btn_reload.clicked.connect(self.web.reload)
        self.btn_home.clicked.connect(self.go_home)
        self.btn_go.clicked.connect(self.on_go_clicked)
        self.edt_url.returnPressed.connect(self.on_go_clicked)

        self.web.urlChanged.connect(self.on_url_changed)
        self.web.titleChanged.connect(self.on_title_changed)
        self.web.loadFinished.connect(self.on_load_finished)

    # =========================================================
    # NAV
    # =========================================================

    def go_home(self):
        self.pending_autoplay = False
        self.last_loaded_target_url = "https://www.youtube.com"
        self.web.setUrl(QUrl(self.last_loaded_target_url))
        self.edt_url.setText(self.last_loaded_target_url)

    def load_video_by_id(self, video_id: str, autoplay: bool = True):
        video_id = (video_id or "").strip()
        if not video_id:
            return

        self.current_video_id = video_id
        self.pending_autoplay = autoplay

        # Dùng embed URL ổn định hơn watch URL trong QWebEngine
        self.last_loaded_target_url = self.embed_url(video_id, autoplay=autoplay)
        self.web.setUrl(QUrl(self.last_loaded_target_url))
        self.edt_url.setText(self.canonical_watch_url(video_id, autoplay=False))

    def load_video_by_url(self, url: str, autoplay: bool = True):
        url = (url or "").strip()
        if not url:
            return

        video_id = self.extract_video_id(url)
        if video_id:
            self.load_video_by_id(video_id, autoplay=autoplay)
            return

        # fallback nếu không tách được id
        self.pending_autoplay = autoplay
        self.last_loaded_target_url = url
        self.web.setUrl(QUrl(url))
        self.edt_url.setText(url)

    def on_go_clicked(self):
        text = self.edt_url.text().strip()
        if not text:
            return

        self.pending_autoplay = False

        if "youtube.com" in text or "youtu.be" in text or text.startswith("http"):
            self.load_video_by_url(text, autoplay=False)
            return

        query = text.replace(" ", "+")
        self.last_loaded_target_url = f"https://www.youtube.com/results?search_query={query}"
        self.web.setUrl(QUrl(self.last_loaded_target_url))

    # =========================================================
    # URL HELPERS
    # =========================================================

    def extract_video_id(self, text: str) -> str:
        text = (text or "").strip()
        if not text:
            return ""

        if len(text) == 11 and "/" not in text and "?" not in text:
            return text

        try:
            parsed = urlparse(text)

            if parsed.netloc in ("youtu.be", "www.youtu.be"):
                return parsed.path.strip("/")

            if "youtube.com" in parsed.netloc or "youtube-nocookie.com" in parsed.netloc:
                qs = parse_qs(parsed.query)
                if "v" in qs:
                    return qs["v"][0]

                path_parts = [p for p in parsed.path.split("/") if p]

                if "shorts" in path_parts:
                    idx = path_parts.index("shorts")
                    if idx + 1 < len(path_parts):
                        return path_parts[idx + 1]

                if "embed" in path_parts:
                    idx = path_parts.index("embed")
                    if idx + 1 < len(path_parts):
                        return path_parts[idx + 1]
        except Exception:
            return ""

        return ""

    def canonical_watch_url(self, video_id: str, autoplay: bool = False) -> str:
        base = f"https://www.youtube.com/watch?v={video_id}"
        return self.add_autoplay(base) if autoplay else base

    def embed_url(self, video_id: str, autoplay: bool = True) -> str:
        base = f"https://www.youtube.com/embed/{video_id}"
        params = {
            "autoplay": "1" if autoplay else "0",
            "rel": "0",
            "modestbranding": "1",
            "playsinline": "1",
        }
        return f"{base}?{urlencode(params)}"

    def add_autoplay(self, url: str) -> str:
        try:
            parsed = urlparse(url)
            qs = parse_qs(parsed.query)
            qs["autoplay"] = ["1"]
            new_query = urlencode(qs, doseq=True)
            return urlunparse((
                parsed.scheme,
                parsed.netloc,
                parsed.path,
                parsed.params,
                new_query,
                parsed.fragment,
            ))
        except Exception:
            return url

    # =========================================================
    # SONG MATCH
    # =========================================================

    def find_saved_song_by_video_id(self, video_id: str):
        video_id = (video_id or "").strip()
        if not video_id:
            return None

        songs = self.controller.list_songs()

        for song in songs:
            saved_id = str(song.get("youtube_video_id", "")).strip()
            if saved_id and saved_id == video_id:
                return song

            youtube_url = str(song.get("youtube_url", "")).strip()
            if youtube_url:
                parsed_id = self.extract_video_id(youtube_url)
                if parsed_id == video_id:
                    return song

        return None

    def try_match_song(self):
        song = self.find_saved_song_by_video_id(self.current_video_id)
        if not song:
            self.last_matched_song_id = None
            return

        song_id = str(song.get("id"))
        if song_id == self.last_matched_song_id:
            return

        self.last_matched_song_id = song_id
        self.song_matched.emit(song)

    # =========================================================
    # PAGE EVENTS
    # =========================================================

    def on_url_changed(self, qurl):
        url = qurl.toString()
        self.current_url = url

        video_id = self.extract_video_id(url)
        if video_id:
            self.current_video_id = video_id
            self.edt_url.setText(self.canonical_watch_url(video_id, autoplay=False))
            self.try_match_song()
        else:
            self.edt_url.setText(url)

        self.video_changed.emit(self.current_video_id, self.current_title, self.current_url)

    def on_title_changed(self, title: str):
        self.current_title = title or ""
        self.update_info_label()
        self.video_changed.emit(self.current_video_id, self.current_title, self.current_url)

    def on_load_finished(self, ok: bool):
        if not ok:
            return

        # Không autoplay lại bằng JS nữa vì dễ reset player
        self.poll_page_state()

    # =========================================================
    # POLLING
    # =========================================================

    def poll_page_state(self):
        # Realtime fix: poll_timer giảm 1000→250ms cho currentTime.
        # URL/TITLE giữ tần suất cũ ~3s = 12 ticks @ 250ms để không spam JS.
        self.last_info_poll_tick += 1
        if self.last_info_poll_tick >= 12:
            self.last_info_poll_tick = 0
            self.web.page().runJavaScript("window.location.href;", self._js_url_callback)
            self.web.page().runJavaScript("document.title;", self._js_title_callback)

        # Skip ads cơ bản
        self._skip_ads_basic()

        # Chỉ đọc trạng thái player
        state_js = """
        (function() {
            try {
                let v = document.querySelector('video');
                if (!v) {
                    return {"is_playing": false, "current_time": 0.0};
                }
                let isPlaying = !v.paused && !v.ended && v.readyState > 2;
                return {
                    "is_playing": isPlaying,
                    "current_time": v.currentTime || 0.0
                };
            } catch(e) {
                return {"is_playing": false, "current_time": 0.0};
            }
        })();
        """
        self.web.page().runJavaScript(state_js, self._js_playback_callback)

    def _skip_ads_basic(self):
        skip_js = """
        (function() {
            try {
                let skipBtn =
                    document.querySelector('.ytp-ad-skip-button') ||
                    document.querySelector('.ytp-ad-skip-button-modern') ||
                    document.querySelector('.videoAdUiSkipButton');

                if (skipBtn) {
                    skipBtn.click();
                }

                let v = document.querySelector('video');
                let adShowing = document.querySelector('.ad-showing');

                if (v && adShowing && v.duration && isFinite(v.duration)) {
                    if (v.currentTime < v.duration - 0.5) {
                        v.currentTime = v.duration - 0.2;
                    }
                }
            } catch(e) {}
        })();
        """
        self.web.page().runJavaScript(skip_js)

    def _js_url_callback(self, value):
        if isinstance(value, str) and value.strip():
            self.current_url = value.strip()

            video_id = self.extract_video_id(self.current_url)
            if video_id:
                self.current_video_id = video_id
                self.edt_url.setText(self.canonical_watch_url(video_id, autoplay=False))
                self.try_match_song()

            self.update_info_label()
            self.video_changed.emit(self.current_video_id, self.current_title, self.current_url)

    def _js_title_callback(self, value):
        if isinstance(value, str):
            self.current_title = value.strip()
            self.update_info_label()
            self.video_changed.emit(self.current_video_id, self.current_title, self.current_url)

    def _js_playback_callback(self, value):
        try:
            is_playing = bool(value.get("is_playing", False))
            current_time = float(value.get("current_time", 0.0))
        except Exception:
            is_playing = False
            current_time = 0.0

        self.last_is_playing = is_playing
        self.last_current_time = current_time
        # Lưu wallclock để main_window có thể extrapolate currentTime giữa các poll
        self.last_poll_wallclock = time.time()
        self.playback_state_changed.emit(is_playing, current_time)

    # =========================================================
    # INFO
    # =========================================================

    def update_info_label(self):
        self.lbl_info.setText(
            f"Video ID: {self.current_video_id or '(chưa có)'} | "
            f"Tiêu đề: {self.current_title[:100]}"
        )