"""Bhoomi clock portal: a Python Cloudflare Worker.

Static files in public/ (the admin page) are served by Workers Static Assets before this code
runs, so everything here is the JSON API:

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

  Clock (Authorization: Bearer <DEVICE_TOKEN>), protobuf messages from proto/clock.proto:
    GET    /api/device/state?tz=Asia/Kolkata&fw=0.4.0&rssi=-71
           -> 200 DeviceState (application/x-protobuf), or 304 if If-None-Match matches.
              Add &format=json to read it with curl while debugging.
    GET    /api/device/image/<id>
    PUT    /api/device/sounds    body: SoundList

Storage is one KV namespace (binding CLOCK):
    "state"       the JSON document described in model.py
    "device"      what the clock last reported (seen, tz, fw, rssi, sounds)
    "img:<id>"    the current photo, base64
"""

import asyncio
import base64
import json
import time
from urllib.parse import parse_qs, urlparse

from google.protobuf import json_format
from workers import Response, WorkerEntrypoint

import auth
import model
import wire
from model import Invalid

STATE_KEY = "state"
DEVICE_KEY = "device"
SEEN_WRITE_EVERY = 600  # s; KV's free tier allows 1,000 writes/day, so don't write on every poll


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

    async def fetch(self, request):
        url = urlparse(request.url)
        path = url.path.rstrip("/") or "/"
        try:
            if path.startswith("/api/device/"):
                return await self.device_api(request, path, url)
            if path.startswith("/api/"):
                return await self.admin_api(request, path, url)
            raise HttpError(404, "Not found.")
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

        raise HttpError(404, "Not found.")

    async def image(self, image_id: str):
        if not image_id.isalnum():
            raise HttpError(404, "No such photo.")
        raw = await self.env.CLOCK.get("img:" + image_id)
        if missing(raw):
            # can briefly happen right after a change: KV takes up to ~60 s to reach every location
            raise HttpError(404, "No such photo.")
        return jpeg_response(base64.b64decode(raw))

    # ---------- clock ----------

    async def device_api(self, request, path: str, url):
        token = self.secret("DEVICE_TOKEN")
        if not token:
            raise HttpError(503, "DEVICE_TOKEN isn't set on the server yet.")
        if not auth.same_secret(token, auth.bearer(request.headers.get("authorization"))):
            raise HttpError(401, "Bad device token.")
        method = request.method

        if path == "/api/device/state" and method == "GET":
            q = {k: v[0] for k, v in parse_qs(url.query).items()}
            await self.note_check_in(q)
            state = await self.load_state()
            etag = f'"{state["version"]}"'
            if request.headers.get("if-none-match") == etag:
                return Response(None, status=304, headers={"etag": etag, "cache-control": "no-store"})
            msg = wire.device_state(state)
            if q.get("format") == "json":  # for humans with curl
                return json_response(json_format.MessageToDict(msg), headers={"etag": etag})
            return Response(msg.SerializeToString(), headers={
                "content-type": wire.CONTENT_TYPE, "etag": etag, "cache-control": "no-store"})

        if path.startswith("/api/device/image/") and method == "GET":
            return await self.image(path.removeprefix("/api/device/image/"))

        if path == "/api/device/sounds" and method == "PUT":
            sounds = wire.parse_sound_list(await request.bytes())
            device = await self.kv_json(DEVICE_KEY) or {}
            device.update({"sounds": sounds, "seen": int(time.time())})
            await self.env.CLOCK.put(DEVICE_KEY, json.dumps(device))
            return Response(None, status=204)

        raise HttpError(404, "Not found.")

    async def note_check_in(self, q: dict):
        """Remember when the clock last polled. Written at most every SEEN_WRITE_EVERY seconds."""
        now = int(time.time())
        device = await self.kv_json(DEVICE_KEY) or {}
        update = {k: q[k][:64] for k in ("tz", "fw") if q.get(k)}
        try:
            update["rssi"] = int(q["rssi"])
        except (KeyError, ValueError):
            pass
        changed = any(device.get(k) != update[k] for k in ("tz", "fw") if k in update)
        if changed or now - int(device.get("seen", 0)) >= SEEN_WRITE_EVERY:
            device.update(update)
            device["seen"] = now
            await self.env.CLOCK.put(DEVICE_KEY, json.dumps(device))
