import json
import uuid
from pathlib import Path


class SongService:
    def __init__(self):
        base_dir = Path(__file__).resolve().parents[2]
        self.data_dir = base_dir / "data"
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.db_path = self.data_dir / "songs.json"

        if not self.db_path.exists():
            self._write([])

    def _read(self):
        try:
            with open(self.db_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                if isinstance(data, list):
                    return data
                return []
        except Exception:
            return []

    def _write(self, songs):
        with open(self.db_path, "w", encoding="utf-8") as f:
            json.dump(songs, f, ensure_ascii=False, indent=2)

    def list_songs(self):
        return self._read()

    def search_songs(self, keyword: str):
        keyword = (keyword or "").strip().lower()
        if not keyword:
            return self.list_songs()

        result = []
        for song in self._read():
            haystack = " ".join([
                str(song.get("title", "")),
                str(song.get("youtube_url", "")),
                str(song.get("youtube_video_id", "")),
                str(song.get("key", "")),
                str(song.get("scale", "")),
                str(song.get("end_key", "")),
                str(song.get("end_scale", "")),
            ]).lower()

            if keyword in haystack:
                result.append(song)

        return result

    def get_song(self, song_id: str):
        for song in self._read():
            if str(song.get("id")) == str(song_id):
                return song
        return None

    def save_song(self, song_data: dict):
        songs = self._read()

        song_id = str(song_data.get("id", "")).strip()
        if not song_id:
            song_id = str(uuid.uuid4())

        clean = {
            "id": song_id,
            "title": str(song_data.get("title", "")).strip(),
            "youtube_url": str(song_data.get("youtube_url", "")).strip(),
            "youtube_video_id": str(song_data.get("youtube_video_id", "")).strip(),
            "key": str(song_data.get("key", "C")).strip(),
            "scale": str(song_data.get("scale", "Major")).strip(),
            "tone_label": str(song_data.get("tone_label", "")).strip(),
            "raise_time": str(song_data.get("raise_time", "00:00")).strip() or "00:00",
            "end_key": str(song_data.get("end_key", song_data.get("key", "C"))).strip(),
            "end_scale": str(song_data.get("end_scale", song_data.get("scale", "Major"))).strip(),
            "audio_file": str(song_data.get("audio_file", "")).strip(),
        }

        updated = False
        for i, song in enumerate(songs):
            if str(song.get("id")) == song_id:
                songs[i] = clean
                updated = True
                break

        if not updated:
            songs.append(clean)

        self._write(songs)
        return clean

    def delete_song(self, song_id: str):
        songs = self._read()
        songs = [s for s in songs if str(s.get("id")) != str(song_id)]
        self._write(songs)