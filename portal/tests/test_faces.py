import copy
import datetime
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import model  # noqa: E402
from model import Invalid  # noqa: E402

VECTORS = os.path.join(os.path.dirname(__file__), "vectors", "face_rules.json")

BG, PANDA, SNOW, HEART, SHEET, BIG, STAR, ICON = ("%016x" % n for n in range(1, 9))
ASSETS = {
    BG: {"kind": "bg", "w": 320, "h": 240, "bytes": 30000},
    PANDA: {"kind": "sprite", "w": 120, "h": 100, "bytes": 12 + 120 * 100 * 3},
    SNOW: {"kind": "sprite", "w": 10, "h": 10, "bytes": 12 + 300},
    STAR: {"kind": "sprite", "w": 8, "h": 8, "bytes": 12 + 192},
    HEART: {"kind": "sprite", "w": 24, "h": 22, "bytes": 12 + 24 * 22 * 3},
    SHEET: {"kind": "sprite", "w": 320, "h": 80, "bytes": 12 + 320 * 80 * 3},   # 4 frames of 80x80
    BIG: {"kind": "sprite", "w": 200, "h": 100, "bytes": 12 + 200 * 100 * 3},
    ICON: {"kind": "sprite", "w": 32, "h": 32, "bytes": 12 + 32 * 32 * 3},
}


def slot(**kw):
    s = {"x": 160, "y": 20, "size": 48, "color": "#ffffff", "shadow": "#000000", "align": "center"}
    s.update(kw)
    return s


def layer(**kw):
    s = {"name": "Panda", "sprite": PANDA, "x": 100, "y": 100, "copies": 1, "stagger": {"x": 0, "y": 0, "phase": 0}, "motion": []}
    s.update(kw)
    return s


def emitter(**kw):
    s = {"kind": "fall", "sprites": [SNOW], "count": 20, "area": {"x": 0, "y": 0, "w": 320, "h": 240},
         "speed_min": 15, "speed_max": 40, "sway": 12, "period": 4000, "phase": 0, "depth": "front", "seed": 7}
    s.update(kw)
    return s


def face(**kw):
    f = {"name": "Snowy panda", "background": BG, "time": slot(), "date": slot(y=90, size=18, format="long"),
         "icon": None, "layers": [layer()], "particles": [emitter()],
         "when": {"hours": None, "months": [12], "weekdays": [], "dates": []}}
    f.update(kw)
    return f


def clean(**kw):
    return model.clean_face(face(**kw), ASSETS, "a1b2c3d4e5f6")


def report(**kw):
    return model.face_report(clean(**kw))


def sprite_bytes(w, h):
    data = bytearray([0x19, 0x14, 0, 0]) + w.to_bytes(2, "little") + h.to_bytes(2, "little") + (2 * w).to_bytes(2, "little") + b"\0\0"
    return bytes(data + bytes(h * 2 * w + h * w))


