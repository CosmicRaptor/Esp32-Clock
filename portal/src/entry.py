"""Bhoomi clock portal: a Python Cloudflare Worker.

Every request runs this code first (`run_worker_first` in wrangler.toml), so two gates cover the
whole site, the admin page's static files included:

  1. HTTPS only. Plain http gets a 301 to https (GET/HEAD) or a 403. Local `wrangler dev` is exempt.
  2. Cloudflare Access. Access sits in front of the site; this code re-checks the JWT it adds
     (Cf-Access-Jwt-Assertion) against the Access application's AUD tag and the team's keys, so the
     site stays closed even if Access is switched off. On when ACCESS_AUD and ACCESS_TEAM_DOMAIN are
     set (secrets, production only). People log in through Access; the clock uses a service token.

Behind those, static files in public/ are the admin page, and the rest is the JSON API:

  Admin (session cookie, from POST /api/login):
    POST   /api/login            {"password": "..."}
    POST   /api/logout
    GET    /api/state            -> {"state": {...}, "device": {...} | null}
    PUT    /api/message          {"text": "..."}     ("" clears it)
    PUT    /api/image            raw 320x240 baseline JPEG body
    DELETE /api/image
    GET    /api/image/<id>       the JPEG
    POST   /api/alarms           {alarm}  -> creates
    PUT    /api/alarms/<id>      {alarm}  -> replaces
    DELETE /api/alarms/<id>
    GET    /api/faces            -> {"faces": [face...], "settings": {default, pinned}, "budgets": {...}, "limits": {...}, ...}
    POST   /api/faces            {face}   -> creates (answers with the saved face)
    PUT    /api/faces/<id>       {face}   -> replaces
    DELETE /api/faces/<id>
    POST   /api/faces/<id>/duplicate
    PUT    /api/face-settings    {"order": [ids], "default": id|"", "pinned": id|""}   (any of the three)
    POST   /api/face-check       {face}   -> {"ok", "errors", "budget"}: validate a draft without saving it
    PUT    /api/assets/bg        raw 320x240 baseline JPEG   -> {"asset": {hash, kind, w, h, bytes}}
    PUT    /api/assets/sprite    raw LVGL RGB565A8 image     -> {"asset": ...}
    PUT    /api/assets/<hash>.png    the sprite's PNG, for the preview
    GET    /api/assets/<hash>.png|.jpg
    GET    /api/weather/location -> {location, weather}: selected city and cached reading
    PUT    /api/weather/location    {location: {name, latitude, longitude} | null}
    GET    /api/weather/cities?q=... -> geocoding matches
    GET    /api/spotify          -> {"configured": bool, "sides": [{side, name, connected, playing}]}
    GET    /api/spotify/login?side=me|her    302 to Spotify's consent page
    DELETE /api/spotify/<side>   disconnect that person

  Spotify sends the browser back to (no cookie: it's a cross-site redirect, so a signed `state` vouches):
    GET    /api/spotify/callback?code=...&state=...   302 to /?spotify=ok (or denied / failed / bad_state)

  Clock (Authorization: Bearer <DEVICE_TOKEN>), protobuf messages from proto/clock.proto:
    GET    /api/device/state?tz=Asia/Kolkata&fw=0.4.0&rssi=-71
           -> 200 DeviceState (application/x-protobuf), or 304 if If-None-Match matches.
              Add &format=json to read it with curl while debugging.
    GET    /api/device/image/<id>
    PUT    /api/device/sounds    body: SoundList
    GET    /api/device/art/<id>     64x64 baseline JPEG cover, proxied from Spotify's CDN (id = Listener.art_id)
    GET    /api/device/face/<id>    a Face (ETag = its rev; &format=json to read it)
    GET    /api/device/asset/<hash> a face's background JPEG or sprite file (content-addressed: never changes)
    GET    /api/device/management  update approval + diagnostic interval (Management protobuf)
    GET    /api/device/firmware/<sha256> approved/content-addressed application firmware
    PUT    /api/device/diagnostics allowlisted health snapshot; at most one stored per five minutes
    GET    /api/management         admin release/settings/diagnostics overview
    PUT    /api/management/settings  diagnostic interval
    PUT    /api/management/firmware  upload candidate application binary
    POST   /api/management/install  approve exact candidate; DELETE cancels future discovery

  /api/device/state is the clock's one heartbeat, polled every ~30 s. Besides the portal's own state it carries
  `nowplaying` (asked from Spotify on each call), and its ETag covers both, so a new song is a new 200.

Storage is one KV namespace (binding CLOCK):
    "state"       the JSON document described in model.py
    "device"      what the clock last reported (seen, tz, fw, rssi, sounds)
    "img:<id>"    the current photo, base64
    "face:<id>"   a clock face (JSON, see model.py); the state holds only the ids, revs, order, default and pinned
    "asset:<hash>"      a face's background JPEG or sprite file, base64; the hash is of the bytes
    "assetpng:<hash>"   the sprite's PNG, base64 (for the admin preview only)
    "assetmeta:<hash>"  {"kind", "w", "h", "bytes", "at"}; one key per asset, so uploads never race each other
    "spotify"     per side ("me"/"her"): refresh token + cached access token (see spotify.py); never sent to any client
"""

import asyncio
import base64
import hashlib
import json
import re
import time
from urllib.parse import parse_qs, urlencode, urlparse

from google.protobuf import json_format
from js import Object, crypto
from pyodide.ffi import to_js
from workers import Response, WorkerEntrypoint, fetch

import auth
import model
import spotify
import wire
import weather
import management
import mindfulness
import mood_video
import clock_pb2 as pb
from model import Invalid

