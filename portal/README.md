# Clock portal

The admin page for Bhoomi's clock. From anywhere, set the **photo** (cropped to the 320×240 screen in the browser), a **message**, and **alarms**. The clock polls this service over the internet. It's a Python Cloudflare Worker with a static page and one KV namespace. The admin page talks JSON, and the clock talks protobuf, with the schema in [`../proto/clock.proto`](../proto/clock.proto).

```
public/          admin page (plain HTML/CSS/JS, no build step), served by Workers Static Assets
src/entry.py     the Worker: routing, KV, the admin and device APIs
src/model.py     validation and the state shape (pure Python)
src/auth.py      session cookie and token checks (pure Python)
src/wire.py      state <-> protobuf messages for the clock
src/clock_pb2.py generated from ../proto/clock.proto (don't edit; run ../proto/generate.sh)
tests/           unit tests
```

## Run locally

Needs Node and [uv](https://docs.astral.sh/uv/) (`brew install uv`).

```bash
npm install && uv sync
cp .dev.vars.example .dev.vars   # set ADMIN_PASSWORD and DEVICE_TOKEN
npm run dev                      # pywrangler: vendors protobuf, then wrangler dev on http://localhost:8787
npm test                         # unit tests
```

Local KV data lives in `.wrangler/` and is wiped if you delete that folder.

## Deploy

```bash
npx wrangler login
npm run deploy                            # first deploy also creates the KV namespace
npx wrangler secret put ADMIN_PASSWORD    # a long one: it's the only thing guarding the page
npx wrangler secret put DEVICE_TOKEN      # e.g. python3 -c 'import secrets;print(secrets.token_urlsafe(24))'
```

For extra protection, put [Cloudflare Access](https://developers.cloudflare.com/cloudflare-one/applications/) in front of everything except `/api/device/*`.

## Clock API

The messages are defined in [`proto/clock.proto`](../proto/clock.proto). The firmware uses the nanopb code in `proto/nanopb/`. Every request sends `Authorization: Bearer <DEVICE_TOKEN>`.

**`GET /api/device/state?tz=Asia/Kolkata&fw=0.4.0&rssi=-71`** returns a `DeviceState` (`application/x-protobuf`). Poll it every 30–60 s. Send the last `ETag` as `If-None-Match` to get a bare `304` when nothing changed. The query parameters are optional. They power the "Clock online" line, the time zone label on alarms, and the weak Wi-Fi hint. Add `&format=json` to read the response with curl.

- `note`: up to 120 characters and 4 lines. It's unset when there's no message.
- `photo`: a new `id` means a new photo to download. It's unset when there's no photo.
- `alarms`: only enabled alarms are listed, sorted by time, in the clock's own time zone. `days` is a bitmask, `1 << tm_wday`. When it's `0`, the alarm rings once on `date`. An empty `sound` means the built-in beep.

**`GET /api/device/image/<id>`** returns exactly 320×240 baseline JPEG data, ready for TJpgDec. Its length is in `photo.size`. KV can take up to about 60 s to reach every location, so a fresh id can briefly return 404. Keep the old photo and retry on the next poll.

**`PUT /api/device/sounds`** takes a `SoundList` body and returns `204`. Send it at boot and whenever the card's `/sounds` folder changes. It fills the alarm sound picker.

### Changing the schema

1. Edit `proto/clock.proto`, and `proto/clock.options` if you add strings or lists. Only add fields with new numbers, so older clocks keep working.
2. Run `proto/generate.sh`. It needs `protoc` and `uv` (`brew install protobuf uv`).
3. Keep the limits in `src/model.py` in step with `clock.options`. `tests/test_wire.py` checks that the worst case fits the nanopb buffers.

## Admin API

This is what the page uses. Requests need the session cookie from `POST /api/login`. See the header of `src/entry.py` for the routes.

## Notes

- KV's free tier allows 1,000 writes a day. Check-ins write at most once every 10 minutes, so a clock polling all day uses about 150 of them.
- Python packages the Worker imports go in `pyproject.toml`. `pywrangler` vendors them into `python_modules/` (gitignored) and pins them in `pylock.toml`. `protoc`'s version must not be newer than the vendored `protobuf`: `protoc` 36.x pairs with `protobuf` 7.36.x.
- `proto/nanopb/` is generated with nanopb 0.4.9.1. The firmware needs the matching Nanopb Arduino library.