class AssetFileTests(unittest.TestCase):
    def test_sprite_header(self):
        self.assertEqual(model.check_sprite_file(sprite_bytes(24, 22)), (24, 22))

    def test_sprite_wrong_length(self):
        with self.assertRaises(Invalid):
            model.check_sprite_file(sprite_bytes(24, 22)[:-1])

    def test_sprite_wrong_format(self):
        data = bytearray(sprite_bytes(8, 8))
        data[1] = 0x12  # RGB565, no alpha plane
        with self.assertRaises(Invalid):
            model.check_sprite_file(bytes(data))
        with self.assertRaises(Invalid):
            model.check_sprite_file(b"not a sprite")

    def test_sprite_stride_must_be_two_bytes_a_pixel(self):
        data = bytearray(sprite_bytes(8, 8))
        data[8:10] = (20).to_bytes(2, "little")
        with self.assertRaises(Invalid):
            model.check_sprite_file(bytes(data))

    def test_sprite_size_caps(self):
        model.check_sprite_file(sprite_bytes(160, 160))
        model.check_sprite_file(sprite_bytes(640, 80))   # a sheet
        with self.assertRaises(Invalid):
            model.check_sprite_file(sprite_bytes(161, 161))   # too many pixels
        with self.assertRaises(Invalid):
            model.check_sprite_file(sprite_bytes(100, 161))   # too tall
        with self.assertRaises(Invalid):
            model.check_sprite_file(sprite_bytes(1281, 8))    # too wide

    def test_png_header(self):
        png = b"\x89PNG\r\n\x1a\n" + (13).to_bytes(4, "big") + b"IHDR" + (24).to_bytes(4, "big") + (22).to_bytes(4, "big") + b"\x08\x06\x00\x00\x00" + bytes(8)
        self.assertEqual(model.png_size(png), (24, 22))
        with self.assertRaises(Invalid):
            model.png_size(b"GIF89a" + bytes(40))

    def test_hash(self):
        h = model.asset_hash(b"hello")
        self.assertTrue(model.valid_hash(h))
        self.assertEqual(h, model.asset_hash(b"hello"))
        self.assertNotEqual(h, model.asset_hash(b"hello!"))
        self.assertFalse(model.valid_hash("ABCDEF0123456789"))
        self.assertFalse(model.valid_hash(None))