FACE_PREFIX, ASSET_PREFIX, PNG_PREFIX, META_PREFIX = "face:", "asset:", "assetpng:", "assetmeta:"
STATE_KEY = "state"
DEVICE_KEY = "device"
SPOTIFY_KEY = "spotify"
SEEN_WRITE_EVERY = 600  # s; KV's free tier allows 1,000 writes/day, so don't write on every poll

# The last good Spotify answer per side, kept in this isolate's memory. Used when Spotify rate-limits
# or hiccups, so the strip on the clock doesn't blink off. Never written anywhere.
_last_listening: dict = {}

# What this isolate last saw of the clock's check-in, so the 30 s polls don't each cost a KV read.
_checkin: dict = {"at": 0}

# Cloudflare Access signing keys (kid -> WebCrypto key), fetched from the team's certs URL. Access
# rotates keys every few weeks and publishes the next one early, so an hour's cache is plenty; an
# unknown kid triggers a refetch, at most once a minute.
_access_keys: dict = {"keys": {}, "at": 0}
ACCESS_KEYS_TTL = 3600
ACCESS_KEYS_RETRY = 60
RS256 = {"name": "RSASSA-PKCS1-v1_5", "hash": "SHA-256"}


def js_obj(value):
    return to_js(value, dict_converter=Object.fromEntries)


IMAGE_SIZE = (model.IMAGE_W, model.IMAGE_H)


class Upstream(Exception):
    """Spotify (or its CDN) couldn't be reached or answered with something unusable."""


class HttpError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


def missing(value) -> bool:
    # KV returns JS null for a missing key, which arrives as pyodide.ffi.JsNull, not None
    return value is None or type(value).__name__ == "JsNull"


def json_response(data, status: int = 200, headers: dict | None = None) -> Response:
    h = {"content-type": "application/json; charset=utf-8", "cache-control": "no-store"}
    h.update(headers or {})
    return Response(json.dumps(data, separators=(",", ":")), status=status, headers=h)


def jpeg_response(data: bytes) -> Response:
    # image ids are random and never reused, so a photo at a given URL never changes
    return Response(data, headers={"content-type": "image/jpeg", "content-length": str(len(data)),
                                   "cache-control": "private, max-age=31536000, immutable"})


def binary_response(data: bytes, content_type: str) -> Response:
    # assets are named by their content, so the bytes at a URL never change
    return Response(data, headers={"content-type": content_type, "content-length": str(len(data)),
                                   "cache-control": "private, max-age=31536000, immutable"})


async def read_json(request):
    try:
        return json.loads(await request.text())
    except ValueError:
        raise Invalid("Expected a JSON body.") from None


