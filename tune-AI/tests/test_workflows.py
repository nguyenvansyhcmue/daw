"""Fast, isolated regression tests for controller workflow services."""

import os
import unittest

import numpy as np

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPixmap
from PyQt6.QtWidgets import QApplication, QWidget

from ui.analog_components import ModeCardButton
from core.services.end_modulation_workflow_service import EndModulationWorkflowService
from core.services.key_signature_service import KeySignatureService
from core.services.realtime_tone_service import RealtimeToneService
from core.services.tone_workflow_service import ToneWorkflowService


class FakeToneCache:
    def __init__(self, cached=None):
        self.cached = cached
        self.saved_results = []

    def extract_video_id(self, url):
        return "abcdefghijk" if url else ""

    def find_by_video_id(self, video_id):
        return self.cached

    def mark_used(self, video_id):
        return self.cached

    def save_detect_result(self, **kwargs):
        self.saved_results.append(kwargs)
        return {"saved": kwargs["result"]["label"]}

    def get_correction_hint_for_detected_label(self, *args, **kwargs):
        return None

    def save_correction_history(self, **kwargs):
        return {"saved": True}

    def find_end_modulation_by_url(self, url):
        return {"found": bool(url)}

    def save_end_modulation(self, **kwargs):
        return kwargs


class FakeSongService:
    def __init__(self, songs=None):
        self.songs = songs or []

    def list_songs(self):
        return self.songs


class FakeToneService:
    def detect_tone_from_youtube_url(self, url, verbose=False):
        return {"key": "C#", "key_index": 1, "scale": "Major", "confidence": 0.8}

    def detect_tone_fix_mode(self, *args, **kwargs):
        return {"key": "Db", "key_index": 1, "scale": "Minor", "confidence": 0.7}


class FakeToneApplication:
    def apply_detected(self, result):
        return {"method": "detected", "label": result["label"]}

    def apply_manual(self, key, scale):
        return {"method": "manual", "label": f"{key} {scale}"}


class FakeEndModulationService:
    def __init__(self):
        self.invalidated = False

    def analyze_end_modulation(self, **kwargs):
        return {"has_modulation": True, "new_key": "C#", "new_scale": "Minor"}

    def invalidate_cache(self):
        self.invalidated = True


class ToneWorkflowServiceTests(unittest.TestCase):
    def setUp(self):
        self.cache = FakeToneCache()
        self.workflow = ToneWorkflowService(
            FakeToneService(),
            self.cache,
            FakeSongService(),
            KeySignatureService(),
            FakeToneApplication(),
        )

    def test_detect_fix_and_confirm_normalize_key_spelling(self):
        detected = self.workflow.auto_resolve_for_youtube("https://youtu.be/abcdefghijk")
        self.assertEqual("Db Major", detected["label"])
        self.assertEqual("detected", detected["applied"]["method"])

        fixed = self.workflow.fix_for_youtube(
            "https://youtu.be/abcdefghijk", current_key="C#", current_scale="Major",
        )
        self.assertEqual("C# Minor", fixed["label"])
        self.assertEqual("Db Major", fixed["previous_label"])

        confirmed = self.workflow.confirm_cache(
            "https://youtu.be/abcdefghijk", "Demo", "Db", "Minor",
        )
        self.assertEqual("C# Minor", confirmed["label"])
        self.assertEqual("manual", confirmed["applied"]["method"])

    def test_saved_song_has_priority_over_tone_cache(self):
        workflow = ToneWorkflowService(
            FakeToneService(),
            FakeToneCache({"key": "C", "scale": "Minor"}),
            FakeSongService([{
                "youtube_video_id": "abcdefghijk", "title": "Official", "key": "Db", "scale": "Major",
            }]),
            KeySignatureService(),
            FakeToneApplication(),
        )
        cached = workflow.find_cached_tone_by_video_id("abcdefghijk")
        self.assertEqual("songs", cached["source"])
        self.assertEqual("Db Major", cached["label"])


class EndModulationWorkflowServiceTests(unittest.TestCase):
    def test_uses_cached_base_tone_and_normalizes_candidate(self):
        tone_workflow = ToneWorkflowService(
            FakeToneService(),
            FakeToneCache({"key": "C#", "scale": "Major", "confidence": 0.9}),
            FakeSongService(),
            KeySignatureService(),
            FakeToneApplication(),
        )
        cache = FakeToneCache()
        end_service = FakeEndModulationService()
        workflow = EndModulationWorkflowService(end_service, cache, tone_workflow, KeySignatureService())

        result = workflow.analyze("https://youtu.be/abcdefghijk")
        self.assertEqual("Db Major", result["base_label"])
        self.assertEqual("C# Minor", result["new_label"])

        saved = workflow.save("https://youtu.be/abcdefghijk", "Demo", result)
        self.assertTrue(saved["ok"])
        self.assertEqual("Db Major", saved["end_modulation"]["base_result"]["label"])


class RealtimeToneServiceTests(unittest.TestCase):
    def test_overloaded_capture_queue_discards_stale_audio(self):
        service = RealtimeToneService()
        for value in range(service.MAX_PENDING_AUDIO_CHUNKS):
            service._enqueue_latest_audio(np.array([value], dtype=np.float32))

        latest = np.array([999], dtype=np.float32)
        service._enqueue_latest_audio(latest)

        self.assertGreater(service.dropped_audio_chunks, 0)
        self.assertEqual(1, service.audio_queue.qsize())
        np.testing.assert_array_equal(latest, service.audio_queue.get_nowait())


class ModeCardButtonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_clips_the_card_scene_inside_its_own_bounds(self):
        host = QWidget()
        host.resize(90, 50)
        host.setStyleSheet("background: #000000;")
        button = ModeCardButton("", "TEST", "asset", None, host)
        button.setGeometry(30, 10, 30, 30)
        button._background = QPixmap(60, 30)
        button._background.fill(QColor("#ff0000"))

        host.show()
        self.app.processEvents()
        image = host.grab().toImage()
        host.close()

        self.assertEqual(QColor("#000000").rgb(), image.pixelColor(20, 25).rgb())
        painted = image.pixelColor(45, 25)
        self.assertGreater(painted.red(), 180)
        self.assertGreater(painted.red(), painted.green() * 3)
        self.assertGreater(painted.red(), painted.blue() * 3)