class CleanFaceTests(unittest.TestCase):
    def test_a_good_face(self):
        f = clean()
        self.assertEqual(f["id"], "a1b2c3d4e5f6")
        self.assertEqual(sorted(f["assets"]), sorted([BG, PANDA, SNOW]))
        self.assertEqual(f["assets"][PANDA]["w"], 120)
        self.assertEqual(model.face_report(f)["errors"], [])

    def test_rev_changes_with_the_face(self):
        a, b = clean(), clean(name="Other")
        self.assertNotEqual(model.face_rev(a), model.face_rev(b))
        self.assertEqual(model.face_rev(a), model.face_rev(copy.deepcopy(a)))
        self.assertEqual(len(model.face_rev(a)), 12)
        # the rev doesn't depend on a previous rev
        a["rev"] = "x"
        self.assertEqual(model.face_rev(a), model.face_rev(clean()))

    def test_unknown_keys_are_dropped(self):
        f = clean(layers=[dict(layer(), sneaky="x")])
        self.assertNotIn("sneaky", f["layers"][0])

    def test_name(self):
        with self.assertRaises(Invalid):
            clean(name="  ")
        with self.assertRaises(Invalid):
            clean(name="x" * 31)
        self.assertEqual(clean(name="  Snowy   panda ")["name"], "Snowy panda")

    def test_background_must_exist_and_be_a_background(self):
        with self.assertRaises(Invalid):
            clean(background="f" * 16)
        with self.assertRaises(Invalid):
            clean(background=PANDA)  # a sprite, not a background

    def test_sprite_must_exist(self):
        with self.assertRaises(Invalid):
            clean(layers=[layer(sprite="e" * 16)])
        with self.assertRaises(Invalid):
            clean(layers=[layer(sprite=BG)])

    def test_colors(self):
        self.assertEqual(clean(time=slot(color="#FFAABB"))["time"]["color"], "#ffaabb")
        for bad in ("white", "#fff", "#gggggg", 5, None):
            with self.assertRaises(Invalid):
                clean(time=slot(color=bad))
        self.assertEqual(clean(time=slot(shadow=""))["time"]["shadow"], "")

    def test_text_slot_limits(self):
        for bad in (slot(size=9), slot(size=97), slot(align="middle"), slot(x=1.5), slot(x=True), slot(x=2000)):
            with self.assertRaises(Invalid):
                clean(time=bad)
        with self.assertRaises(Invalid):
            clean(date=slot(format="iso"))

    def test_date_can_be_left_out(self):
        self.assertIsNone(clean(date=None)["date"])

    def test_layer_limits(self):
        with self.assertRaises(Invalid):
            clean(layers=[layer() for _ in range(model.MAX_LAYERS + 1)])
        with self.assertRaises(Invalid):
            clean(layers=[layer(copies=model.MAX_COPIES + 1)])
        with self.assertRaises(Invalid):
            clean(layers=[layer(sprite=BIG)])   # 200 wide

    def test_icon_is_small(self):
        icon = layer(sprite=ICON, x=10, y=10)
        self.assertEqual(clean(icon=icon)["icon"]["sprite"], ICON)
        with self.assertRaises(Invalid):
            clean(icon=layer(sprite=PANDA))     # 120x100

    def test_a_sprite_sheet_is_measured_per_frame(self):
        sheet = layer(sprite=SHEET, motion=[{"type": "frames", "frames": 4, "fps": 6}])
        f = clean(layers=[sheet])
        self.assertEqual(model.layer_frame(f["layers"][0], f["assets"]), (80, 80))
        with self.assertRaises(Invalid):
            clean(layers=[layer(sprite=SHEET)])     # 320 wide without frames
        with self.assertRaises(Invalid):
            clean(layers=[dict(sheet, motion=[{"type": "frames", "frames": 3, "fps": 6}])])   # 320 % 3 != 0

    def test_particle_limits(self):
        with self.assertRaises(Invalid):
            clean(particles=[emitter() for _ in range(model.MAX_EMITTERS + 1)])
        with self.assertRaises(Invalid):
            clean(particles=[emitter(count=41)])
        with self.assertRaises(Invalid):
            clean(particles=[emitter(sprites=[])])
        with self.assertRaises(Invalid):
            clean(particles=[emitter(sprites=[SNOW] * 4)])
        with self.assertRaises(Invalid):
            clean(particles=[emitter(sprites=[PANDA])])    # too big for a particle
        with self.assertRaises(Invalid):
            clean(particles=[emitter(speed_min=50, speed_max=10)])
        with self.assertRaises(Invalid):
            clean(particles=[emitter(kind="spin")])

    def test_motion_limits(self):
        bob = {"type": "bob", "period": 3000, "phase": 0, "amp": 4}
        with self.assertRaises(Invalid):
            clean(layers=[layer(motion=[bob] * (model.MAX_MOTIONS + 1))])
        for bad in ({"type": "rotate", "period": 3000, "amp": 4},     # no rotation
                    dict(bob, period=100), dict(bob, period=70000), dict(bob, amp=81), {"type": "bob", "period": 3000},
                    {"type": "blink", "period": 3000, "on": 3000}, {"type": "pulse", "period": 3000, "min": 100, "max": 100},
                    {"type": "pulse", "period": 3000, "min": 20, "max": 100},
                    {"type": "fade", "period": 3000, "keys": [{"at": 0, "v": 100}]},
                    {"type": "fade", "period": 3000, "keys": [{"at": 100, "v": 100}, {"at": 500, "v": 0}]},      # first not at 0
                    {"type": "fade", "period": 3000, "keys": [{"at": 0, "v": 100}, {"at": 0, "v": 0}]},          # not increasing
                    {"type": "fade", "period": 3000, "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 101}]}):
            with self.assertRaises(Invalid, msg=bad):
                clean(layers=[layer(motion=[bad])])

    def test_phase_wraps_into_the_period(self):
        f = clean(layers=[layer(motion=[{"type": "bob", "period": 2000, "phase": 2500, "amp": 3}])])
        self.assertEqual(f["layers"][0]["motion"][0]["phase"], 500)

    def test_frames_period_is_derived(self):
        f = clean(layers=[layer(sprite=SHEET, motion=[{"type": "frames", "frames": 4, "fps": 8, "period": 1}])])
        self.assertEqual(f["layers"][0]["motion"][0]["period"], 500)

    def test_when(self):
        w = lambda **kw: clean(when=dict({"hours": None, "months": [], "weekdays": [], "dates": []}, **kw))["when"]
        self.assertEqual(w(months=[12, 1, 12])["months"], [1, 12])
        self.assertEqual(w(hours={"from": 22, "to": 6})["hours"], {"from": 22, "to": 6})
        self.assertEqual(w(dates=[{"month": 2, "day": 29, "year": None}])["dates"][0]["day"], 29)   # yearly Feb 29 is fine
        for bad in ({"hours": {"from": 5, "to": 5}}, {"hours": {"from": 24, "to": 6}}, {"hours": {"from": 5, "to": 0}},
                    {"months": [13]}, {"weekdays": [7]}, {"dates": [{"month": 2, "day": 30}]},
                    {"dates": [{"month": 2, "day": 29, "year": 2025}]}, {"dates": [{"month": 1, "day": 1}] * 9}):
            with self.assertRaises(Invalid, msg=bad):
                w(**bad)
        self.assertEqual(clean(when=None)["when"], {"hours": None, "months": [], "weekdays": [], "dates": []})


class ReportTests(unittest.TestCase):
    def errors(self, **kw):
        return report(**kw)["errors"]

    def test_everything_must_stay_on_screen(self):
        self.assertEqual(self.errors(layers=[layer(x=200, y=140)]), [])            # 120x100 ends at exactly 320x240
        self.assertTrue(self.errors(layers=[layer(x=201, y=140)]))
        self.assertTrue(self.errors(layers=[layer(x=-1)]))
        self.assertTrue(self.errors(layers=[layer(y=141)]))

    def test_motion_counts_towards_the_edge(self):
        bob = {"type": "bob", "period": 3000, "phase": 0, "amp": 10}
        self.assertEqual(self.errors(layers=[layer(y=10, motion=[bob])]), [])
        self.assertTrue(self.errors(layers=[layer(y=9, motion=[bob])]))
        path = {"type": "path", "period": 3000, "dx": 50, "dy": 0, "mode": "pingpong"}
        self.assertEqual(self.errors(layers=[layer(x=150, motion=[path])]), [])      # reaches 320
        self.assertTrue(self.errors(layers=[layer(x=151, motion=[path])]))
        back = dict(path, dx=-50)
        self.assertTrue(self.errors(layers=[layer(x=49, motion=[back])]))

    def test_stacked_motions_add_up(self):
        bob = {"type": "bob", "period": 3000, "phase": 0, "amp": 6}
        self.assertTrue(self.errors(layers=[layer(y=10, motion=[bob, bob])]))        # 12 > 10
        self.assertEqual(self.errors(layers=[layer(y=12, motion=[bob, bob])]), [])

    def test_pulse_grows_about_the_centre(self):
        pulse = {"type": "pulse", "period": 3000, "phase": 0, "min": 100, "max": 150}
        # a 24x22 heart at 150% grows 6 px left and right and 5.5 px up and down
        self.assertEqual(self.errors(layers=[layer(sprite=HEART, x=6, y=6, motion=[pulse])], particles=[]), [])
        self.assertTrue(self.errors(layers=[layer(sprite=HEART, x=5, y=6, motion=[pulse])], particles=[]))
        self.assertTrue(self.errors(layers=[layer(sprite=HEART, x=6, y=5, motion=[pulse])], particles=[]))

    def test_copies_are_checked_one_by_one(self):
        row = layer(sprite=HEART, x=10, y=10, copies=5, stagger={"x": 30, "y": 0, "phase": 100})
        self.assertEqual(self.errors(layers=[row]), [])                              # last ends at x = 10 + 120 + 24
        self.assertTrue(self.errors(layers=[dict(row, stagger={"x": 80, "y": 0, "phase": 0})]))   # 10 + 320 + 24
        self.assertTrue(self.errors(layers=[dict(row, stagger={"x": -30, "y": 0, "phase": 0})]))  # runs off the left

    def test_frames_use_the_cell_not_the_sheet(self):
        sheet = layer(sprite=SHEET, x=240, y=160, motion=[{"type": "frames", "frames": 4, "fps": 6}])
        self.assertEqual(self.errors(layers=[sheet]), [])                             # 80x80 ends at 320x240
        self.assertTrue(self.errors(layers=[dict(sheet, x=241)]))

    def test_particle_area_must_be_on_screen(self):
        self.assertEqual(self.errors(particles=[emitter(area={"x": 0, "y": 0, "w": 320, "h": 240})]), [])
        self.assertTrue(self.errors(particles=[emitter(area={"x": 10, "y": 0, "w": 320, "h": 240})]))
        self.assertTrue(self.errors(particles=[emitter(area={"x": -5, "y": 0, "w": 100, "h": 100})]))

    def test_text_must_be_on_screen(self):
        self.assertTrue(self.errors(time=slot(x=0, align="center")))                  # half of it left of the screen
        self.assertTrue(self.errors(time=slot(x=319, size=96, align="left")))
        self.assertTrue(self.errors(time=slot(y=230)))                                # below the bottom
        self.assertTrue(self.errors(date=slot(x=160, y=90, size=40, format="long")))  # "Wednesday, 30 September" at 40 px
        self.assertEqual(self.errors(time=slot(x=160, y=20, size=64), date=slot(x=160, y=100, size=18, format="long")), [])

    def test_text_alignment_moves_the_box(self):
        right = slot(x=315, y=10, size=40, align="right", shadow="")
        self.assertEqual(self.errors(time=right), [])
        self.assertTrue(self.errors(time=dict(right, align="left")))

    def test_too_many_sprites_counting_copies(self):
        many = [layer(sprite=STAR, x=10 + 30 * i, y=10, copies=4, stagger={"x": 0, "y": 10, "phase": 0}) for i in range(7)]
        self.assertTrue(any("sprites on screen" in e for e in self.errors(layers=many, particles=[])))
        self.assertFalse(any("sprites on screen" in e for e in self.errors(layers=many[:6], particles=[])))

    def test_too_many_particles(self):
        three = [emitter(count=30) for _ in range(3)]    # 90
        self.assertTrue(any("particles" in e for e in self.errors(particles=three)))

    def test_nothing_moving_costs_nothing(self):
        r = report(layers=[layer()], particles=[])
        self.assertEqual(r["budget"]["px"], 0)
        self.assertEqual(r["budget"]["ratio"], 0)

    def test_a_calm_face_fits(self):
        bob = {"type": "bob", "period": 4000, "phase": 0, "amp": 4}
        r = report(layers=[layer(motion=[bob])], particles=[emitter()])
        self.assertEqual(r["errors"], [])
        self.assertGreater(r["budget"]["px"], 0)
        self.assertLess(r["budget"]["ratio"], 1)
        self.assertEqual({i["what"] for i in r["budget"]["items"]}, {"Layer 1 (Panda)", "Particles 1"})

    def test_faster_costs_more(self):
        slow = report(layers=[layer(motion=[{"type": "path", "period": 20000, "phase": 0, "dx": 40, "dy": 0, "mode": "pingpong"}])])
        fast = report(layers=[layer(motion=[{"type": "path", "period": 2000, "phase": 0, "dx": 40, "dy": 0, "mode": "pingpong"}])])
        self.assertLess(slow["budget"]["px"], fast["budget"]["px"])

    def test_copies_multiply(self):
        one = report(layers=[layer(sprite=HEART, x=10, y=10, motion=[{"type": "fade", "period": 2000, "phase": 0, "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 20}]}])], particles=[])
        four = report(layers=[layer(sprite=HEART, x=10, y=10, copies=4, stagger={"x": 30, "y": 0, "phase": 0}, motion=[{"type": "fade", "period": 2000, "phase": 0, "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 20}]}])], particles=[])
        self.assertEqual(four["budget"]["px"], 4 * one["budget"]["px"])

    def test_scaling_is_expensive(self):
        fade = {"type": "fade", "period": 2000, "phase": 0, "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 20}]}
        pulse = {"type": "pulse", "period": 2000, "phase": 0, "min": 100, "max": 101}
        a = report(layers=[layer(x=50, y=50, motion=[fade])], particles=[])["budget"]["px"]
        b = report(layers=[layer(x=50, y=50, motion=[pulse])], particles=[])["budget"]["px"]
        self.assertGreater(b, 2.5 * a)

    def test_blink_is_cheap_because_it_flips_rarely(self):
        blink = {"type": "blink", "period": 4000, "phase": 0, "on": 150}
        fade = {"type": "fade", "period": 4000, "phase": 0, "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 0}]}
        self.assertLess(report(layers=[layer(motion=[blink])], particles=[])["budget"]["px"],
                        report(layers=[layer(motion=[fade])], particles=[])["budget"]["px"] / 10)

    def test_over_budget_is_an_error(self):
        pulse = {"type": "pulse", "period": 2000, "phase": 0, "min": 80, "max": 100}
        r = report(layers=[layer(x=100, y=60, sprite=PANDA, motion=[pulse], copies=3, stagger={"x": 0, "y": 0, "phase": 0})], particles=[])
        self.assertGreater(r["budget"]["ratio"], 1)
        self.assertTrue(any("redraws too much" in e for e in r["errors"]))

    def test_twinkle_costs_the_sprite_area(self):
        r = report(layers=[], particles=[emitter(kind="twinkle", sprites=[STAR], count=10)])
        self.assertEqual(r["budget"]["px"], 10 * 8 * 8)

    def test_memory_budget(self):
        huge = {h: {"kind": "sprite", "w": 160, "h": 160, "bytes": 76_812} for h in ("%016x" % n for n in range(100, 130))}
        assets = dict(ASSETS, **huge)
        layers = [dict(layer(sprite=h), x=0, y=0) for h in list(huge)[:8]]
        f = model.clean_face(face(layers=layers, particles=[]), assets, "a1b2c3d4e5f6")
        self.assertEqual(model.face_report(f)["errors"], [])                # 8 sprites + the background: fine
        f = model.clean_face(face(layers=layers[:1], particles=[emitter(sprites=[SNOW])]), assets, "x")
        f["assets"].update({h: huge[h] for h in list(huge)[:20]})            # twenty big sprites in one face
        self.assertTrue(any("memory" in e for e in model.face_report(f)["errors"]))


class RulesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(VECTORS) as f:
            cls.vectors = json.load(f)

    def test_matches(self):
        for case in self.vectors["matches"]:
            with self.subTest(case["name"]):
                self.assertEqual(model.face_matches(case["when"], case["now"]), case["match"])

    def test_picks(self):
        for case in self.vectors["picks"]:
            with self.subTest(case["name"]):
                self.assertEqual(model.pick_face(case["faces"], case["default"], case["pinned"], case["now"]), case["expect"])

    def test_local_fields(self):
        sat = datetime.datetime(2026, 11, 7, 23, 5)    # a Saturday
        self.assertEqual(model.local_fields(sat), {"year": 2026, "month": 11, "day": 7, "hour": 23, "weekday": 6})
        sun = datetime.datetime(2026, 11, 8, 0, 0)
        self.assertEqual(model.local_fields(sun)["weekday"], 0)


class HelperTests(unittest.TestCase):
    def test_draft_hashes_finds_every_asset_in_any_shape(self):
        draft = {"background": BG, "icon": {"sprite": ICON}, "layers": [{"sprite": PANDA}, "junk", {"sprite": 5}],
                 "particles": [{"sprites": [SNOW, "nope"]}, None]}
        self.assertEqual(model.draft_hashes(draft), {BG, ICON, PANDA, SNOW})
        self.assertEqual(model.draft_hashes("not a dict"), set())
        self.assertEqual(model.draft_hashes({"layers": "x", "particles": 3}), set())

    def test_ids(self):
        self.assertTrue(model.valid_id(model.new_id()))
        for bad in ("", "short", "A" * 12, "g" * 12, None, "../state0000"):
            self.assertFalse(model.valid_id(bad))

    def test_limits_the_page_gets_match_the_constants(self):
        lim = model.face_limits()
        self.assertEqual((lim["layers"], lim["budget_px"], lim["sprite_frame"]), (model.MAX_LAYERS, model.BUDGET_PX, 160))

    def test_put_face_after_another(self):
        s = model.empty_state()
        for i in "abc":
            model.put_face(s, {"id": i, "rev": "r"})
        model.put_face(s, {"id": "new", "rev": "r"}, after="a")
        self.assertEqual([e["id"] for e in s["faces"]["list"]], ["a", "new", "b", "c"])
        model.put_face(s, {"id": "new2", "rev": "r"}, after="gone")
        self.assertEqual(s["faces"]["list"][-1]["id"], "new2")


class FaceListTests(unittest.TestCase):
    def state_with(self, *ids):
        s = model.empty_state()
        for i in ids:
            model.put_face(s, {"id": i, "rev": "r" + i})
        return s

    def test_put_keeps_place_and_adds_at_the_end(self):
        s = self.state_with("a", "b", "c")
        model.put_face(s, {"id": "b", "rev": "new"})
        self.assertEqual([(e["id"], e["rev"]) for e in s["faces"]["list"]], [("a", "ra"), ("b", "new"), ("c", "rc")])

    def test_limit(self):
        s = self.state_with(*[f"f{i}" for i in range(model.MAX_FACES)])
        with self.assertRaises(Invalid):
            model.put_face(s, {"id": "one-more", "rev": "r"})
        model.put_face(s, {"id": "f0", "rev": "changed"})   # editing one is still fine

    def test_drop_clears_pin_and_default(self):
        s = self.state_with("a", "b")
        s["faces"].update(default="a", pinned="a")
        model.drop_face(s, "a")
        self.assertEqual(s["faces"], {"list": [{"id": "b", "rev": "rb"}], "default": "", "pinned": ""})

    def test_settings(self):
        s = self.state_with("a", "b", "c")
        model.apply_face_settings(s, {"order": ["c", "a", "b"], "pinned": "b", "default": "a"})
        self.assertEqual([e["id"] for e in s["faces"]["list"]], ["c", "a", "b"])
        self.assertEqual((s["faces"]["pinned"], s["faces"]["default"]), ("b", "a"))
        model.apply_face_settings(s, {"pinned": ""})
        self.assertEqual((s["faces"]["pinned"], s["faces"]["default"]), ("", "a"))

    def test_settings_must_make_sense(self):
        s = self.state_with("a", "b")
        for bad in ({"order": ["a"]}, {"order": ["a", "a"]}, {"order": ["a", "b", "c"]}, {"pinned": "zzz"}, {"default": "zzz"}, []):
            with self.assertRaises(Invalid, msg=bad):
                model.apply_face_settings(s, bad)

    def test_old_states_get_an_empty_list(self):
        s = model.normalize_state({"version": 3, "alarms": []})
        self.assertEqual(s["faces"], {"list": [], "default": "", "pinned": ""})
        self.assertEqual(model.normalize_state({"faces": "nonsense"})["faces"], {"list": [], "default": "", "pinned": ""})


class AssetCleanupTests(unittest.TestCase):
    def test_only_old_unused_assets_go(self):
        metas = {"used": {"at": 0}, "fresh": {"at": 9000}, "stale": {"at": 100}}
        faces = [{"assets": {"used": {}}}]
        self.assertEqual(model.unreferenced_assets(metas, faces, now=10_000), ["stale"])

    def test_nothing_goes_while_it_is_still_in_a_face(self):
        faces = [{"assets": {"a": {}}}, {"assets": {"b": {}}}]
        self.assertEqual(model.unreferenced_assets({"a": {"at": 0}, "b": {"at": 0}}, faces, now=10**9), [])


if __name__ == "__main__":
    unittest.main()
