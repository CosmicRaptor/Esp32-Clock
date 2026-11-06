"""The clock's wire format: the state document <-> protobuf messages from proto/clock.proto.

Kept apart from model.py so the stored state stays plain JSON (what the admin page edits),
and only the device API speaks protobuf.
"""

import datetime

from google.protobuf.message import DecodeError

import clock_pb2 as pb
import model
import spotify
from model import Invalid

CONTENT_TYPE = "application/x-protobuf"


def days_mask(days: list) -> int:
    mask = 0
    for d in days:
        mask |= 1 << d
    return mask


def device_state(state: dict, listening: tuple | None = None) -> pb.DeviceState:
    """`listening`: the (left, right) "listening" dicts from spotify.py; None = nobody connected."""
    out = pb.DeviceState(version=state["version"])
    out.nowplaying.CopyFrom(now_playing(*(listening or (spotify.OFF, spotify.OFF))))
    if state.get("message"):
        out.note.text = state["message"]["text"]
        out.note.sent_at = state["message"]["at"]
    if state.get("image"):
        out.photo.id = state["image"]["id"]
        out.photo.sent_at = state["image"]["at"]
        out.photo.size = state["image"]["bytes"]
    faces = state.get("faces") or {}
    for entry in faces.get("list", []):
        out.faces.add(id=entry["id"], rev=entry["rev"])
    out.pinned_face = faces.get("pinned", "")
    out.default_face = faces.get("default", "")
    for a in state["alarms"]:
        if not a.get("enabled"):
            continue
        hour, minute = (int(x) for x in a["time"].split(":"))
        alarm = out.alarms.add(id=a["id"], hour=hour, minute=minute, days=days_mask(a["days"]),
                               label=a["label"], sound=a["sound"])
        if not a["days"]:
            d = datetime.date.fromisoformat(a["date"])
            alarm.date.year, alarm.date.month, alarm.date.day = d.year, d.month, d.day
    return out


_ALIGN = {"left": pb.TextSlot.LEFT, "center": pb.TextSlot.CENTER, "right": pb.TextSlot.RIGHT}
_FORMAT = {"long": pb.TextSlot.LONG, "short": pb.TextSlot.SHORT, "day_month": pb.TextSlot.DAY_MONTH,
           "weekday": pb.TextSlot.WEEKDAY, "numeric": pb.TextSlot.NUMERIC}
_MOTION = {"bob": pb.Motion.BOB, "sway": pb.Motion.SWAY, "path": pb.Motion.PATH, "fade": pb.Motion.FADE,
           "blink": pb.Motion.BLINK, "pulse": pb.Motion.PULSE, "frames": pb.Motion.FRAMES}
_KIND = {"fall": pb.Emitter.FALL, "rise": pb.Emitter.RISE, "twinkle": pb.Emitter.TWINKLE}


def _hex_color(c: str) -> int:
    return int(c[1:], 16)


def _fill_sprite(out: pb.SpriteRef, face: dict, h: str) -> None:
    meta = face["assets"][h]
    out.hash, out.w, out.h, out.size = h, meta["w"], meta["h"], meta["bytes"]


def _fill_slot(out: pb.TextSlot, slot: dict) -> None:
    out.x, out.y, out.size, out.color = slot["x"], slot["y"], slot["size"], _hex_color(slot["color"])
    out.align = _ALIGN[slot["align"]]
    if slot["shadow"]:
        out.has_shadow, out.shadow = True, _hex_color(slot["shadow"])
    if "format" in slot:
        out.format = _FORMAT[slot["format"]]


def _fill_motion(out: pb.Motion, m: dict) -> None:
    out.type, out.period_ms, out.phase_ms = _MOTION[m["type"]], m["period"], m["phase"]
    if m["type"] in ("bob", "sway"):
        out.amp = m["amp"]
    elif m["type"] == "path":
        out.dx, out.dy, out.loop = m["dx"], m["dy"], m["mode"] == "loop"
    elif m["type"] == "fade":
        for k in m["keys"]:
            out.keys.add(at=k["at"], pct=k["v"])
    elif m["type"] == "blink":
        out.on_ms = m["on"]
    elif m["type"] == "pulse":
        out.min_pct, out.max_pct = m["min"], m["max"]
    elif m["type"] == "frames":
        out.frames, out.fps = m["frames"], m["fps"]


