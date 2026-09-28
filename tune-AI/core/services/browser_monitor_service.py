from __future__ import annotations

from urllib.parse import parse_qs, urlparse

import requests


class BrowserMonitorService:
    """
    Đọc URL YouTube từ Brave bên ngoài thông qua cổng remote debugging.
    Brave phải được mở với --remote-debugging-port=9222.
    """

    def __init__(self, debug_url: str = "http://127.0.0.1:9222/json"):
        self.debug_url = debug_url

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

            if (
                "youtube.com" in parsed.netloc
                or "youtube-nocookie.com" in parsed.netloc
                or "music.youtube.com" in parsed.netloc
            ):
                qs = parse_qs(parsed.query)

                if "v" in qs:
                    return qs["v"][0]

                parts = [p for p in parsed.path.split("/") if p]

                if "shorts" in parts:
                    idx = parts.index("shorts")
                    if idx + 1 < len(parts):
                        return parts[idx + 1]

                if "embed" in parts:
                    idx = parts.index("embed")
                    if idx + 1 < len(parts):
                        return parts[idx + 1]

        except Exception:
            return ""

        return ""

    def canonical_watch_url(self, video_id: str) -> str:
        video_id = (video_id or "").strip()
        if not video_id:
            return ""
        return f"https://www.youtube.com/watch?v={video_id}"

    def get_tabs(self) -> list[dict]:
        try:
            response = requests.get(self.debug_url, timeout=1.2)
            response.raise_for_status()
            data = response.json()
            if isinstance(data, list):
                return data
            return []
        except Exception:
            return []

    def get_current_youtube_video(self):
        tabs = self.get_tabs()

        youtube_candidates = []

        for tab in tabs:
            url = str(tab.get("url", "") or "")
            title = str(tab.get("title", "") or "")
            tab_type = str(tab.get("type", "") or "")

            if tab_type and tab_type != "page":
                continue

            video_id = self.extract_video_id(url)
            if not video_id:
                continue

            if (
                "youtube.com" not in url
                and "youtu.be" not in url
                and "music.youtube.com" not in url
            ):
                continue

            youtube_candidates.append({
                "video_id": video_id,
                "url": self.canonical_watch_url(video_id),
                "raw_url": url,
                "title": title,
            })

        if not youtube_candidates:
            return None

        return youtube_candidates[0]