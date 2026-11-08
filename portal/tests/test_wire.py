import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import clock_pb2 as pb  # noqa: E402
import model  # noqa: E402
import wire  # noqa: E402
from model import Invalid  # noqa: E402


HEADER = os.path.join(os.path.dirname(__file__), "..", "..", "firmware", "BhoomiClock", "clock.pb.h")


def nanopb_size(message: str) -> int:
    """The worst-case encoded size nanopb computed for `message` (the generated header, at the sketch root)."""
    with open(HEADER) as f:
        return int(re.search(rf"#define bhoomi_clock_v1_{message}_size\s+(\d+)", f.read()).group(1))


def nanopb_array(struct: str, field: str) -> int:
    """The fixed array length nanopb gave `struct.field` (from clock.options)."""
    with open(HEADER) as f:
        text = f.read()
    body = re.search(rf"typedef struct _bhoomi_clock_v1_{struct} \{{(.*?)\}} bhoomi_clock_v1_{struct};", text, re.S).group(1)
    return int(re.search(rf"\b{field}\[(\d+)\]", body).group(1))


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
        self.assertLessEqual(len(msg.SerializeToString()), nanopb_size("DeviceState"))


class FacesInStateTests(unittest.TestCase):
    def test_refs_pinned_and_default(self):
        s = model.empty_state()
        s["faces"] = {"list": [{"id": "aaaaaaaaaaaa", "rev": "111111111111"}, {"id": "bbbbbbbbbbbb", "rev": "222222222222"}],
                      "default": "bbbbbbbbbbbb", "pinned": "aaaaaaaaaaaa"}
        msg = pb.DeviceState.FromString(wire.device_state(s).SerializeToString())
        self.assertEqual([(f.id, f.rev) for f in msg.faces], [("aaaaaaaaaaaa", "111111111111"), ("bbbbbbbbbbbb", "222222222222")])
        self.assertEqual((msg.pinned_face, msg.default_face), ("aaaaaaaaaaaa", "bbbbbbbbbbbb"))

    def test_no_faces_is_empty_and_small(self):
        msg = pb.DeviceState.FromString(wire.device_state(model.empty_state()).SerializeToString())
        self.assertEqual((len(msg.faces), msg.pinned_face, msg.default_face), (0, "", ""))

    def test_the_limits_match_the_nanopb_arrays(self):
        self.assertEqual(nanopb_array("DeviceState", "faces"), model.MAX_FACES)
        self.assertEqual(nanopb_array("Face", "layers"), model.MAX_LAYERS)
        self.assertEqual(nanopb_array("Face", "emitters"), model.MAX_EMITTERS)
        self.assertEqual(nanopb_array("Layer", "motion"), model.MAX_MOTIONS)
        self.assertEqual(nanopb_array("Motion", "keys"), model.MAX_FADE_KEYS)
        self.assertEqual(nanopb_array("Emitter", "sprites"), model.MAX_EMITTER_SPRITES)
        self.assertEqual(nanopb_array("When", "dates"), model.MAX_DATES)

    def test_a_full_face_list_fits(self):
        s = model.empty_state()
        s["faces"] = {"list": [{"id": model.new_id(), "rev": model.new_id()} for _ in range(model.MAX_FACES)],
                      "default": "f" * 12, "pinned": "f" * 12}
        s["message"] = {"text": model.clean_message("😀" * model.MESSAGE_MAX), "at": 1}
        s["alarms"] = [model.clean_alarm({"time": "23:59", "date": "2026-12-31", "label": "😀" * model.LABEL_MAX,
                                          "sound": "x" * model.SOUND_MAX}, model.new_id()) for _ in range(model.MAX_ALARMS)]
        self.assertLessEqual(len(wire.device_state(s).SerializeToString()), nanopb_size("DeviceState"))


def _assets():
    out = {}
    for n in range(1, 8):
        out["%016x" % n] = {"kind": "sprite", "w": 40, "h": 40, "bytes": 12 + 40 * 40 * 3}
    out["%016x" % 0] = {"kind": "bg", "w": 320, "h": 240, "bytes": 31877}
    return out