def _fill_layer(out: pb.Layer, face: dict, layer: dict) -> None:
    _fill_sprite(out.sprite, face, layer["sprite"])
    out.x, out.y, out.copies = layer["x"], layer["y"], layer["copies"]
    out.stagger_x, out.stagger_y, out.stagger_phase_ms = layer["stagger"]["x"], layer["stagger"]["y"], layer["stagger"]["phase"]
    for m in layer["motion"]:
        _fill_motion(out.motion.add(), m)
    out.scale_pct = layer.get("scale", 100)
    if layer.get("scatter"):
        a = layer["scatter"]["area"]
        out.scatter, out.scatter_seed = True, layer["scatter"]["seed"]
        out.scatter_area.x, out.scatter_area.y, out.scatter_area.w, out.scatter_area.h = a["x"], a["y"], a["w"], a["h"]


def face_message(face: dict) -> pb.Face:
    """A stored face (model.clean_face's shape, with "rev") -> the Face message the clock downloads."""
    out = pb.Face(id=face["id"], rev=face["rev"])
    _fill_sprite(out.background, face, face["background"])
    _fill_slot(out.time, face["time"])
    if face["date"]:
        _fill_slot(out.date, face["date"])
    if face["icon"]:
        _fill_layer(out.icon, face, face["icon"])
    for layer in face["layers"]:
        _fill_layer(out.layers.add(), face, layer)
    for p in face["particles"]:
        e = out.emitters.add(kind=_KIND[p["kind"]], count=p["count"], speed_min=p["speed_min"], speed_max=p["speed_max"],
                             sway=p["sway"], period_ms=p["period"], phase_ms=p["phase"], front=p["depth"] == "front",
                             seed=p["seed"])
        for h in p["sprites"]:
            _fill_sprite(e.sprites.add(), face, h)
        e.area.x, e.area.y, e.area.w, e.area.h = p["area"]["x"], p["area"]["y"], p["area"]["w"], p["area"]["h"]
    np = face.get("nowplaying")
    if np:
        out.nowplaying.show = np["show"]
        out.nowplaying.x, out.nowplaying.y, out.nowplaying.w = np["x"], np["y"], np["w"]
        if np["bg"]:
            out.nowplaying.has_bg, out.nowplaying.bg = True, _hex_color(np["bg"])
        out.nowplaying.bg_pct = np["bg_pct"]
        out.nowplaying.title, out.nowplaying.artist = _hex_color(np["title"]), _hex_color(np["artist"])
    when = face["when"]
    out.when.SetInParent()
    if when["hours"]:
        out.when.hour_from, out.when.hour_to = when["hours"]["from"], when["hours"]["to"]
    for m in when["months"]:
        out.when.months |= 1 << (m - 1)
    out.when.days = days_mask(when["weekdays"])
    for d in when["dates"]:
        out.when.dates.add(year=d["year"] or 0, month=d["month"], day=d["day"])
    return out


_STATUS = {"playing": pb.Listener.PLAYING, "paused": pb.Listener.PAUSED}


def now_playing(left: dict, right: dict) -> pb.NowPlaying:
    """Two "listening" dicts (see spotify.py) -> the clock's NowPlaying message."""
    out = pb.NowPlaying()
    for field, listening in ((out.left, left), (out.right, right)):
        field.SetInParent()  # both sides are always present, even when "off"
        field.status = _STATUS.get(listening["status"], pb.Listener.OFF)
        if field.status != pb.Listener.OFF:
            field.title, field.artist, field.art_id = listening["title"], listening["artist"], listening["art"]
    return out


def parse_sound_list(body: bytes) -> list:
    msg = pb.SoundList()
    try:
        msg.ParseFromString(body)
    except DecodeError:
        raise Invalid("Expected a SoundList protobuf.") from None
    return model.clean_sounds(list(msg.names))
