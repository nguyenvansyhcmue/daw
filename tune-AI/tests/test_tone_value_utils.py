import unittest

from ui.tone_value_utils import (
    coerce_tune_value,
    key_for_pitch_class,
    parse_mmss,
    pitch_class_for_key,
    seconds_to_mmss,
)


class ToneValueUtilsTests(unittest.TestCase):
    def test_time_formats_preserve_legacy_forms(self):
        self.assertEqual(seconds_to_mmss(125), "02:05")
        self.assertEqual(parse_mmss("02:05"), 125)
        self.assertEqual(parse_mmss("01:02:03", allow_hours=True), 3723)
        self.assertEqual(parse_mmss("bad", allow_hours=True), 0)

    def test_pitch_mapping_is_scale_aware(self):
        major = {0: "C", 1: "Db"}
        minor = {0: "C", 1: "C#"}
        mapping = {"C": 0, "Db": 1}
        self.assertEqual(pitch_class_for_key(" Db ", mapping), 1)
        self.assertEqual(key_for_pitch_class(1, "Major", major, minor), "Db")
        self.assertEqual(key_for_pitch_class(1, "Minor", major, minor), "C#")

    def test_tune_value_accepts_legacy_and_signed_values(self):
        self.assertEqual(coerce_tune_value(50), 50)
        self.assertEqual(coerce_tune_value(75), 25)
        self.assertEqual(coerce_tune_value(-75), -50)


if __name__ == "__main__":
    unittest.main()
