"""Data rules for the clock portal.

Pure Python with no Workers imports, so it can be unit-tested with plain `python3`.

Everything the clock needs lives in one JSON document (the "state"):

    {
      "version": 7,                                  # bumped on every change; the device ETag
      "message": {"text": "...", "at": 1730000000},  # or None
      "image":   {"id": "a1b2c3d4e5f6", "at": ..., "bytes": 31877},  # or None
      "alarms":  [ {alarm}, ... ],
      "faces":   {"list": [{"id": "...", "rev": "..."}, ...],   # priority order: earlier wins
                  "default": "", "pinned": ""}                  # a face id, or "" for none
    }

An alarm:

    {
      "id": "a1b2c3d4e5f6",
      "time": "07:30",          # 24 h, in the clock's own time zone
      "days": [1, 2, 3, 4, 5],  # 0 = Sunday ... 6 = Saturday (same as C's tm_wday); [] = once
      "date": "",               # "YYYY-MM-DD" when days is [] (a one-off alarm), else ""
      "label": "Wake up!",
      "sound": "",              # file in /sounds on the clock's card; "" = built-in beep
      "enabled": true
    }

A clock face is a separate JSON document, stored under its own KV key (see "faces" below for its shape).
"""

import datetime
import hashlib
import json
import math
import re
import secrets

# Keep these in step with proto/clock.options (nanopb's fixed buffer sizes on the clock).
MESSAGE_MAX = 120      # characters (<= 480 UTF-8 bytes)
MESSAGE_LINES = 4
LABEL_MAX = 40         # characters (<= 160 UTF-8 bytes)
SOUND_MAX = 64         # UTF-8 bytes
MAX_ALARMS = 20
MAX_SOUNDS = 100

IMAGE_W, IMAGE_H = 320, 240      # the clock's screen, landscape
IMAGE_MAX_BYTES = 200_000        # a 320x240 JPEG at q≈0.85 is ~20-45 KB

_TIME = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")


class Invalid(ValueError):
    """Bad input from a client; the message is safe to show to the user."""


def new_id() -> str:
    return secrets.token_hex(6)


def empty_state() -> dict:
    return {"version": 0, "message": None, "image": None, "alarms": [],
            "faces": {"list": [], "default": "", "pinned": ""}}


def normalize_state(state) -> dict:
    """Fill in anything missing from a stored state (older shapes, or nothing stored yet)."""
    base = empty_state()
    if isinstance(state, dict):
        base.update({k: state[k] for k in base if k in state})
    if not isinstance(base["alarms"], list):
        base["alarms"] = []
    faces = base["faces"] if isinstance(base["faces"], dict) else {}
    base["faces"] = {"list": [f for f in faces.get("list", []) if isinstance(f, dict) and "id" in f and "rev" in f],
                     "default": faces.get("default") or "", "pinned": faces.get("pinned") or ""}
    return base


# ---------- message ----------

def clean_message(text) -> str:
    """Trimmed message text; "" means "no message"."""
    if not isinstance(text, str):
        raise Invalid("The message must be text.")
    text = text.replace("\r\n", "\n").replace("\r", "\n").strip()
    # control characters (other than newlines) would show up as boxes on the clock
    text = "".join(c for c in text if c == "\n" or (c >= " " and c != "\x7f"))
    text = re.sub(r"\n{2,}", "\n", text)
    if len(text) > MESSAGE_MAX:
        raise Invalid(f"Keep the message under {MESSAGE_MAX} characters.")
    if text.count("\n") >= MESSAGE_LINES:
        raise Invalid(f"The message can have at most {MESSAGE_LINES} lines.")
    return text


# ---------- alarms ----------

def clean_alarm(data, alarm_id: str) -> dict:
    if not isinstance(data, dict):
        raise Invalid("Alarm must be an object.")

    time = data.get("time")
    if not isinstance(time, str) or not _TIME.match(time):
        raise Invalid("Pick a time for the alarm (HH:MM).")

    days = data.get("days", [])
    if not isinstance(days, list) or not all(isinstance(d, int) and not isinstance(d, bool) and 0 <= d <= 6 for d in days):
        raise Invalid("Days must be numbers from 0 (Sunday) to 6 (Saturday).")
    days = sorted(set(days))

    date = ""
    if not days:
        date = data.get("date", "")
        if not isinstance(date, str):
            raise Invalid("Pick a date for a one-off alarm.")
        try:
            datetime.date.fromisoformat(date)
        except ValueError:
            raise Invalid("Pick a date for a one-off alarm.") from None
        if len(date) != 10:  # fromisoformat also takes "20261106"; keep one spelling for the firmware
            raise Invalid("Dates look like 2026-11-06.")

    label = data.get("label", "")
    if not isinstance(label, str):
        raise Invalid("The label must be text.")
    label = " ".join(label.split())
    if len(label) > LABEL_MAX:
        raise Invalid(f"Keep the label under {LABEL_MAX} characters.")

    sound = data.get("sound", "")
    if not _sound_name_ok(sound, allow_empty=True):
        raise Invalid("Unknown sound.")

    enabled = data.get("enabled", True)
    if not isinstance(enabled, bool):
        raise Invalid("enabled must be true or false.")

    return {"id": alarm_id, "time": time, "days": days, "date": date,
            "label": label, "sound": sound, "enabled": enabled}


