"""Data rules for the clock portal.

Pure Python with no Workers imports, so it can be unit-tested with plain `python3`.

Everything the clock needs lives in one JSON document (the "state"):

    {
      "version": 7,                                  # bumped on every change; the device ETag
      "message": {"text": "...", "at": 1730000000},  # or None
      "image":   {"id": "a1b2c3d4e5f6", "at": ..., "bytes": 31877},  # or None
      "alarms":  [ {alarm}, ... ]
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
"""

import datetime
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
    return {"version": 0, "message": None, "image": None, "alarms": []}


def normalize_state(state) -> dict:
    """Fill in anything missing from a stored state (older shapes, or nothing stored yet)."""
    base = empty_state()
    if isinstance(state, dict):
        base.update({k: state[k] for k in base if k in state})
    if not isinstance(base["alarms"], list):
        base["alarms"] = []
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

