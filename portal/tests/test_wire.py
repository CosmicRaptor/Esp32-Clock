import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import clock_pb2 as pb  # noqa: E402
import model  # noqa: E402
import wire  # noqa: E402
from model import Invalid  # noqa: E402


class DeviceStateTests(unittest.TestCase):
    def state(self):
        s = model.empty_state()
        s["version"] = 7
        s["message"] = {"text": "Hi Bhoomi ✨\nsecond line", "at": 1700000000}
        s["image"] = {"id": "abcdef012345", "at": 1700000001, "bytes": 31877}
        s["alarms"] = model.sort_alarms([
            model.clean_alarm({"time": "07:30", "days": [1, 2, 3, 4, 5], "label": "Wake up"}, "a1"),
            model.clean_alarm({"time": "06:15", "date": "2026-11-07", "sound": "chime.mp3"}, "a2"),
            model.clean_alarm({"time": "08:00", "days": [0], "enabled": False}, "a3"),
        ])
        return s

    def test_round_trip(self):
        data = wire.device_state(self.state()).SerializeToString()
        msg = pb.DeviceState.FromString(data)
        self.assertEqual(msg.version, 7)
        self.assertEqual(msg.note.text, "Hi Bhoomi ✨\nsecond line")
        self.assertEqual((msg.photo.id, msg.photo.size), ("abcdef012345", 31877))
        self.assertEqual([a.id for a in msg.alarms], ["a2", "a1"])  # sorted, disabled one left out
        once, weekdays = msg.alarms
        self.assertEqual((once.hour, once.minute, once.days), (6, 15, 0))
        self.assertEqual((once.date.year, once.date.month, once.date.day), (2026, 11, 7))
        self.assertEqual(once.sound, "chime.mp3")
        self.assertEqual(weekdays.days, 0b0111110)  # Mon-Fri = bits 1-5
        self.assertFalse(weekdays.HasField("date"))

    def test_empty_state(self):
        msg = pb.DeviceState.FromString(wire.device_state(model.empty_state()).SerializeToString())
        self.assertFalse(msg.HasField("note"))
        self.assertFalse(msg.HasField("photo"))
        self.assertEqual(len(msg.alarms), 0)

    def test_fits_nanopb_buffers(self):
        """Worst case the portal allows must fit the fixed sizes in proto/clock.options."""
        s = model.empty_state()
        s["message"] = {"text": model.clean_message("😀" * model.MESSAGE_MAX), "at": 1}
        s["alarms"] = [model.clean_alarm({"time": "23:59", "date": "2026-12-31", "label": "😀" * model.LABEL_MAX,
                                          "sound": "x" * model.SOUND_MAX}, model.new_id())
                       for _ in range(model.MAX_ALARMS)]
        msg = wire.device_state(s)
        self.assertLessEqual(len(msg.note.text.encode()), 480)
        for a in msg.alarms:
            self.assertLessEqual(len(a.label.encode()), 160)
            self.assertLessEqual(len(a.sound.encode()), 64)
            self.assertLessEqual(len(a.id), 12)
        self.assertLessEqual(len(msg.alarms), 20)
        self.assertLessEqual(len(msg.SerializeToString()), 6216)  # bhoomi_clock_v1_DeviceState_size


class NowPlayingInStateTests(unittest.TestCase):
    def test_always_present_and_off_without_spotify(self):
        msg = pb.DeviceState.FromString(wire.device_state(model.empty_state()).SerializeToString())
        self.assertTrue(msg.HasField("nowplaying"))
        self.assertEqual((msg.nowplaying.left.status, msg.nowplaying.right.status), (pb.Listener.OFF, pb.Listener.OFF))

    def test_carries_both_sides_next_to_the_portal_state(self):
        left = {"status": "playing", "title": "T", "artist": "A", "art": "a" * 40}
        right = {"status": "paused", "title": "U", "artist": "B", "art": ""}
        s = model.empty_state()
        s["message"] = {"text": "hi", "at": 1}
        msg = pb.DeviceState.FromString(wire.device_state(s, (left, right)).SerializeToString())
        self.assertEqual(msg.note.text, "hi")
        self.assertEqual((msg.nowplaying.left.title, msg.nowplaying.right.status), ("T", pb.Listener.PAUSED))


class SoundListTests(unittest.TestCase):
    def test_parse(self):
        body = pb.SoundList(names=["chime.mp3", "Birdsong.wav"]).SerializeToString()
        self.assertEqual(wire.parse_sound_list(body), ["Birdsong.wav", "chime.mp3"])

    def test_garbage(self):
        with self.assertRaises(Invalid):
            wire.parse_sound_list(b"\xff\xff\xff")


if __name__ == "__main__":
    unittest.main()