def sort_alarms(alarms: list) -> list:
    return sorted(alarms, key=lambda a: (a["time"], a["date"], a["id"]))


# ---------- photo ----------

# Start-of-frame markers carry the image size; C2/C6/CA/CE are progressive.
_SOF = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}
_PROGRESSIVE = {0xC2, 0xC6, 0xCA, 0xCE}


def jpeg_info(data: bytes):
    """(width, height, progressive) read from a JPEG's frame header. Raises Invalid if it isn't one."""
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        raise Invalid("That isn't a JPEG.")
    i, n = 2, len(data)
    while i + 4 <= n:
        if data[i] != 0xFF:
            raise Invalid("That JPEG looks damaged.")
        marker = data[i + 1]
        if marker == 0xFF:  # fill byte
            i += 1
            continue
        if marker == 0x01 or 0xD0 <= marker <= 0xD8:  # markers without a length
            i += 2
            continue
        seg = int.from_bytes(data[i + 2:i + 4], "big")
        if seg < 2:
            raise Invalid("That JPEG looks damaged.")
        if marker in _SOF:
            if i + 9 > n:
                break
            height = int.from_bytes(data[i + 5:i + 7], "big")
            width = int.from_bytes(data[i + 7:i + 9], "big")
            return width, height, marker in _PROGRESSIVE
        if marker in (0xDA, 0xD9):  # scan data / end before any frame header
            break
        i += 2 + seg
    raise Invalid("That JPEG looks damaged.")


def check_clock_photo(data: bytes) -> None:
    """The clock decodes photos with TJpgDec: baseline only, exactly the screen size."""
    if len(data) > IMAGE_MAX_BYTES:
        raise Invalid(f"The photo is too big ({len(data) // 1000} KB); the limit is {IMAGE_MAX_BYTES // 1000} KB.")
    width, height, progressive = jpeg_info(data)
    if (width, height) != (IMAGE_W, IMAGE_H):
        raise Invalid(f"The photo must be {IMAGE_W}x{IMAGE_H} (got {width}x{height}).")
    if progressive:
        raise Invalid("Progressive JPEGs can't be shown on the clock.")


# ---------- device check-ins ----------

def clean_sounds(data) -> list:
    if not isinstance(data, list) or len(data) > MAX_SOUNDS:
        raise Invalid("sounds must be a list of file names.")
    if not all(_sound_name_ok(s) for s in data):
        raise Invalid("sounds must be a list of file names.")
    return sorted(set(data), key=str.lower)


def _sound_name_ok(name, allow_empty=False) -> bool:
    if not isinstance(name, str):
        return False
    if not name:
        return allow_empty
    return len(name.encode()) <= SOUND_MAX and "/" not in name and "\\" not in name and name.isprintable()



# =====================================================================================================
# Clock faces
#
# A face is a layered scene. This is its JSON shape, as the admin page edits and KV stores it (the clock gets the
# same thing as protobuf, see wire.py / proto/clock.proto). Coordinates are pixels on the 320x240 screen.
#
#   {
#     "id": "a1b2c3d4e5f6", "name": "Paris at sunset",
#     "background": "<asset hash>",                   # a 320x240 baseline JPEG
#     "time": {"x": 160, "y": 40, "size": 64, "color": "#ffffff", "shadow": "#000000", "align": "center"},
#     "date": {... same ..., "format": "long"} | None,
#     "icon": {layer} | None,                         # drawn above the text
#     "layers": [ {"name": "Panda", "sprite": "<hash>", "x": 80, "y": 90, "copies": 1,
#                  "stagger": {"x": 0, "y": 0, "phase": 0}, "motion": [ {motion}, ... ]} ],
#     "particles": [ {"kind": "fall", "sprites": ["<hash>"], "count": 24, "area": {"x":0,"y":0,"w":320,"h":240},
#                     "speed_min": 15, "speed_max": 40, "sway": 12, "period": 4000, "phase": 0,
#                     "depth": "front", "seed": 1234} ],
#     "when": {"hours": {"from": 22, "to": 6} | None, "months": [12, 1], "weekdays": [0, 6],
#              "dates": [{"month": 12, "day": 25, "year": None}]},
#     # added by the server, never sent by the page:
#     "assets": {"<hash>": {"kind": "bg" | "sprite", "w": 320, "h": 240, "bytes": 31877}},
#     "rev": "0123456789ab"                           # hash of everything above; changes whenever the face does
#   }
#
# Motions (every one has "period" in ms, 200-60000, and "phase" in ms, 0 <= phase < period):
#   {"type": "bob", "amp": 4}            sway likewise. Vertical / horizontal sine, in pixels
#   {"type": "path", "dx": 40, "dy": 0, "mode": "pingpong" | "loop"}
#   {"type": "fade", "keys": [{"at": 0, "v": 100}, {"at": 500, "v": 30}]}   at: permille of the period, v: opacity %
#   {"type": "blink", "on": 150}         visible for the first `on` ms of every period
#   {"type": "pulse", "min": 90, "max": 110}                                 scale in percent, about the centre
#   {"type": "frames", "frames": 4, "fps": 6}                                a horizontal sprite sheet (period is derived)
# There is no rotation: the clock would rotate in software, which is slow. Use a "frames" sheet instead.
#
# The formulas are written out in portal/README.md and proto/clock.proto, and implemented once in
# public/face-motion.js (the preview). Nothing here runs a motion; this file only checks that a face is allowed.
# =====================================================================================================