class FaceMessageTests(unittest.TestCase):
    A = _assets()
    BG, S1, S2, S3 = ("%016x" % n for n in (0, 1, 2, 3))

    def motion(self, **kw):
        return dict({"type": "bob", "period": 2000, "phase": 100, "amp": 4}, **kw)

    def face(self):
        slot = {"x": 160, "y": 20, "size": 64, "color": "#ffeedd", "shadow": "#102030", "align": "center"}
        fade = {"type": "fade", "period": 3000, "phase": 0, "keys": [{"at": 0, "v": 100}, {"at": 400, "v": 20}, {"at": 700, "v": 60}, {"at": 900, "v": 100}]}
        layer = {"name": "Panda", "sprite": self.S1, "x": 100, "y": 90, "copies": 3, "stagger": {"x": 40, "y": 0, "phase": 250},
                 "motion": [self.motion(), {"type": "path", "period": 5000, "phase": 0, "dx": -30, "dy": 10, "mode": "loop"}, fade]}
        data = {"name": "Test", "background": self.BG, "time": slot, "date": dict(slot, y=100, size=20, format="short", shadow=""),
                "icon": {"sprite": self.S2, "x": 10, "y": 10, "motion": [{"type": "pulse", "period": 1000, "phase": 0, "min": 90, "max": 110}],
                         "scatter": {"area": {"x": 0, "y": 0, "w": 100, "h": 80}, "seed": 12345}},
                "layers": [dict(layer, scale=60)],
                "particles": [{"kind": "rise", "sprites": [self.S3, self.S2], "count": 12, "area": {"x": 10, "y": 20, "w": 100, "h": 200},
                               "speed_min": 5, "speed_max": 30, "sway": 8, "period": 3000, "phase": 7, "depth": "back", "seed": 42}],
                "message_box": {"x": 0, "y": 200},
                "nowplaying": {"show": True, "x": 8, "y": 190, "w": 304, "bg": "#102030", "bg_pct": 40, "title": "#ffffff", "artist": "#ffb3c0"},
                "when": {"hours": {"from": 22, "to": 6}, "months": [1, 12], "weekdays": [0, 6],
                         "dates": [{"month": 2, "day": 14, "year": None}, {"month": 11, "day": 7, "year": 2026}]}}
        face = model.clean_face(data, self.A, "abcdefabcdef")
        face["rev"] = model.face_rev(face)
        return face

    def test_round_trip(self):
        face = self.face()
        msg = pb.Face.FromString(wire.face_message(face).SerializeToString())
        self.assertEqual((msg.id, msg.rev), ("abcdefabcdef", face["rev"]))
        self.assertEqual((msg.background.hash, msg.background.w, msg.background.h, msg.background.size), (self.BG, 320, 240, 31877))
        self.assertEqual((msg.time.x, msg.time.y, msg.time.size, msg.time.color, msg.time.has_shadow, msg.time.shadow, msg.time.align),
                         (160, 20, 64, 0xffeedd, True, 0x102030, pb.TextSlot.CENTER))
        self.assertEqual((msg.date.format, msg.date.has_shadow), (pb.TextSlot.SHORT, False))
        self.assertEqual(msg.icon.sprite.hash, self.S2)
        self.assertEqual(msg.icon.motion[0].type, pb.Motion.PULSE)
        self.assertEqual((msg.icon.motion[0].min_pct, msg.icon.motion[0].max_pct), (90, 110))
        layer = msg.layers[0]
        self.assertEqual((layer.sprite.hash, layer.sprite.w, layer.sprite.size, layer.x, layer.y, layer.copies),
                         (self.S1, 40, 12 + 4800, 100, 90, 3))
        self.assertEqual((layer.stagger_x, layer.stagger_y, layer.stagger_phase_ms), (40, 0, 250))
        self.assertEqual((layer.scale_pct, layer.scatter), (60, False))
        self.assertEqual((msg.icon.scale_pct, msg.icon.scatter, msg.icon.scatter_seed), (100, True, 12345))
        self.assertEqual((msg.icon.scatter_area.x, msg.icon.scatter_area.y, msg.icon.scatter_area.w, msg.icon.scatter_area.h), (0, 0, 100, 80))
        bob, path, fade = layer.motion
        self.assertEqual((bob.type, bob.period_ms, bob.phase_ms, bob.amp), (pb.Motion.BOB, 2000, 100, 4))
        self.assertEqual((path.type, path.dx, path.dy, path.loop), (pb.Motion.PATH, -30, 10, True))
        self.assertEqual([(k.at, k.pct) for k in fade.keys], [(0, 100), (400, 20), (700, 60), (900, 100)])
        e = msg.emitters[0]
        self.assertEqual((e.kind, e.count, e.front, e.seed, e.sway, e.period_ms, e.phase_ms), (pb.Emitter.RISE, 12, False, 42, 8, 3000, 7))
        self.assertEqual([s.hash for s in e.sprites], [self.S3, self.S2])
        self.assertEqual((e.area.x, e.area.y, e.area.w, e.area.h, e.speed_min, e.speed_max), (10, 20, 100, 200, 5, 30))
        self.assertTrue(msg.HasField("message_box"))
        self.assertEqual((msg.message_box.x, msg.message_box.y), (0, 200))
        np = msg.nowplaying
        self.assertEqual((np.show, np.x, np.y, np.w, np.has_bg, np.bg, np.bg_pct, np.title, np.artist), (True, 8, 190, 304, True, 0x102030, 40, 0xffffff, 0xffb3c0))
        w = msg.when
        self.assertEqual((w.hour_from, w.hour_to, w.months, w.days), (22, 6, 0b100000000001, 0b1000001))
        self.assertEqual([(d.year, d.month, d.day) for d in w.dates], [(0, 2, 14), (2026, 11, 7)])

    def test_unset_optionals(self):
        face = self.face()
        face["icon"], face["date"], face["particles"], face["message_box"] = None, None, [], None
        msg = pb.Face.FromString(wire.face_message(face).SerializeToString())
        self.assertFalse(msg.HasField("message_box"))
        self.assertFalse(msg.HasField("icon"))
        self.assertFalse(msg.HasField("date"))
        self.assertEqual(len(msg.emitters), 0)

    def test_blink_and_frames(self):
        face = self.face()
        face["layers"][0]["motion"] = [{"type": "blink", "period": 4000, "phase": 0, "on": 150},
                                       {"type": "frames", "period": 667, "phase": 0, "frames": 4, "fps": 6}]
        face["assets"][self.S1]["w"] = 160
        msg = pb.Face.FromString(wire.face_message(face).SerializeToString())
        blink, frames = msg.layers[0].motion
        self.assertEqual((blink.type, blink.on_ms), (pb.Motion.BLINK, 150))
        self.assertEqual((frames.type, frames.frames, frames.fps), (pb.Motion.FRAMES, 4, 6))

    def test_the_most_a_face_can_be_fits_the_nanopb_buffers(self):
        """Every limit at its maximum, so every field is as long as it gets."""
        assets = dict(self.A)
        spr = {"kind": "sprite", "w": 40, "h": 40, "bytes": 4812}
        slot = {"x": -1000, "y": 1000, "size": 96, "color": "#ffffff", "shadow": "#ffffff", "align": "right", "format": "numeric"}
        keys = [{"at": 0, "v": 100}, {"at": 300, "v": 20}, {"at": 600, "v": 60}, {"at": 1000, "v": 100}]
        motions = [{"type": "fade", "period": 60000, "phase": 59999, "keys": keys}] * 2 + \
                  [{"type": "path", "period": 60000, "phase": 59999, "dx": -320, "dy": -240, "mode": "loop"}]
        layer = {"name": "x" * 24, "sprite": self.S1, "x": -1000, "y": -1000, "copies": 12, "stagger": {"x": -320, "y": -240, "phase": -60000},
                 "motion": motions, "scale": 200,
                 "scatter": {"area": {"x": -1000, "y": -1000, "w": 1000, "h": 1000}, "seed": 2**32 - 1, "period": 60000}}
        emitter = {"kind": "fall", "sprites": [self.S1, self.S2, self.S3], "count": 40, "area": {"x": 1000, "y": 1000, "w": 1000, "h": 1000},
                   "speed_min": 400, "speed_max": 400, "sway": 60, "period": 60000, "phase": 59999, "depth": "front", "seed": 2**32 - 1}
        when = {"hours": {"from": 23, "to": 24}, "months": list(range(1, 13)), "weekdays": list(range(7)),
                "dates": [{"month": 12, "day": 30 - i, "year": 2100} for i in range(8)]}
        # clean_face's range checks are tighter than nanopb's, so build the clean dict directly (without the
        # on-screen checks, which are not what this test is about)
        base = model.clean_face({"name": "x", "background": self.BG, "time": dict(slot, x=0, y=0), "layers": [], "particles": []}, assets, "f" * 12)
        np = {"show": True, "x": -1000, "y": 1000, "w": 320, "bg": "#ffffff", "bg_pct": 100, "title": "#ffffff", "artist": "#ffffff"}
        face = dict(base, time=slot, date=slot, icon=layer, layers=[layer] * model.MAX_LAYERS, particles=[emitter] * model.MAX_EMITTERS,
                    when=when, nowplaying=np)
        face["assets"] = {self.BG: assets[self.BG], self.S1: spr, self.S2: spr, self.S3: spr}
        face["rev"] = "f" * 12
        face["weather"] = {kind: {"background": self.BG, "layers": [layer] * model.MAX_LAYERS,
                                 "particles": [emitter] * model.MAX_EMITTERS, "text_color": "#ffffff"}
                           for kind in ("cloudy", "rain")}
        data = wire.face_message(face).SerializeToString()
        self.assertLessEqual(len(data), nanopb_size("Face"))
        msg = pb.Face.FromString(data)
        self.assertEqual((len(msg.layers), len(msg.emitters), len(msg.layers[0].motion), len(msg.layers[0].motion[0].keys)), (8, 3, 3, 4))


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