class Default(WorkerEntrypoint):
    # ---------- storage ----------

    async def kv_json(self, key):
        raw = await self.env.CLOCK.get(key)
        return None if missing(raw) else json.loads(raw)

    async def load_state(self) -> dict:
        return model.normalize_state(await self.kv_json(STATE_KEY))

    async def save_state(self, state: dict) -> dict:
        state["version"] = int(state.get("version", 0)) + 1
        await self.env.CLOCK.put(STATE_KEY, json.dumps(state))
        return state

    def secret(self, name: str) -> str:
        value = getattr(self.env, name, None)
        return "" if missing(value) else str(value)

    # ---------- entry ----------

    async def access_keys(self, team: str, kid: str) -> dict:
        now = time.time()
        cache = _access_keys
        fresh = now - cache["at"] < ACCESS_KEYS_TTL
        if kid in cache["keys"] and fresh:
            return cache["keys"]
        if fresh and now - cache["at"] < ACCESS_KEYS_RETRY:
            return cache["keys"]  # just fetched: an unknown kid is a forged token, not a rotation
        try:
            resp = await fetch(team.rstrip("/") + "/cdn-cgi/access/certs")
            jwks = json.loads(await resp.text()) if resp.status == 200 else None
        except Exception:
            jwks = None
        if not isinstance(jwks, dict) or not isinstance(jwks.get("keys"), list):
            if cache["keys"]:
                return cache["keys"]  # keep the last good keys if Access's endpoint hiccups
            raise HttpError(503, "Couldn't reach Cloudflare Access to check the login.")
        keys = {}
        for jwk in jwks["keys"]:
            if isinstance(jwk, dict) and jwk.get("kty") == "RSA" and isinstance(jwk.get("kid"), str):
                pub = {k: jwk[k] for k in ("kty", "n", "e") if k in jwk}
                keys[jwk["kid"]] = await crypto.subtle.importKey("jwk", js_obj(pub), js_obj(RS256), False,
                                                                 to_js(["verify"]))
        cache["keys"], cache["at"] = keys, now
        return keys

    async def check_access(self, request) -> None:
        """Raises HttpError(403) unless the request carries a valid Access JWT for this application."""
        aud, team = self.secret("ACCESS_AUD"), self.secret("ACCESS_TEAM_DOMAIN")
        if not (aud and team):
            return  # Access isn't configured for this Worker (local dev)
        try:
            header, claims, signed, signature = auth.split_jwt(request.headers.get(auth.ACCESS_HEADER))
            key = (await self.access_keys(team, header["kid"])).get(header["kid"])
            if key is None:
                raise auth.BadJwt("unknown signing key")
            if not await crypto.subtle.verify(js_obj(RS256), key, to_js(signature), to_js(signed)):
                raise auth.BadJwt("bad signature")
            auth.check_access_claims(claims, aud, team)
        except auth.BadJwt as e:
            raise HttpError(403, f"Cloudflare Access login required ({e}).") from None

    async def fetch(self, request):
        url = urlparse(request.url)
        path = url.path.rstrip("/") or "/"
        try:
            if url.scheme != "https" and not auth.is_local(url.hostname):
                if request.method in ("GET", "HEAD"):
                    return Response(None, status=301, headers={"location": auth.https_url(request.url),
                                                               "cache-control": "no-store"})
                raise HttpError(403, "HTTPS only.")
            await self.check_access(request)
            if not path.startswith("/api/"):
                return await self.env.ASSETS.fetch(request)  # the admin page (public/)
            if path.startswith("/api/device/"):
                return await self.device_api(request, path, url)
            if path == "/api/spotify/callback" and request.method == "GET":
                return await self.spotify_callback(url)  # before the cookie check: see auth.make_oauth_state
            return await self.admin_api(request, path, url)
        except Invalid as e:
            return json_response({"error": str(e)}, 400)
        except HttpError as e:
            return json_response({"error": str(e)}, e.status)

    # ---------- admin ----------

    async def admin_api(self, request, path: str, url):
        method = request.method
        secure = url.scheme == "https"
        password = self.secret("ADMIN_PASSWORD")

        if path == "/api/login" and method == "POST":
            if not password:
                raise HttpError(503, "ADMIN_PASSWORD isn't set on the server yet.")
            body = await read_json(request)
            given = body.get("password") if isinstance(body, dict) else None
            if not auth.same_secret(password, given if isinstance(given, str) else None):
                await asyncio.sleep(1)  # a little friction for guessing
                raise HttpError(401, "That's not the password.")
            token = auth.make_session(password)
            return json_response({"ok": True}, headers={"set-cookie": auth.session_cookie(token, secure)})

        if path == "/api/logout" and method == "POST":
            return json_response({"ok": True}, headers={"set-cookie": auth.clear_cookie(secure)})

        if not auth.check_session(password, auth.read_cookie(request.headers.get("cookie"))):
            raise HttpError(401, "Please log in.")

        if path.startswith("/api/mood-note/") and method in ("GET", "PUT"):
            mood = path.removeprefix("/api/mood-note/")
            if mood not in ("1", "2", "3", "4", "5"):
                raise Invalid("Choose a mood from 1 to 5.")
            key = "mood-note:" + mood
            if method == "GET":
                return json_response({"note": await self.kv_json(key) or ""})
            body = await read_json(request)
            note = mindfulness.clean_note(body.get("note") if isinstance(body, dict) else None)
            await self.env.CLOCK.put(key, json.dumps(note))
            return json_response({"note": note})

        if path.startswith("/api/mood-video/") and method in ("GET", "PUT"):
            mood = path.removeprefix("/api/mood-video/")
            if mood not in ("1", "2", "3", "4", "5"):
                raise Invalid("Choose a mood from 1 to 5.")
            key = "mood-video:" + mood
            if method == "GET":
                return json_response({"video": await self.kv_json(key)})
            data = bytes(await request.bytes())
            meta = mood_video.validate(data)
            old = await self.kv_json(key)
            await self.env.CLOCK.put("mood-video-data:" + mood + ":" + meta["sha256"], base64.b64encode(data).decode())
            await self.env.CLOCK.put(key, json.dumps(meta))
            if old and old.get("sha256") != meta["sha256"]:
                await self.env.CLOCK.delete("mood-video-data:" + mood + ":" + old["sha256"])
            return json_response({"video": meta})

        if path == "/api/moods" and method == "GET":
            listing = await self.env.CLOCK.list(js_obj({"prefix": "mood:", "limit": 1000}))
            if hasattr(listing, "to_py"):
                listing = listing.to_py()
            records = await asyncio.gather(*(self.kv_json(k["name"]) for k in listing["keys"]))
            cutoff = int(time.time()) - mindfulness.RETENTION
            return json_response({"moods": sorted((r for r in records if r and r["at"] > cutoff), key=lambda r: r["at"], reverse=True)})

        if path == "/api/management" and method == "GET":
            return json_response({"settings": await self.kv_json("management:settings") or {"interval": 3600},
                                  "release": await self.kv_json("management:release"),
                                  "approved": await self.kv_json("management:approved"),
                                  "diagnostics": await self.kv_json("management:diagnostics")})
        if path == "/api/management/settings" and method == "PUT":
            cfg = management.settings(await read_json(request))
            await self.env.CLOCK.put("management:settings", json.dumps(cfg))
            return json_response(cfg)
        if path == "/api/management/firmware" and method == "PUT":
            data = bytes(await request.bytes())
            release = management.firmware(data)
            # Content-addressed: publishing a new candidate never changes an approved binary.
            await self.env.CLOCK.put("firmware:" + release["release"], base64.b64encode(data).decode())
            await self.env.CLOCK.put("management:release", json.dumps(release))
            return json_response(release)
        if path == "/api/management/install" and method == "POST":
            requested = await read_json(request)
            release = await self.kv_json("management:release")
            if not isinstance(requested, dict) or not release or requested.get("release") != release["release"]:
                raise Invalid("Refresh and select the uploaded release.")
            await self.env.CLOCK.put("management:approved", json.dumps(release))
            return json_response(release)
        if path == "/api/management/install" and method == "DELETE":
            await self.env.CLOCK.delete("management:approved")
            return json_response({"ok": True})

        if path == "/api/weather/cities" and method == "GET":
            q = parse_qs(url.query).get("q", [""])[0].strip()
            if not 2 <= len(q) <= 80:
                raise Invalid("Enter at least two characters of a city name.")
            target = "https://geocoding-api.open-meteo.com/v1/search?" + urlencode({"name": q, "count": 8, "language": "en", "format": "json"})
            try:
                data = await asyncio.wait_for(self.weather_json(target), timeout=6)
            except Exception:
                raise HttpError(502, "City search is unavailable. Try again.") from None
            return json_response({"results": data.get("results", [])})

        if path == "/api/weather/location":
            if method == "GET":
                loc = await self.kv_json("weather:location")
                rec = await self.kv_json("weather:cache")
                reading = rec["reading"] if weather.usable(rec, loc, time.time()) else None
                return json_response({"location": loc, "weather": reading})
            if method == "PUT":
                body = await read_json(request)
                if not isinstance(body, dict) or "location" not in body:
                    raise Invalid("Expected a location, or null to turn weather off.")
                loc = weather.location(body["location"])
                await self.env.CLOCK.put("weather:location", json.dumps(loc))
                return json_response({"location": loc})

        if path == "/api/state" and method == "GET":
            return json_response({"state": await self.load_state(),
                                  "device": await self.kv_json(DEVICE_KEY), "now": int(time.time())})

        if path == "/api/message" and method == "PUT":
            body = await read_json(request)
            text = model.clean_message(body.get("text") if isinstance(body, dict) else None)
            state = await self.load_state()
            state["message"] = {"text": text, "at": int(time.time())} if text else None
            return json_response({"state": await self.save_state(state)})

        if path == "/api/image" and method == "PUT":
            data = await request.bytes()
            model.check_clock_photo(data)
            image_id = model.new_id()
            # photo first, then the state pointing at it, so the clock never sees an id it can't fetch
            await self.env.CLOCK.put("img:" + image_id, base64.b64encode(data).decode())
            state = await self.load_state()
            old = state.get("image")
            state["image"] = {"id": image_id, "at": int(time.time()), "bytes": len(data)}
            state = await self.save_state(state)
            if old:
                await self.env.CLOCK.delete("img:" + old["id"])
            return json_response({"state": state})

        if path == "/api/image" and method == "DELETE":
            state = await self.load_state()
            old = state.get("image")
            state["image"] = None
            state = await self.save_state(state)
            if old:
                await self.env.CLOCK.delete("img:" + old["id"])
            return json_response({"state": state})

        if path.startswith("/api/image/") and method == "GET":
            return await self.image(path.removeprefix("/api/image/"))

        if path == "/api/alarms" and method == "POST":
            state = await self.load_state()
            if len(state["alarms"]) >= model.MAX_ALARMS:
                raise Invalid(f"That's a lot of alarms! The limit is {model.MAX_ALARMS}.")
            alarm = model.clean_alarm(await read_json(request), model.new_id())
            state["alarms"] = model.sort_alarms(state["alarms"] + [alarm])
            return json_response({"state": await self.save_state(state), "alarm": alarm})

        if path.startswith("/api/alarms/") and method in ("PUT", "DELETE"):
            alarm_id = path.removeprefix("/api/alarms/")
            state = await self.load_state()
            rest = [a for a in state["alarms"] if a["id"] != alarm_id]
            if len(rest) == len(state["alarms"]):
                raise HttpError(404, "That alarm doesn't exist any more.")
            if method == "PUT":
                rest.append(model.clean_alarm(await read_json(request), alarm_id))
            state["alarms"] = model.sort_alarms(rest)
            return json_response({"state": await self.save_state(state)})

        if path == "/api/faces" and method == "GET":
            return json_response(await self.faces_overview())

        if path == "/api/faces" and method == "POST":
            return json_response(await self.save_face(model.new_id(), await read_json(request)))

        if path.startswith("/api/faces/") and method in ("PUT", "DELETE", "POST"):
            rest = path.removeprefix("/api/faces/").split("/")
            if not model.valid_id(rest[0]) or len(rest) > 2:
                raise HttpError(404, "Not found.")
            if method == "PUT" and len(rest) == 1:
                if not await self.load_face(rest[0]):
                    raise HttpError(404, "That face doesn't exist any more.")
                return json_response(await self.save_face(rest[0], await read_json(request)))
            if method == "DELETE" and len(rest) == 1:
                return json_response(await self.delete_face(rest[0]))
            if method == "POST" and rest[1:] == ["duplicate"]:
                return json_response(await self.duplicate_face(rest[0]))
            raise HttpError(404, "Not found.")

        if path == "/api/face-settings" and method == "PUT":
            state = await self.load_state()
            model.apply_face_settings(state, await read_json(request))
            return json_response({"state": await self.save_state(state)})

        if path == "/api/face-check" and method == "POST":
            data = await read_json(request)
            if isinstance(data, dict) and not str(data.get("name") or "").strip():
                data = {**data, "name": "Untitled"}  # a missing name is for Save to complain about, not the meter
            try:
                _, report = model.check_face(data, await self.asset_metas(model.draft_hashes(data)), "0" * 12)
            except Invalid as err:
                return json_response({"ok": False, "errors": [str(err)], "budget": None})
            return json_response({"ok": not report["errors"], **report})

        if path == "/api/assets/bg" and method == "PUT":
            data = bytes(await request.bytes())
            model.check_clock_photo(data)
            return json_response({"asset": await self.store_asset(data, "bg", IMAGE_SIZE)})

        if path == "/api/assets/sprite" and method == "PUT":
            data = bytes(await request.bytes())
            return json_response({"asset": await self.store_asset(data, "sprite", model.check_sprite_file(data))})

        if path.startswith("/api/assets/") and method in ("GET", "PUT"):
            name, _, ext = path.removeprefix("/api/assets/").partition(".")
            if not model.valid_hash(name) or ext not in ("png", "jpg"):
                raise HttpError(404, "Not found.")
            if method == "PUT" and ext == "png":
                return json_response({"asset": await self.store_png(name, bytes(await request.bytes()))})
            if method == "GET":
                return await self.asset_for_page(name, ext)
            raise HttpError(404, "Not found.")

        if path == "/api/spotify" and method == "GET":
            return json_response(await self.spotify_overview())

        if path == "/api/spotify/login" and method == "GET":
            q = {k: v[0] for k, v in parse_qs(url.query).items()}
            side = q.get("side")
            if side not in spotify.SIDES:
                raise Invalid("Which side is this for?")
            client_id = self.secret("SPOTIFY_CLIENT_ID")
            if not client_id or not self.secret("SPOTIFY_CLIENT_SECRET"):
                raise HttpError(503, "SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET aren't set on the server yet.")
            state = auth.make_oauth_state(password, side, model.new_id())
            target = spotify.authorize_url(client_id, self.redirect_uri(url), state, self.accounts_url())
            return Response(None, status=302, headers={"location": target, "cache-control": "no-store"})

        if path.startswith("/api/spotify/") and method == "DELETE":
            side = path.removeprefix("/api/spotify/")
            if side not in spotify.SIDES:
                raise HttpError(404, "Not found.")
            await self.store_tokens(side, None)
            _last_listening.pop(side, None)
            return json_response(await self.spotify_overview())

        raise HttpError(404, "Not found.")

    async def image(self, image_id: str):
        if not image_id.isalnum():
            raise HttpError(404, "No such photo.")
        raw = await self.env.CLOCK.get("img:" + image_id)
        if missing(raw):
            # can briefly happen right after a change: KV takes up to ~60 s to reach every location
            raise HttpError(404, "No such photo.")
        return jpeg_response(base64.b64decode(raw))

    # ---------- faces ----------

    async def load_face(self, face_id: str):
        return await self.kv_json(FACE_PREFIX + face_id)

    async def asset_metas(self, hashes) -> dict:
        """hash -> meta for those assets that exist."""
        hashes = sorted(hashes)
        metas = await asyncio.gather(*(self.kv_json(META_PREFIX + h) for h in hashes))
        return {h: m for h, m in zip(hashes, metas) if m}

    async def faces_overview(self) -> dict:
        state = await self.load_state()
        docs = await asyncio.gather(*(self.load_face(e["id"]) for e in state["faces"]["list"]))
        faces = [f for f in docs if f]
        return {"faces": faces, "settings": {"default": state["faces"]["default"], "pinned": state["faces"]["pinned"]},
                "budgets": {f["id"]: model.face_report(f)["budget"] for f in faces},
                "limits": model.face_limits(), "version": state["version"],
                "device": await self.kv_json(DEVICE_KEY), "now": int(time.time())}

    async def save_face(self, face_id: str, data, after: str = "") -> dict:
        """Check a face draft, store it, and point the state at it. The clock's ETag moves, so it re-fetches."""
        face, report = model.check_face(data, await self.asset_metas(model.draft_hashes(data)), face_id)
        if report["errors"]:
            raise Invalid(" ".join(report["errors"][:2]))
        face["rev"] = model.face_rev(face)
        state = await self.load_state()
        before = next((e["rev"] for e in state["faces"]["list"] if e["id"] == face_id), None)
        if before == face["rev"]:
            return {"face": face, "state": state, "budget": report["budget"]}  # nothing changed: leave the clock alone
        model.put_face(state, face, after)  # may refuse (too many faces) before anything is written
        # the face first, then the state pointing at it, so the clock never sees a face it can't fetch
        await self.env.CLOCK.put(FACE_PREFIX + face_id, json.dumps(face))
        state = await self.save_state(state)
        await self.collect_assets()
        return {"face": face, "state": state, "budget": report["budget"]}

    async def duplicate_face(self, face_id: str) -> dict:
        face = await self.load_face(face_id)
        if not face:
            raise HttpError(404, "That face doesn't exist any more.")
        face["name"] = (face["name"] + " copy")[:model.FACE_NAME_MAX].strip()
        return await self.save_face(model.new_id(), face, after=face_id)

    async def delete_face(self, face_id: str) -> dict:
        state = await self.load_state()
        if not any(e["id"] == face_id for e in state["faces"]["list"]):
            raise HttpError(404, "That face doesn't exist any more.")
        model.drop_face(state, face_id)
        state = await self.save_state(state)   # unlist it first, then delete its data
        await self.env.CLOCK.delete(FACE_PREFIX + face_id)
        await self.collect_assets()
        return {"state": state}

    async def store_asset(self, data: bytes, kind: str, size: tuple) -> dict:
        h = model.asset_hash(data)
        meta = {"kind": kind, "w": size[0], "h": size[1], "bytes": len(data), "at": int(time.time())}
        await self.env.CLOCK.put(ASSET_PREFIX + h, base64.b64encode(data).decode())
        await self.env.CLOCK.put(META_PREFIX + h, json.dumps(meta))   # last, and again for a re-upload: it restarts the grace period
        return {"hash": h, **{k: meta[k] for k in ("kind", "w", "h", "bytes")}}

    async def store_png(self, h: str, data: bytes) -> dict:
        meta = await self.kv_json(META_PREFIX + h)
        if not meta or meta["kind"] != "sprite":
            raise HttpError(404, "Upload the sprite first.")
        if model.png_size(data) != (meta["w"], meta["h"]):
            raise Invalid("The PNG isn't the same size as the sprite.")
        await self.env.CLOCK.put(PNG_PREFIX + h, base64.b64encode(data).decode())
        return {"hash": h, **{k: meta[k] for k in ("kind", "w", "h", "bytes")}}

    async def asset_for_page(self, h: str, ext: str) -> Response:
        raw = await self.env.CLOCK.get((PNG_PREFIX if ext == "png" else ASSET_PREFIX) + h)
        if missing(raw):
            raise HttpError(404, "No such image.")
        return binary_response(base64.b64decode(raw), "image/png" if ext == "png" else "image/jpeg")

    async def collect_assets(self) -> None:
        """Delete the assets no face uses any more (after a grace period, see model.unreferenced_assets). Housekeeping:
        it must never make a save fail."""
        try:
            listing = await self.env.CLOCK.list(js_obj({"prefix": META_PREFIX}))
            if hasattr(listing, "to_py"):
                listing = listing.to_py()
            hashes = [k["name"].removeprefix(META_PREFIX) for k in listing["keys"]]  # (not listing.keys: that's a method)
            metas, state = await asyncio.gather(self.asset_metas(hashes), self.load_state())
            docs = await asyncio.gather(*(self.load_face(e["id"]) for e in state["faces"]["list"]))
            for h in model.unreferenced_assets(metas, [f for f in docs if f], time.time()):
                await asyncio.gather(*(self.env.CLOCK.delete(prefix + h) for prefix in (ASSET_PREFIX, PNG_PREFIX, META_PREFIX)))
        except Exception as e:
            print("collect_assets failed:", repr(e))  # shows in the Worker's logs

    # ---------- clock ----------

    async def device_api(self, request, path: str, url):
        token = self.secret("DEVICE_TOKEN")
        if not token:
            raise HttpError(503, "DEVICE_TOKEN isn't set on the server yet.")
        if not auth.same_secret(token, auth.bearer(request.headers.get("authorization"))):
            raise HttpError(401, "Bad device token.")
        method = request.method

        if path == "/api/device/mood-notes" and method == "GET":
            notes = await asyncio.gather(*(self.kv_json("mood-note:" + str(i)) for i in range(1, 6)))
            text = "".join(str(i) + " " + base64.b64encode((note or "").encode()).decode() + "\n" for i, note in enumerate(notes, 1))
            return Response(text, headers={"content-type": "text/plain", "cache-control": "no-store"})

        if path == "/api/device/mood-videos" and method == "GET":
            entries = await asyncio.gather(*(self.kv_json("mood-video:" + str(i)) for i in range(1, 6)))
            text = "".join(f"{i} {e['sha256']} {e['size']}\n" for i, e in enumerate(entries, 1) if e)
            return Response(text, headers={"content-type": "text/plain", "cache-control": "no-store"})
        if path.startswith("/api/device/mood-video/") and method == "GET":
            parts = path.removeprefix("/api/device/mood-video/").split("/")
            if len(parts) != 2 or parts[0] not in ("1", "2", "3", "4", "5") or not re.fullmatch(r"[0-9a-f]{64}", parts[1]):
                raise HttpError(404, "Unknown video.")
            raw = await self.env.CLOCK.get("mood-video-data:" + parts[0] + ":" + parts[1])
            if missing(raw):
                raise HttpError(404, "Video is still propagating; retry later.")
            return binary_response(base64.b64decode(str(raw)), "application/octet-stream")

        if path == "/api/device/mood" and method == "PUT":
            now = int(time.time())
            mood = mindfulness.clean(await read_json(request), now)
            expires = mood["at"] + mindfulness.RETENTION
            if expires > now + 60:
                await self.env.CLOCK.put("mood:" + str(mood["at"]), json.dumps(mood), js_obj({"expiration": expires}))
            return json_response({"ok": True})

        if path == "/api/device/management" and method == "GET":
            cfg = await self.kv_json("management:settings") or {"interval": 3600}
            release = await self.kv_json("management:approved") or {}
            out = pb.Management(diagnostic_interval=cfg["interval"], release=release.get("release", ""), size=release.get("size", 0))
            return Response(out.SerializeToString(), headers={"content-type": wire.CONTENT_TYPE, "cache-control": "no-store"})
        if path.startswith("/api/device/firmware/") and method == "GET":
            h = path.removeprefix("/api/device/firmware/")
            if not re.fullmatch(r"[0-9a-f]{64}", h):
                raise HttpError(404, "Unknown firmware.")
            raw = await self.env.CLOCK.get("firmware:" + h)
            if missing(raw):
                raise HttpError(404, "Firmware is still propagating; retry later.")
            return binary_response(base64.b64decode(str(raw)), "application/octet-stream")
        if path == "/api/device/diagnostics" and method == "PUT":
            data = management.report(await read_json(request))
            old = await self.kv_json("management:diagnostics")
            now = int(time.time())
            if not management.report_due(old, data, now):
                return json_response({"retry": 300}, status=429)
            data["at"] = now
            await self.env.CLOCK.put("management:diagnostics", json.dumps(data))
            return json_response({"ok": True})

        if path == "/api/device/weather/location" and method == "PUT":
            q = {k: v[0] for k, v in parse_qs(url.query).items()}
            try:
                loc = weather.location({"name": q.get("name"), "latitude": float(q.get("latitude", "")),
                                        "longitude": float(q.get("longitude", ""))}) if q.get("name") else None
            except ValueError:
                raise Invalid("Invalid weather coordinates.") from None
            await self.env.CLOCK.put("weather:location", json.dumps(loc))
            return Response(None, status=204)

        if path == "/api/device/weather" and method == "GET":
            loc = await self.kv_json("weather:location")
            reading = None
            if loc:
                async def store(record):
                    await self.env.CLOCK.put("weather:cache", json.dumps(record))
                reading = await weather.current(loc, await self.kv_json("weather:cache"), int(time.time()), self.weather_json, store)
            msg = wire.pb.Weather(city=loc["name"] if loc else "", available=bool(reading))
            if reading:
                msg.temperature_c, msg.code, msg.is_day, msg.observed_at = (reading[k] for k in ("temperature_c", "code", "is_day", "observed_at"))
            if "format=json" in url.query:
                return json_response({"location": loc, "weather": reading})
            return Response(msg.SerializeToString(), headers={"content-type": wire.CONTENT_TYPE, "cache-control": "no-store"})

        if path == "/api/device/state" and method == "GET":
            q = {k: v[0] for k, v in parse_qs(url.query).items()}
            await self.note_check_in(q)
            state, listening = await asyncio.gather(self.load_state(), self.listening_both())
            msg = wire.device_state(state, listening)
            # the ETag moves with the portal's version AND with what is playing
            etag = '"' + str(state["version"]) + "-" + hashlib.sha1(msg.nowplaying.SerializeToString()).hexdigest()[:8] + '"'
            if request.headers.get("if-none-match") == etag:
                return Response(None, status=304, headers={"etag": etag, "cache-control": "no-store"})
            if q.get("format") == "json":  # for humans with curl
                return json_response(json_format.MessageToDict(msg), headers={"etag": etag})
            return Response(msg.SerializeToString(), headers={
                "content-type": wire.CONTENT_TYPE, "etag": etag, "cache-control": "no-store"})

        if path.startswith("/api/device/image/") and method == "GET":
            return await self.image(path.removeprefix("/api/device/image/"))

        if path.startswith("/api/device/face/") and method == "GET":
            face_id = path.removeprefix("/api/device/face/")
            face = await self.load_face(face_id) if model.valid_id(face_id) else None
            if not face:
                raise HttpError(404, "No such face.")
            etag = '"' + face["rev"] + '"'
            if request.headers.get("if-none-match") == etag:
                return Response(None, status=304, headers={"etag": etag, "cache-control": "no-store"})
            msg = wire.face_message(face)
            if "format=json" in (url.query or ""):  # for humans with curl
                return json_response(json_format.MessageToDict(msg), headers={"etag": etag})
            return Response(msg.SerializeToString(), headers={"content-type": wire.CONTENT_TYPE, "etag": etag,
                                                              "cache-control": "no-store"})

        if path.startswith("/api/device/asset/") and method == "GET":
            h = path.removeprefix("/api/device/asset/")
            raw = await self.env.CLOCK.get(ASSET_PREFIX + h) if model.valid_hash(h) else None
            if raw is None or missing(raw):
                # can briefly happen right after a change: KV takes up to ~60 s to reach every location
                raise HttpError(404, "No such asset.")
            data = base64.b64decode(raw)
            return binary_response(data, "image/jpeg" if data[:2] == b"\xff\xd8" else "application/octet-stream")

        if path == "/api/device/nowplaying" and method == "GET":
            left, right = await asyncio.gather(*(self.listening(side) for side in spotify.SIDES))
            body = wire.now_playing(left, right).SerializeToString()
            etag = '"' + hashlib.sha1(body).hexdigest()[:12] + '"'
            if request.headers.get("if-none-match") == etag:
                return Response(None, status=304, headers={"etag": etag, "cache-control": "no-store"})
            if url.query and "format=json" in url.query:  # for humans with curl
                return json_response(json_format.MessageToDict(wire.pb.NowPlaying.FromString(body)),
                                     headers={"etag": etag})
            return Response(body, headers={"content-type": wire.CONTENT_TYPE, "etag": etag, "cache-control": "no-store"})

        if path.startswith("/api/device/art/") and method == "GET":
            return await self.cover(path.removeprefix("/api/device/art/"))

        if path == "/api/device/sounds" and method == "PUT":
            sounds = wire.parse_sound_list(await request.bytes())
            device = await self.kv_json(DEVICE_KEY) or {}
            device.update({"sounds": sounds, "seen": int(time.time())})
            await self.env.CLOCK.put(DEVICE_KEY, json.dumps(device))
            return Response(None, status=204)

        raise HttpError(404, "Not found.")

    # ---------- Spotify ----------

    def accounts_url(self) -> str:
        return self.secret("SPOTIFY_ACCOUNTS_URL") or spotify.ACCOUNTS_URL  # overridable to test against a fake

    def api_url(self) -> str:
        return self.secret("SPOTIFY_API_URL") or spotify.API_URL

    def redirect_uri(self, url) -> str:
        # Must match, character for character, a Redirect URI registered in the Spotify dashboard.
        return self.secret("SPOTIFY_REDIRECT_URI") or f"{url.scheme}://{url.netloc}/api/spotify/callback"

    def side_name(self, side: str) -> str:
        return self.secret("ME_NAME" if side == "me" else "HER_NAME") or ("Me" if side == "me" else "Her")

    async def weather_json(self, url):
        resp = await self.upstream(url)
        if resp.status != 200:
            raise Upstream("Weather service unavailable")
        return json.loads(await resp.text())

    async def upstream(self, *args, **kwargs):
        """fetch() that turns every network failure into Upstream."""
        try:
            return await fetch(*args, **kwargs)
        except Exception as e:  # pyodide raises OSError / JsException depending on the failure
            raise Upstream(type(e).__name__) from None

    async def store_tokens(self, side: str, rec: dict | None):
        doc = await self.kv_json(SPOTIFY_KEY) or {}
        if rec:
            doc[side] = rec
        else:
            doc.pop(side, None)
        await self.env.CLOCK.put(SPOTIFY_KEY, json.dumps(doc))

    async def token_call(self, form: dict):
        resp = await self.upstream(
            self.accounts_url() + "/api/token", method="POST",
            headers={"authorization": spotify.basic_auth(self.secret("SPOTIFY_CLIENT_ID"), self.secret("SPOTIFY_CLIENT_SECRET")),
                     "content-type": "application/x-www-form-urlencoded"},
            body=urlencode(form))
        try:
            data = await resp.json()
        except Exception:
            data = {}
        return resp.status, data

    async def refresh(self, side: str, rec: dict):
        """A fresh token record for `side`, saved. None if Spotify says the grant is gone (the person
        revoked access): the side is then disconnected. Raises Upstream if Spotify just isn't answering."""
        status, data = await self.token_call({"grant_type": "refresh_token", "refresh_token": rec["refresh"]})
        if status == 400 and isinstance(data, dict) and data.get("error") == "invalid_grant":
            await self.store_tokens(side, None)
            return None
        fresh = spotify.token_record(data, time.time(), rec) if status == 200 else None
        if not fresh:
            raise Upstream(f"token refresh answered {status}")
        await self.store_tokens(side, fresh)
        return fresh

    async def playing_call(self, access: str):
        resp = await self.upstream(
            self.api_url() + "/v1/me/player/currently-playing?additional_types=episode",
            headers={"authorization": "Bearer " + access})
        data = None
        if resp.status == 200:
            try:
                data = await resp.json()
            except Exception:
                raise Upstream("unreadable answer") from None
        return resp.status, data

    async def listening(self, side: str, rec: dict | None = None) -> dict:
        """What `side` is listening to right now, as a spotify.parse_playing() dict. Never raises."""
        if rec is None:  # (False = "already looked: not connected")
            rec = (await self.kv_json(SPOTIFY_KEY) or {}).get(side)
        if not isinstance(rec, dict) or not rec.get("refresh"):
            return dict(spotify.OFF)
        try:
            if not spotify.token_fresh(rec, time.time()):
                rec = await self.refresh(side, rec)
                if rec is None:
                    _last_listening.pop(side, None)
                    return dict(spotify.OFF)
            status, data = await self.playing_call(rec["access"])
            if status == 401:  # revoked or expired early: one fresh token, one more try
                rec = await self.refresh(side, rec)
                if rec is None:
                    _last_listening.pop(side, None)
                    return dict(spotify.OFF)
                status, data = await self.playing_call(rec["access"])
            if status not in (200, 204):  # 429 rate limit, 5xx, ...: keep what we showed
                raise Upstream(f"currently-playing answered {status}")
        except Upstream:
            return _last_listening.get(side) or dict(spotify.OFF)
        result = spotify.parse_playing(status, data)
        _last_listening[side] = result
        return result

    async def listening_both(self) -> tuple:
        """(left, right) listening dicts. One KV read; no Spotify calls at all when nobody is connected."""
        tokens = await self.kv_json(SPOTIFY_KEY) or {}
        left, right = await asyncio.gather(*(self.listening(side, tokens.get(side) or False) for side in spotify.SIDES))
        return left, right

    async def spotify_overview(self) -> dict:
        tokens = await self.kv_json(SPOTIFY_KEY) or {}
        configured = bool(self.secret("SPOTIFY_CLIENT_ID") and self.secret("SPOTIFY_CLIENT_SECRET"))
        # Playback is refreshed by clock requests, not by opening the admin page.
        now = [_last_listening.get(s) or dict(spotify.OFF) for s in spotify.SIDES]
        sides = []
        for side, listening in zip(spotify.SIDES, now):
            connected = bool(tokens.get(side))
            playing = None
            if connected and listening["status"] != "off":
                playing = {k: listening[k] for k in ("status", "title", "artist")}
            sides.append({"side": side, "name": self.side_name(side), "connected": connected, "playing": playing})
        return {"configured": configured, "sides": sides}

    async def spotify_callback(self, url):
        """Spotify sends the browser here after the consent page. Always ends in a redirect to the admin page."""
        q = {k: v[0] for k, v in parse_qs(url.query).items()}

        def back(result: str):
            return Response(None, status=302, headers={"location": f"/?spotify={result}", "cache-control": "no-store"})

        side = auth.check_oauth_state(self.secret("ADMIN_PASSWORD"), q.get("state"))
        if side not in spotify.SIDES:
            return back("bad_state")
        if q.get("error") or not q.get("code"):
            return back("denied")
        try:
            status, data = await self.token_call({"grant_type": "authorization_code", "code": q["code"],
                                                  "redirect_uri": self.redirect_uri(url)})
        except Upstream:
            return back("failed")
        rec = spotify.token_record(data, time.time()) if status == 200 else None
        if not rec:
            return back("failed")
        await self.store_tokens(side, rec)
        _last_listening.pop(side, None)
        return back("ok")

    async def cover(self, art_id: str):
        """GET /api/device/art/<id>: the 64x64 cover from Spotify's CDN. The id is checked to be 40 hex
        chars first, so this can only ever fetch images from the CDN, never an arbitrary address."""
        if not spotify.valid_art_id(art_id):
            raise HttpError(404, "No such cover.")
        base = self.secret("SPOTIFY_ART_URL") or "https://i.scdn.co"
        try:
            resp = await self.upstream(f"{base}/image/{art_id}")
            if resp.status != 200:
                raise HttpError(404, "No such cover.")
            data = bytes(await resp.bytes())
            _, _, progressive = model.jpeg_info(data)  # raises Invalid: not a JPEG
        except (Upstream, Invalid):
            raise HttpError(502, "Couldn't get that cover.") from None
        if progressive or len(data) > 20_000:  # the clock's decoder is baseline-only, and its buffer small
            raise HttpError(502, "That cover isn't something the clock can show.")
        return jpeg_response(data)

    async def note_check_in(self, q: dict):
        """Remember when the clock last polled. Written at most every SEEN_WRITE_EVERY seconds, and (the clock
        polls every ~30 s) without even reading KV in between, going by this isolate's own memory."""
        now = int(time.time())
        update = {k: q[k][:64] for k in ("tz", "fw") if q.get(k)}
        try:
            update["rssi"] = int(q["rssi"])
        except (KeyError, ValueError):
            pass
        known = _checkin
        if (now - known["at"] < SEEN_WRITE_EVERY
                and all(known.get(k) == update[k] for k in ("tz", "fw") if k in update)):
            return
        device = await self.kv_json(DEVICE_KEY) or {}
        changed = any(device.get(k) != update[k] for k in ("tz", "fw") if k in update)
        if changed or now - int(device.get("seen", 0)) >= SEEN_WRITE_EVERY:
            device.update(update)
            device["seen"] = now
            await self.env.CLOCK.put(DEVICE_KEY, json.dumps(device))
        known.update({k: update[k] for k in ("tz", "fw") if k in update}, at=int(device.get("seen", now)))