# Keep the counts in step with proto/clock.options (nanopb's fixed arrays on the clock).
MAX_FACES = 12
FACE_NAME_MAX = 30
MAX_LAYERS = 8
MAX_COPIES = 12
MAX_INSTANCES = 24          # sprites drawn from layers + icon, copies counted
MAX_MOTIONS = 3             # per layer
MAX_FADE_KEYS = 4
MAX_EMITTERS = 3
MAX_EMITTER_SPRITES = 3
MAX_EMITTER_COUNT = 40
MAX_PARTICLES = 60          # over all emitters
MAX_DATES = 8
LAYER_NAME_MAX = 24

# Sprite files (LVGL RGB565A8). A "frames" layer uses a sheet of equal frames side by side.
SPRITE_MAX_FRAME = 160      # one sprite or frame: at most 160 x 160
SPRITE_MAX_W = 1280         # a sheet of up to 8 frames of 160 wide
SPRITE_MAX_H = 160
SPRITE_MAX_PIXELS = 51_200  # 150 KB as RGB565A8: 160x160 twice over
ICON_MAX = 64
PARTICLE_SPRITE_MAX = 48
PNG_MAX_BYTES = 300_000
SPRITE_HEADER = 12          # lv_image_header_t
LV_MAGIC, LV_CF_RGB565A8 = 0x19, 0x14

# Everything of a face that sits in the clock's PSRAM at once: the decoded background (RGB565) + the sprites.
FACE_MAX_BYTES = 1_500_000
BACKGROUND_DECODED_BYTES = IMAGE_W * IMAGE_H * 2

TEXT_MIN, TEXT_MAX = 10, 96
# Montserrat Medium: line height (hhea ascent 968 + descent 251) and the widest string each slot can show, in
# 1/1000 em, measured from the font's advance widths. The page's preview draws with the same font.
TEXT_LINE_EM = 1.219
TEXT_BASELINE_EM = 0.968
TIME_WIDTH_EM = 2.901
DATE_WIDTH_EM = {"long": 13.763, "short": 6.540, "day_month": 7.214, "weekday": 6.053, "numeric": 5.955}
DATE_FORMATS = tuple(DATE_WIDTH_EM)
ALIGNS = ("left", "center", "right")

# Performance budget. The panel hangs off a 40 MHz SPI bus: ~2.5 M pixels/s at best, in practice about half of that,
# and the same core also decodes audio and handles Wi-Fi. Whatever moves gets redrawn, so we cap how many pixels
# change per animation tick. 30,000 px/tick at 15 ticks/s is 450 k px/s: roughly a fifth of the bus, and the face
# is still smooth. A full-screen redraw would be 76,800.
ANIM_FPS = 15
BUDGET_PX = 30_000
SCALE_COST = 3              # a scaled sprite is drawn by software transform: count it three times

_HASH = re.compile(r"^[0-9a-f]{16}$")
_COLOR = re.compile(r"^#[0-9a-f]{6}$")
_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


_ID = re.compile(r"^[0-9a-f]{12}$")


def valid_id(i) -> bool:
    """Ids made by new_id(): 12 hex chars."""
    return isinstance(i, str) and bool(_ID.match(i))


def draft_hashes(data) -> set:
    """Every asset hash a face draft mentions (whatever shape it is in), so the server knows which to look up."""
    found = set()

    def take(h):
        if valid_hash(h):
            found.add(h)

    if isinstance(data, dict):
        take(data.get("background"))
        for layer in [data.get("icon")] + (data.get("layers") if isinstance(data.get("layers"), list) else []):
            if isinstance(layer, dict):
                take(layer.get("sprite"))
        for p in data.get("particles") if isinstance(data.get("particles"), list) else []:
            if isinstance(p, dict) and isinstance(p.get("sprites"), list):
                for h in p["sprites"]:
                    take(h)
    return found


def face_limits() -> dict:
    """The numbers the page needs to enforce the same limits as the server."""
    return {"faces": MAX_FACES, "name": FACE_NAME_MAX, "layers": MAX_LAYERS, "copies": MAX_COPIES, "instances": MAX_INSTANCES,
            "motions": MAX_MOTIONS, "emitters": MAX_EMITTERS, "emitter_sprites": MAX_EMITTER_SPRITES,
            "particles_each": MAX_EMITTER_COUNT, "particles": MAX_PARTICLES, "dates": MAX_DATES,
            "sprite_frame": SPRITE_MAX_FRAME, "sprite_w": SPRITE_MAX_W, "sprite_h": SPRITE_MAX_H, "sprite_pixels": SPRITE_MAX_PIXELS,
            "icon": ICON_MAX, "particle_sprite": PARTICLE_SPRITE_MAX, "text_min": TEXT_MIN, "text_max": TEXT_MAX,
            "fade_keys": MAX_FADE_KEYS, "anim_fps": ANIM_FPS, "budget_px": BUDGET_PX}


def valid_hash(h) -> bool:
    return isinstance(h, str) and bool(_HASH.match(h))


def asset_hash(data: bytes) -> str:
    """Assets are named by their content: 16 hex chars of the SHA-256."""
    return hashlib.sha256(data).hexdigest()[:16]


# ---------- asset files ----------

