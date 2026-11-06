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
