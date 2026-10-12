"""Read YouTube playback state from a Chromium DevTools Protocol tab."""

import json


class YouTubePlaybackService:
    """Keep CDP/WebSocket details outside the application facade."""

    PLAYBACK_EXPRESSION = """
    (() => {
        const v = document.querySelector('video');
        if (!v) return {ok:false, error:'Không tìm thấy thẻ video YouTube'};
        return {
            ok: true,
            currentTime: Number(v.currentTime || 0),
            duration: Number(v.duration || 0),
            paused: Boolean(v.paused),
            ended: Boolean(v.ended),
            playbackRate: Number(v.playbackRate || 1),
            url: String(location.href || ''),
            title: String(document.title || '')
        };
    })()
    """

    def __init__(self, browser_monitor_service):
        self.browser_monitor_service = browser_monitor_service

    def get_state(self, video_id: str = "") -> dict:
        video_id = str(video_id or "").strip()
        try:
            tabs = self.browser_monitor_service.get_tabs()
        except Exception as error:
            return {"ok": False, "error": f"Không đọc được tab Brave: {error}"}

        target_tab, actual_video_id = self._find_target_tab(tabs, video_id)
        if not target_tab:
            return {"ok": False, "error": "Không tìm thấy tab YouTube phù hợp trong Brave."}

        websocket_url = str(target_tab.get("webSocketDebuggerUrl", "") or "").strip()
        if not websocket_url:
            return {"ok": False, "error": "Tab Brave không có webSocketDebuggerUrl."}

        try:
            import websocket  # type: ignore
        except Exception:
            return {
                "ok": False,
                "error": "Thiếu thư viện websocket-client để đọc thời gian YouTube thật.",
                "missing_dependency": "websocket-client",
            }

        socket = None
        try:
            socket = self._connect(websocket, websocket_url)
            socket.send(json.dumps({
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {
                    "expression": self.PLAYBACK_EXPRESSION,
                    "returnByValue": True,
                    "awaitPromise": True,
                },
            }))
            response = json.loads(socket.recv())
            value = (((response or {}).get("result") or {}).get("result") or {}).get("value")
            if not isinstance(value, dict):
                return {"ok": False, "error": "Không đọc được currentTime từ YouTube."}
            value["ok"] = bool(value.get("ok", False))
            value["actual_video_id"] = actual_video_id
            value["requested_video_id"] = video_id
            return value
        except Exception as error:
            return {"ok": False, "error": f"Lỗi đọc currentTime YouTube: {error}"}
        finally:
            if socket is not None:
                try:
                    socket.close()
                except Exception:
                    pass

    def _find_target_tab(self, tabs: list, requested_video_id: str):
        fallback = (None, "")
        for tab in tabs:
            try:
                url = str(tab.get("url", "") or "")
                tab_type = str(tab.get("type", "") or "")
                if tab_type and tab_type != "page":
                    continue
                video_id = self.browser_monitor_service.extract_video_id(url)
                if requested_video_id and video_id == requested_video_id:
                    return tab, video_id
                if video_id and fallback[0] is None:
                    fallback = (tab, video_id)
            except Exception:
                continue
        return fallback

    @staticmethod
    def _connect(websocket, websocket_url: str):
        try:
            return websocket.create_connection(websocket_url, timeout=1.5, origin="http://localhost")
        except Exception:
            return websocket.create_connection(websocket_url, timeout=1.5)