def check_sprite_file(data: bytes):
    """(w, h) of an LVGL 9 RGB565A8 raw image: 12-byte header, RGB565 plane, A8 plane. Raises Invalid."""
    if len(data) < SPRITE_HEADER or data[0] != LV_MAGIC or data[1] != LV_CF_RGB565A8:
        raise Invalid("That isn't a clock sprite (expected an LVGL RGB565A8 image).")
    flags = int.from_bytes(data[2:4], "little")
    w, h = int.from_bytes(data[4:6], "little"), int.from_bytes(data[6:8], "little")
    stride = int.from_bytes(data[8:10], "little")
    if flags or int.from_bytes(data[10:12], "little") or stride != w * 2 or w < 1 or h < 1:
        raise Invalid("That sprite's header looks wrong.")
    if len(data) != SPRITE_HEADER + h * stride + h * w:
        raise Invalid("That sprite is the wrong length for its size.")
    if w > SPRITE_MAX_W or h > SPRITE_MAX_H or w * h > SPRITE_MAX_PIXELS:
        raise Invalid(f"Sprites can be at most {SPRITE_MAX_FRAME}x{SPRITE_MAX_FRAME} "
                      f"(a sprite sheet {SPRITE_MAX_W}x{SPRITE_MAX_H}, {SPRITE_MAX_PIXELS} pixels in all); that one is {w}x{h}.")
    return w, h


def png_size(data: bytes):
    """(w, h) from a PNG's header. Raises Invalid if it isn't one."""
    if len(data) > PNG_MAX_BYTES:
        raise Invalid(f"That PNG is too big ({len(data) // 1000} KB).")
    if len(data) < 33 or data[:8] != _PNG_MAGIC or data[12:16] != b"IHDR":
        raise Invalid("That isn't a PNG.")
    return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")


# ---------- cleaning ----------

def _obj(v, what):
    if not isinstance(v, dict):
        raise Invalid(f"{what} must be an object.")
    return v


def _int(v, lo, hi, what):
    if isinstance(v, float) and v.is_integer():
        v = int(v)
    if not isinstance(v, int) or isinstance(v, bool):
        raise Invalid(f"{what} must be a whole number.")
    if not lo <= v <= hi:
        raise Invalid(f"{what} must be between {lo} and {hi}.")
    return v


def _color(v, what, allow_empty=False):
    if allow_empty and (v is None or v == ""):
        return ""
    if not isinstance(v, str) or not _COLOR.match(v.lower()):
        raise Invalid(f"{what} must be a colour like #ffffff.")
    return v.lower()


def _enum(v, options, what):
    if v not in options:
        raise Invalid(f"{what} must be one of: {', '.join(options)}.")
    return v


def _asset(assets, h, kind, what):
    meta = assets.get(h) if valid_hash(h) else None
    if not meta or meta.get("kind") != kind:
        raise Invalid(f"{what}: that image is missing. Upload it again.")
    return meta


def _clean_name(v, what, limit):
    if not isinstance(v, str):
        raise Invalid(f"{what} must be text.")
    v = " ".join(v.split())
    if len(v) > limit:
        raise Invalid(f"Keep the {what} under {limit} characters.")
    return v


def _clean_slot(d, what, is_date):
    d = _obj(d, what)
    slot = {
        "x": _int(d.get("x"), -1000, 1000, f"The {what}'s x"),
        "y": _int(d.get("y"), -1000, 1000, f"The {what}'s y"),
        "size": _int(d.get("size"), TEXT_MIN, TEXT_MAX, f"The {what}'s size"),
        "color": _color(d.get("color"), f"The {what}'s colour"),
        "shadow": _color(d.get("shadow"), f"The {what}'s shadow", allow_empty=True),
        "align": _enum(d.get("align", "center"), ALIGNS, f"The {what}'s alignment"),
    }
    if is_date:
        slot["format"] = _enum(d.get("format", "long"), DATE_FORMATS, "The date format")
    return slot


def _clean_motion(m, where, sprite_w):
    m = _obj(m, where)
    kind = m.get("type")
    if kind == "frames":
        frames = _int(m.get("frames"), 2, 16, f"{where}: frames")
        fps = _int(m.get("fps"), 1, ANIM_FPS, f"{where}: frames per second")
        if sprite_w % frames:
            raise Invalid(f"{where}: the sprite is {sprite_w} px wide, which doesn't split into {frames} equal frames.")
        period = round(1000 * frames / fps)
        out = {"type": "frames", "period": period, "frames": frames, "fps": fps}
    else:
        period = _int(m.get("period"), 200, 60000, f"{where}: period")
        if kind in ("bob", "sway"):
            out = {"type": kind, "period": period, "amp": _int(m.get("amp"), -80, 80, f"{where}: amount")}
        elif kind == "path":
            out = {"type": "path", "period": period, "dx": _int(m.get("dx"), -320, 320, f"{where}: dx"),
                   "dy": _int(m.get("dy"), -240, 240, f"{where}: dy"),
                   "mode": _enum(m.get("mode", "pingpong"), ("pingpong", "loop"), f"{where}: mode")}
        elif kind == "fade":
            keys = m.get("keys")
            if not isinstance(keys, list) or not 2 <= len(keys) <= MAX_FADE_KEYS:
                raise Invalid(f"{where}: a fade needs 2 to {MAX_FADE_KEYS} keyframes.")
            out_keys, last = [], -1
            for k in keys:
                k = _obj(k, f"{where}: keyframe")
                at, v = _int(k.get("at"), 0, 1000, f"{where}: keyframe time"), _int(k.get("v"), 0, 100, f"{where}: opacity")
                if at <= last:
                    raise Invalid(f"{where}: keyframes must go forward in time.")
                last = at
                out_keys.append({"at": at, "v": v})
            if out_keys[0]["at"] != 0:
                raise Invalid(f"{where}: the first keyframe must be at 0.")
            out = {"type": "fade", "period": period, "keys": out_keys}
        elif kind == "blink":
            out = {"type": "blink", "period": period, "on": _int(m.get("on"), 1, period - 1, f"{where}: visible time")}
        elif kind == "pulse":
            lo, hi = _int(m.get("min"), 50, 200, f"{where}: smallest size"), _int(m.get("max"), 50, 200, f"{where}: biggest size")
            if lo >= hi:
                raise Invalid(f"{where}: the biggest size must be more than the smallest.")
            out = {"type": "pulse", "period": period, "min": lo, "max": hi}
        else:
            raise Invalid(f"{where}: unknown motion '{kind}'. (There's no rotation: use a frames sprite sheet.)")
    out["phase"] = _int(m.get("phase", 0), 0, 10**6, f"{where}: phase") % out["period"]
    return out


