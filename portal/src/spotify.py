"""Spotify bits that need no network: URLs, token bookkeeping, and turning Spotify's JSON into what
the clock shows. Pure Python with no Workers imports, so it can be unit-tested with plain `python3`.

Two people connect their own Spotify accounts, called sides: "me" (shown on the left of the clock's
strip) and "her" (on the right). Per side the portal keeps, in KV key "spotify":

    {"me": {"refresh": "...", "access": "...", "expires": 1730003600}, "her": {...}}

and turns Spotify's "currently playing" answer into a "listening" dict:

    {"status": "playing" | "paused" | "off", "title": "...", "artist": "...", "art": "<40 hex>" | ""}
"""

import base64
import re
import unicodedata
from urllib.parse import urlencode

from model import SPOTIFY_TEXT_MAX

SIDES = ("me", "her")                       # left, right
SCOPE = "user-read-currently-playing"

ACCOUNTS_URL = "https://accounts.spotify.com"
API_URL = "https://api.spotify.com"

TEXT_MAX = SPOTIFY_TEXT_MAX
ART_PX = 64         # the clock wants the smallest cover: ~3 KB, and plenty for a ~40 px thumbnail
DETAIL_ART_MIN_PX, DETAIL_ART_MAX_PX = 224, 320  # half-size decode still covers the 112 px player tile
EXPIRY_MARGIN = 60  # refresh an access token this many seconds before it runs out

_ART_URL = re.compile(r"^https://i\.scdn\.co/image/([0-9a-f]{40})$")
_ART_ID = re.compile(r"^[0-9a-f]{40}$")

OFF = {"status": "off", "title": "", "artist": "", "art": ""}


def authorize_url(client_id: str, redirect_uri: str, state: str, accounts_url: str = ACCOUNTS_URL) -> str:
    query = urlencode({"response_type": "code", "client_id": client_id, "scope": SCOPE,
                       "redirect_uri": redirect_uri, "state": state, "show_dialog": "true"})
    return f"{accounts_url}/authorize?{query}"


def basic_auth(client_id: str, client_secret: str) -> str:
    return "Basic " + base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()


def token_record(data: dict, now: float, old: dict | None = None) -> dict | None:
    """A stored token record from Spotify's /api/token answer, or None if it has no access token.

    A refresh answer usually leaves out refresh_token (keep the old one); sometimes it rotates it.
    """
    if not isinstance(data, dict) or not isinstance(data.get("access_token"), str):
        return None
    refresh = data.get("refresh_token") or (old or {}).get("refresh")
    if not isinstance(refresh, str) or not refresh:
        return None
    try:
        lifetime = int(data.get("expires_in", 3600))
    except (TypeError, ValueError):
        lifetime = 3600
    return {"refresh": refresh, "access": data["access_token"], "expires": int(now) + lifetime}


def token_fresh(rec: dict | None, now: float) -> bool:
    return bool(rec and rec.get("access") and int(rec.get("expires", 0)) - EXPIRY_MARGIN > now)


# ---------- turning Spotify's JSON into clock text ----------

_PUNCT = str.maketrans({"‘": "'", "’": "'", "‚": "'", "“": '"', "”": '"',
                        "–": "-", "—": "-", "−": "-", "…": "...", " ": " ",
                        "×": "x", "·": "-", "•": "-"})


def ascii_text(text, limit: int = TEXT_MAX) -> str:
    """Text the clock's fonts can draw: accents dropped (é -> e), typographic punctuation made plain,
    anything else (kanji, emoji...) a single '?' per run. Shortened with '...' past `limit`."""
    if not isinstance(text, str):
        return ""
    text = unicodedata.normalize("NFKD", text.translate(_PUNCT))
    out = []
    for c in text:
        if unicodedata.combining(c):
            continue
        out.append(" " if c.isspace() else c if " " <= c <= "~" else "?")
    s = re.sub(r"\s+", " ", "".join(out)).strip()
    s = re.sub(r"\?{2,}", "?", s)
    if len(s) > limit:
        s = s[:limit - 3].rstrip() + "..."
    return s


def valid_art_id(art_id) -> bool:
    return isinstance(art_id, str) and bool(_ART_ID.match(art_id))


def pick_art(images, min_width: int = ART_PX, max_width: int | None = None) -> str:
    """The id of the smallest cover in the requested size range, or an empty string."""
    best = None
    for img in images if isinstance(images, list) else []:
        if not isinstance(img, dict) or not isinstance(img.get("url"), str):
            continue
        m = _ART_URL.match(img["url"])
        w = img.get("width")
        if not m or not isinstance(w, int) or w < min_width or (max_width is not None and w > max_width):
            continue
        if best is None or w < best[0]:
            best = (w, m.group(1))
    return best[1] if best else ""


def parse_playing(status: int, data, *, sampled_at_ms: int = 0) -> dict:
    """Spotify's GET /v1/me/player/currently-playing answer -> a "listening" dict."""
    if status != 200 or not isinstance(data, dict):  # 204: nothing is playing anywhere
        return dict(OFF)
    device = data.get("device")
    if isinstance(device, dict) and device.get("is_private_session") is True:
        return dict(OFF)
    item = data.get("item")
    if not isinstance(item, dict):  # an ad, or a private session: nothing worth showing
        return dict(OFF)

    if item.get("type") == "episode":
        show = item.get("show") if isinstance(item.get("show"), dict) else {}
        artist = show.get("publisher") or show.get("name") or ""
        collection = show.get("name")
        images = item.get("images") or show.get("images")
    else:
        artists = item.get("artists") if isinstance(item.get("artists"), list) else []
        artist = ", ".join(a["name"] for a in artists if isinstance(a, dict) and isinstance(a.get("name"), str))
        album = item.get("album") if isinstance(item.get("album"), dict) else {}
        collection = album.get("name")
        images = album.get("images")

    playing = data.get("is_playing") is True
    result = {"status": "playing" if playing else "paused",
              "title": ascii_text(item.get("name")) or "?", "artist": ascii_text(artist),
              "art": pick_art(images)}
    if name := ascii_text(collection):
        result["collection"] = name
    if art := pick_art(images, DETAIL_ART_MIN_PX, DETAIL_ART_MAX_PX):
        result["detail_art"] = art
    duration = item.get("duration_ms")
    if type(duration) is int and 0 < duration <= 0xFFFFFFFF:
        result["duration_ms"] = duration
        progress = data.get("progress_ms")
        if type(progress) is int and progress >= 0:
            result["elapsed_ms"] = min(progress, duration)
            if playing:
                result["sampled_at_ms"] = sampled_at_ms
    return result
