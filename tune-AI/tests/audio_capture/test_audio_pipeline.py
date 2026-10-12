import unittest

import numpy as np

from core.audio_capture.audio_analysis import AudioAnalysis
from core.audio_capture.audio_mixer import AudioMixer
from core.audio_capture.models import AudioFrame, AudioSource


class AudioPipelineTests(unittest.TestCase):
    def frame(self, value, source, channels=2):
        samples = np.full((480, channels), value, dtype=np.float32)
        return AudioFrame(samples, source)

    def test_mixer_mixes_mono_and_stereo_as_stereo(self):
        result = AudioMixer().mix(
            self.frame(.2, AudioSource.MICROPHONE, channels=1),
            self.frame(.1, AudioSource.SYSTEM),
        )
        self.assertEqual(result.channels, 2)
        self.assertGreater(float(result.samples[0, 0]), .25)
        self.assertLess(float(result.samples[0, 0]), .31)

    def test_mixer_rejects_mismatched_sample_rates(self):
        mic = self.frame(.2, AudioSource.MICROPHONE)
        system = AudioFrame(np.ones((480, 2), dtype=np.float32), AudioSource.SYSTEM, sample_rate=44_100)
        with self.assertRaises(ValueError):
            AudioMixer().mix(mic, system)

    def test_analysis_returns_real_level_and_waveform(self):
        samples = np.zeros((480, 2), dtype=np.float32)
        samples[100:200] = .5
        snapshot = AudioAnalysis(waveform_points=24).analyse(AudioFrame(samples, AudioSource.SYSTEM))
        self.assertGreater(snapshot.peak_left, .49)
        self.assertGreater(snapshot.rms_left, .2)
        self.assertEqual(snapshot.waveform.shape, (24,))
        self.assertGreater(float(snapshot.waveform.max()), .49)


if __name__ == "__main__":
    unittest.main()