def _clean_layer(d, assets, where, limit):
    d = _obj(d, where)
    sprite = d.get("sprite")
    meta = _asset(assets, sprite, "sprite", where)
    copies = _int(d.get("copies", 1), 1, MAX_COPIES, f"{where}: copies")
    st = _obj(d.get("stagger") or {}, f"{where}: stagger")
    motions = d.get("motion", [])
    if not isinstance(motions, list) or len(motions) > MAX_MOTIONS:
        raise Invalid(f"{where}: at most {MAX_MOTIONS} motions.")
    motions = [_clean_motion(m, f"{where}, motion {i + 1}", meta["w"]) for i, m in enumerate(motions)]
    if sum(m["type"] == "frames" for m in motions) > 1:
        raise Invalid(f"{where}: only one frames motion per layer.")
    if any(m["type"] == "frames" for m in motions) and any(m["type"] == "pulse" for m in motions):
        raise Invalid(f"{where}: a sprite sheet can't pulse (the clock would show its neighbouring frames when it scales).")
    fw = meta["w"] // next((m["frames"] for m in motions if m["type"] == "frames"), 1)
    if fw > limit or meta["h"] > limit:
        raise Invalid(f"{where}: the sprite is {fw}x{meta['h']}; here the limit is {limit}x{limit}.")
    return {
        "name": _clean_name(d.get("name", ""), "layer name", LAYER_NAME_MAX),
        "sprite": sprite,
        "x": _int(d.get("x"), -1000, 1000, f"{where}: x"),
        "y": _int(d.get("y"), -1000, 1000, f"{where}: y"),
        "copies": copies,
        "stagger": {"x": _int(st.get("x", 0), -320, 320, f"{where}: stagger x"),
                    "y": _int(st.get("y", 0), -240, 240, f"{where}: stagger y"),
                    "phase": _int(st.get("phase", 0), -60000, 60000, f"{where}: stagger phase")},
        "motion": motions,
    }


def _clean_particles(d, assets, where):
    d = _obj(d, where)
    kind = _enum(d.get("kind"), ("fall", "rise", "twinkle"), f"{where}: kind")
    sprites = d.get("sprites")
    if not isinstance(sprites, list) or not 1 <= len(sprites) <= MAX_EMITTER_SPRITES:
        raise Invalid(f"{where}: pick 1 to {MAX_EMITTER_SPRITES} sprites.")
    for h in sprites:
        meta = _asset(assets, h, "sprite", where)
        if meta["w"] > PARTICLE_SPRITE_MAX or meta["h"] > PARTICLE_SPRITE_MAX:
            raise Invalid(f"{where}: particle sprites can be at most {PARTICLE_SPRITE_MAX}x{PARTICLE_SPRITE_MAX} "
                          f"(that one is {meta['w']}x{meta['h']}).")
    area = _obj(d.get("area"), f"{where}: area")
    speed_min, speed_max = _int(d.get("speed_min", 0), 0, 400, f"{where}: slowest"), _int(d.get("speed_max", 0), 0, 400, f"{where}: fastest")
    if speed_min > speed_max:
        raise Invalid(f"{where}: the slowest speed can't be above the fastest.")
    period = _int(d.get("period", 4000), 500, 60000, f"{where}: period")
    return {
        "kind": kind,
        "sprites": list(sprites),
        "count": _int(d.get("count"), 1, MAX_EMITTER_COUNT, f"{where}: count"),
        "area": {"x": _int(area.get("x"), -1000, 1000, f"{where}: area x"), "y": _int(area.get("y"), -1000, 1000, f"{where}: area y"),
                 "w": _int(area.get("w"), 1, 1000, f"{where}: area width"), "h": _int(area.get("h"), 1, 1000, f"{where}: area height")},
        "speed_min": speed_min, "speed_max": speed_max,
        "sway": _int(d.get("sway", 0), 0, 60, f"{where}: sway"),
        "period": period,
        "phase": _int(d.get("phase", 0), 0, 10**6, f"{where}: phase") % period,
        "depth": _enum(d.get("depth", "front"), ("front", "back"), f"{where}: depth"),
        "seed": _int(d.get("seed", 1), 0, 2**32 - 1, f"{where}: seed"),
    }


