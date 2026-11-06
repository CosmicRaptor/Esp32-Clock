import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import auth  # noqa: E402
import model  # noqa: E402
from model import Invalid  # noqa: E402


def jpeg(width, height, sof=0xC0):
    """A minimal JPEG header: SOI, an APP0 segment, then a frame header."""
    app0 = b"\xff\xe0" + (16).to_bytes(2, "big") + b"JFIF\x00" + bytes(9)
    sof_seg = bytes([0xFF, sof]) + (17).to_bytes(2, "big") + b"\x08" + height.to_bytes(2, "big") + width.to_bytes(2, "big") + bytes(10)
    return b"\xff\xd8" + app0 + sof_seg + b"\xff\xda\x00\x02" + b"\xff\xd9"


class MessageTests(unittest.TestCase):
    def test_trims_and_collapses(self):
        self.assertEqual(model.clean_message("  hi\r\n\r\n\nthere \t"), "hi\nthere")

    def test_drops_control_chars(self):
        self.assertEqual(model.clean_message("a\x00b\x07c"), "abc")

    def test_limits(self):
        model.clean_message("x" * model.MESSAGE_MAX)
        with self.assertRaises(Invalid):
            model.clean_message("x" * (model.MESSAGE_MAX + 1))
        with self.assertRaises(Invalid):
            model.clean_message("a\nb\nc\nd\ne")
        with self.assertRaises(Invalid):
            model.clean_message(None)


class AlarmTests(unittest.TestCase):
    def test_repeating(self):
        a = model.clean_alarm({"time": "07:30", "days": [5, 1, 1], "label": "  wake   up ", "date": "ignored"}, "id1")
        self.assertEqual(a, {"id": "id1", "time": "07:30", "days": [1, 5], "date": "", "label": "wake up",
                             "sound": "", "enabled": True})

    def test_once_needs_date(self):
        with self.assertRaises(Invalid):
            model.clean_alarm({"time": "07:30", "days": []}, "x")
        with self.assertRaises(Invalid):
            model.clean_alarm({"time": "07:30", "date": "20261106"}, "x")
        a = model.clean_alarm({"time": "23:59", "date": "2026-11-06"}, "x")
        self.assertEqual(a["date"], "2026-11-06")

    def test_bad_values(self):
        for bad in ({"time": "24:00", "days": [1]}, {"time": "7:30", "days": [1]}, {"time": "07:30", "days": [7]},
                    {"time": "07:30", "days": [True]}, {"time": "07:30", "days": [1], "sound": "../x"},
                    {"time": "07:30", "days": [1], "enabled": "yes"}, {"time": "07:30", "days": [1], "label": "x" * 41}):
            with self.assertRaises(Invalid, msg=bad):
                model.clean_alarm(bad, "x")

    def test_sound_names(self):
        model.clean_alarm({"time": "07:00", "days": [1], "sound": "chime.mp3"}, "x")
        with self.assertRaises(Invalid):  # 64 bytes max, counted in UTF-8 (nanopb buffer on the clock)
            model.clean_alarm({"time": "07:00", "days": [1], "sound": "é" * 33}, "x")
        self.assertEqual(model.clean_sounds(["b.mp3", "A.wav", "b.mp3"]), ["A.wav", "b.mp3"])
        for bad in ([""], ["a/b.mp3"], ["x\n.mp3"], "chime.mp3", ["x"] * 101):
            with self.assertRaises(Invalid, msg=bad):
                model.clean_sounds(bad)


class JpegTests(unittest.TestCase):
    def test_reads_size(self):
        self.assertEqual(model.jpeg_info(jpeg(320, 240)), (320, 240, False))
        self.assertEqual(model.jpeg_info(jpeg(10, 20, 0xC2)), (10, 20, True))

    def test_clock_rules(self):
        model.check_clock_photo(jpeg(320, 240))
        with self.assertRaises(Invalid):
            model.check_clock_photo(jpeg(640, 480))
        with self.assertRaises(Invalid):
            model.check_clock_photo(jpeg(320, 240, 0xC2))
        with self.assertRaises(Invalid):
            model.check_clock_photo(b"\x89PNG\r\n\x1a\n" + bytes(100))
        with self.assertRaises(Invalid):
            model.check_clock_photo(b"\xff\xd8\xff\xe0\x00")


class AuthTests(unittest.TestCase):
    def test_session_roundtrip(self):
        t = auth.make_session("pw", now=1000)
        self.assertTrue(auth.check_session("pw", t, now=1001))
        self.assertFalse(auth.check_session("other", t, now=1001))
        self.assertFalse(auth.check_session("pw", t, now=1000 + auth.SESSION_DAYS * 86400 + 1))
        self.assertFalse(auth.check_session("pw", t.replace("v1.", "v1.9"), now=1001))
        self.assertFalse(auth.check_session("", t, now=1001))

    def test_helpers(self):
        self.assertEqual(auth.read_cookie("a=1; clock_session=xyz; b=2"), "xyz")
        self.assertIsNone(auth.read_cookie(None))
        self.assertEqual(auth.bearer("Bearer abc "), "abc")
        self.assertIsNone(auth.bearer("Basic abc"))
        self.assertFalse(auth.same_secret("", ""))
        self.assertTrue(auth.same_secret("abc", "abc"))


if __name__ == "__main__":
    unittest.main()