def _clean_when(d):
    d = _obj(d if d is not None else {}, "The rules")
    hours = d.get("hours")
    if hours is not None:
        hours = _obj(hours, "The hours")
        hours = {"from": _int(hours.get("from"), 0, 23, "The first hour"), "to": _int(hours.get("to"), 1, 24, "The last hour")}
        if hours["from"] == hours["to"]:
            raise Invalid("The hours can't start and end at the same time: leave them out to mean all day.")
    months, weekdays = d.get("months", []), d.get("weekdays", [])
    if not isinstance(months, list) or not isinstance(weekdays, list):
        raise Invalid("Months and weekdays must be lists.")
    months = sorted({_int(m, 1, 12, "A month") for m in months})
    weekdays = sorted({_int(w, 0, 6, "A weekday") for w in weekdays})
    dates = d.get("dates", [])
    if not isinstance(dates, list) or len(dates) > MAX_DATES:
        raise Invalid(f"At most {MAX_DATES} special dates.")
    out_dates = []
    for item in dates:
        item = _obj(item, "A date")
        month, day = _int(item.get("month"), 1, 12, "The date's month"), _int(item.get("day"), 1, 31, "The date's day")
        year = item.get("year")
        year = None if year in (None, 0, "") else _int(year, 2000, 2100, "The date's year")
        try:
            datetime.date(year or 2024, month, day)  # 2024 is a leap year, so a yearly Feb 29 is allowed
        except ValueError:
            raise Invalid(f"{day}/{month} isn't a real date.") from None
        if {"month": month, "day": day, "year": year} not in out_dates:
            out_dates.append({"month": month, "day": day, "year": year})
    return {"hours": hours, "months": months, "weekdays": weekdays, "dates": out_dates}


def when_is_empty(when: dict) -> bool:
    return not (when.get("hours") or when.get("months") or when.get("weekdays") or when.get("dates"))


def clean_face(data, assets: dict, face_id: str) -> dict:
    """A face from the admin page, checked and normalised. `assets` maps hash -> {"kind", "w", "h", "bytes"} for every
    asset that exists. Raises Invalid with the first thing wrong. Pass the result to face_report() for the
    on-screen and performance checks."""
    d = _obj(data, "A face")
    name = _clean_name(d.get("name", ""), "name", FACE_NAME_MAX)
    if not name:
        raise Invalid("Give the face a name.")
    bg = d.get("background")
    bg_meta = _asset(assets, bg, "bg", "The background")
    if (bg_meta["w"], bg_meta["h"]) != (IMAGE_W, IMAGE_H):
        raise Invalid(f"The background must be {IMAGE_W}x{IMAGE_H}.")
    layers = d.get("layers", [])
    if not isinstance(layers, list) or len(layers) > MAX_LAYERS:
        raise Invalid(f"At most {MAX_LAYERS} layers.")
    particles = d.get("particles", [])
    if not isinstance(particles, list) or len(particles) > MAX_EMITTERS:
        raise Invalid(f"At most {MAX_EMITTERS} particle effects.")
    face = {
        "id": face_id,
        "name": name,
        "background": bg,
        "time": _clean_slot(d.get("time"), "time", False),
        "date": None if d.get("date") is None else _clean_slot(d["date"], "date", True),
        "icon": None if d.get("icon") is None else _clean_layer(d["icon"], assets, "The icon", ICON_MAX),
        "layers": [_clean_layer(layer, assets, f"Layer {i + 1}", SPRITE_MAX_FRAME) for i, layer in enumerate(layers)],
        "particles": [_clean_particles(p, assets, f"Particles {i + 1}") for i, p in enumerate(particles)],
        "when": _clean_when(d.get("when")),
    }
    used = {bg}
    for layer in face["layers"] + ([face["icon"]] if face["icon"] else []):
        used.add(layer["sprite"])
    for p in face["particles"]:
        used.update(p["sprites"])
    face["assets"] = {h: {k: assets[h][k] for k in ("kind", "w", "h", "bytes")} for h in sorted(used)}
    return face


def face_rev(face: dict) -> str:
    """12 hex chars that change whenever anything about the face does (so the clock re-fetches it)."""
    body = {k: v for k, v in face.items() if k != "rev"}
    return hashlib.sha1(json.dumps(body, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:12]


# ---------- on screen? how much does it redraw? ----------

def layer_frame(layer: dict, assets: dict):
    """(width, height) of one frame of a layer's sprite: the whole image, or one cell of a sheet."""
    meta = assets[layer["sprite"]]
    n = next((m["frames"] for m in layer["motion"] if m["type"] == "frames"), 1)
    return meta["w"] // n, meta["h"]


def _motion_growth(motions, fw, fh):
    """How far a sprite's box can stray from its place: (left, up, right, down) pixels. Offsets of stacked motions add."""
    left = up = right = down = 0.0
    for m in motions:
        t = m["type"]
        if t == "bob":
            up += abs(m["amp"]); down += abs(m["amp"])
        elif t == "sway":
            left += abs(m["amp"]); right += abs(m["amp"])
        elif t == "path":
            if m["dx"] > 0: right += m["dx"]
            else: left -= m["dx"]
            if m["dy"] > 0: down += m["dy"]
            else: up -= m["dy"]
        elif t == "pulse" and m["max"] > 100:
            gx, gy = fw * (m["max"] - 100) / 200, fh * (m["max"] - 100) / 200
            left += gx; right += gx; up += gy; down += gy
    return left, up, right, down


def _layer_boxes(layer, fw, fh):
    """The box each copy of a layer can reach: (copy index, left, top, right, bottom)."""
    left, up, right, down = _motion_growth(layer["motion"], fw, fh)
    for i in range(layer["copies"]):
        x, y = layer["x"] + i * layer["stagger"]["x"], layer["y"] + i * layer["stagger"]["y"]
        yield i, math.floor(x - left), math.floor(y - up), math.ceil(x + fw + right), math.ceil(y + fh + down)


def _text_box(slot, what):
    em = TIME_WIDTH_EM if what == "time" else DATE_WIDTH_EM[slot["format"]]
    w = math.ceil(em * slot["size"] * 1.03)
    h = math.ceil(TEXT_LINE_EM * slot["size"])
    x = {"left": slot["x"], "center": slot["x"] - w // 2, "right": slot["x"] - w}[slot["align"]]
    shadow = max(1, slot["size"] // 24) if slot["shadow"] else 0
    return x, slot["y"], x + w + shadow, slot["y"] + h + shadow


def _inside(box):
    return box[0] >= 0 and box[1] >= 0 and box[2] <= IMAGE_W and box[3] <= IMAGE_H


def _layer_px(layer, fw, fh):
    """Pixels redrawn per animation tick by one copy of a layer, on average. See the budget notes above."""
    tick = 1000 / ANIM_FPS
    step_x = step_y = 0.0     # peak distance moved per tick
    scale, repaint, duty = 1.0, 0.0, 0.0
    for m in layer["motion"]:
        t, p = m["type"], m["period"]
        if t == "bob":
            step_y += abs(m["amp"]) * 2 * math.pi / p * tick
        elif t == "sway":
            step_x += abs(m["amp"]) * 2 * math.pi / p * tick
        elif t == "path":
            k = (math.pi if m["mode"] == "pingpong" else 1) / p * tick
            step_x += abs(m["dx"]) * k; step_y += abs(m["dy"]) * k
        elif t == "fade":
            repaint = max(repaint, 1.0)          # opacity changes all the time: the whole sprite
        elif t == "pulse":
            scale = max(scale, m["max"] / 100)
            repaint = max(repaint, 1.0)
        elif t == "blink":
            duty = max(duty, min(1.0, 2 * tick / p))    # it flips twice per period
        elif t == "frames":
            duty = max(duty, min(1.0, m["fps"] / ANIM_FPS))
    area = fw * fh
    px = area * duty
    if step_x or step_y:      # integer positions change on `moves` of the ticks; each change redraws old + new box
        moves = min(1.0, step_x + step_y)
        px = max(px, (fw + math.ceil(step_x)) * (fh + math.ceil(step_y)) * moves)
    if repaint:
        px = max(px, area * scale * scale * repaint)
    if any(m["type"] == "pulse" for m in layer["motion"]):
        px *= SCALE_COST
    return min(px, IMAGE_W * IMAGE_H)


def _emitter_px(p, assets):
    """Pixels redrawn per tick by one emitter: every particle, at its worst-case sprite size."""
    tick = 1000 / ANIM_FPS
    w = max(assets[h]["w"] for h in p["sprites"])
    h = max(assets[h]["h"] for h in p["sprites"])
    if p["kind"] == "twinkle":
        each = w * h
    else:
        step_y = p["speed_max"] / ANIM_FPS
        step_x = p["sway"] * 2 * math.pi / (0.75 * p["period"]) * tick   # the PRNG shortens a period to 75% at most
        each = (w + math.ceil(step_x)) * (h + math.ceil(step_y)) * min(1.0, step_x + step_y)
    return each * p["count"]


def face_report(face: dict) -> dict:
    """What's wrong with a cleaned face, and how heavy it is.

    {"errors": [text, ...], "budget": {"px": changed pixels per tick, "limit": BUDGET_PX, "ratio": px/limit,
                                       "items": [{"what": ..., "px": ...}], "bytes": ..., "bytes_limit": ...}}
    """
    assets = face["assets"]
    errors, items = [], []

    for what, slot in (("time", face["time"]), ("date", face["date"])):
        if slot and not _inside(_text_box(slot, what)):
            errors.append(f"The {what} runs off the screen.")

    instances = 0
    named = [("The icon", face["icon"])] if face["icon"] else []
    named += [(f"Layer {i + 1}" + (f" ({layer['name']})" if layer["name"] else ""), layer) for i, layer in enumerate(face["layers"])]
    for what, layer in named:
        fw, fh = layer_frame(layer, assets)
        instances += layer["copies"]
        if not all(_inside(box[1:]) for box in _layer_boxes(layer, fw, fh)):
            errors.append(f"{what} moves off the screen. Move it inwards or shrink its motion.")
        px = _layer_px(layer, fw, fh) * layer["copies"]
        if px:
            items.append({"what": what, "px": round(px)})
    if instances > MAX_INSTANCES:
        errors.append(f"Too many sprites on screen ({instances}): the limit is {MAX_INSTANCES} counting copies.")

    total_particles = 0
    for i, p in enumerate(face["particles"]):
        total_particles += p["count"]
        a = p["area"]
        if not _inside((a["x"], a["y"], a["x"] + a["w"], a["y"] + a["h"])):
            errors.append(f"Particles {i + 1}: the area runs off the screen.")
        items.append({"what": f"Particles {i + 1}", "px": round(_emitter_px(p, assets))})
    if total_particles > MAX_PARTICLES:
        errors.append(f"Too many particles ({total_particles}): the limit is {MAX_PARTICLES} in all.")

    size = BACKGROUND_DECODED_BYTES + sum(m["bytes"] for h, m in assets.items() if m["kind"] == "sprite")
    total = sum(item["px"] for item in items)
    ratio = total / BUDGET_PX
    if ratio > 1:
        errors.append(f"This face redraws too much ({round(ratio * 100)}% of what the clock can animate smoothly). "
                      "Use fewer or smaller moving things.")
    if size > FACE_MAX_BYTES:
        errors.append(f"The images use too much of the clock's memory ({size // 1000} KB of {FACE_MAX_BYTES // 1000} KB).")
    items.sort(key=lambda i: -i["px"])
    return {"errors": errors, "budget": {"px": total, "limit": BUDGET_PX, "ratio": round(ratio, 3), "items": items,
                                         "bytes": size, "bytes_limit": FACE_MAX_BYTES}}


def check_face(data, assets: dict, face_id: str) -> tuple:
    """clean + report. Returns (face, report); the face has no "rev" yet. Raises Invalid on the first structural
    problem; `report["errors"]` holds the rest (off screen, over budget)."""
    face = clean_face(data, assets, face_id)
    return face, face_report(face)


# ---------- which face shows now ----------
# The clock does this itself, offline. public/face-rules.js is the same thing in JS (for the "active now" line);
# tests/vectors/face_rules.json is run against both.

def face_matches(when: dict, now: dict) -> bool:
    """`now`: the clock's local {"year", "month", "day", "hour", "weekday"} (weekday: 0 = Sunday). All the
    constraints that are set must hold; a `when` with none never matches."""
    if when_is_empty(when):
        return False
    hours = when.get("hours")
    if hours:
        h, lo, hi = now["hour"], hours["from"], hours["to"]
        if not (lo <= h < hi if lo < hi else h >= lo or h < hi):
            return False
    if when.get("months") and now["month"] not in when["months"]:
        return False
    if when.get("weekdays") and now["weekday"] not in when["weekdays"]:
        return False
    if when.get("dates") and not any(d["month"] == now["month"] and d["day"] == now["day"] and d.get("year") in (None, 0, now["year"])
                                     for d in when["dates"]):
        return False
    return True


def pick_face(faces: list, default: str, pinned: str, now: dict):
    """The id of the face to show, or None for the clock's built-in one. `faces`: [{"id", "when"}] in priority order."""
    ids = {f["id"] for f in faces}
    if pinned and pinned in ids:
        return pinned
    for f in faces:
        if face_matches(f["when"], now):
            return f["id"]
    return default if default and default in ids else None


def local_fields(dt: datetime.datetime) -> dict:
    """The `now` argument for face_matches, from a datetime already in the clock's time zone."""
    return {"year": dt.year, "month": dt.month, "day": dt.day, "hour": dt.hour, "weekday": (dt.weekday() + 1) % 7}


# ---------- the face list in the state ----------

def face_list_entry(face: dict) -> dict:
    return {"id": face["id"], "rev": face["rev"]}


def put_face(state: dict, face: dict, after: str = "") -> None:
    """Add a face to the end of the list (or right after the face `after`), or update it in place (its place in
    the order stays)."""
    entry = face_list_entry(face)
    lst = state["faces"]["list"]
    for i, e in enumerate(lst):
        if e["id"] == face["id"]:
            lst[i] = entry
            return
    if len(lst) >= MAX_FACES:
        raise Invalid(f"That's a lot of faces! The limit is {MAX_FACES}.")
    at = next((i + 1 for i, e in enumerate(lst) if e["id"] == after), len(lst))
    lst.insert(at, entry)


def drop_face(state: dict, face_id: str) -> None:
    faces = state["faces"]
    faces["list"] = [e for e in faces["list"] if e["id"] != face_id]
    for key in ("default", "pinned"):
        if faces[key] == face_id:
            faces[key] = ""


def apply_face_settings(state: dict, data) -> None:
    """The face order, the fallback face and the pinned face. Any of the three may be left out."""
    data = _obj(data, "The face settings")
    faces = state["faces"]
    ids = [e["id"] for e in faces["list"]]
    if "order" in data:
        order = data["order"]
        if not isinstance(order, list) or sorted(order) != sorted(ids):
            raise Invalid("The order has to list every face once.")
        by_id = {e["id"]: e for e in faces["list"]}
        faces["list"] = [by_id[i] for i in order]
    for key in ("default", "pinned"):
        if key in data:
            v = data[key] or ""
            if v and v not in ids:
                raise Invalid("That face doesn't exist any more.")
            faces[key] = v


def unreferenced_assets(metas: dict, faces: list, now: float, grace: float = 3600) -> list:
    """Assets (hash -> meta with "at") that no face uses and that were uploaded more than `grace` seconds ago. The
    grace period keeps a just-uploaded sprite alive while the face that will use it is still being edited."""
    used = {h for f in faces for h in f.get("assets", {})}
    return [h for h, m in metas.items() if h not in used and now - m.get("at", 0) > grace]
