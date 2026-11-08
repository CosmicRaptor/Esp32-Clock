# AGENTS.md: the clock portal

Read `../AGENTS.md` first for the whole project and its rules, and `README.md` here for the API, the faces format and deployment. This file covers what's specific to working on the portal.

## What it is

A **Python Cloudflare Worker** (Pyodide, run through `pywrangler`). It has:
- static admin pages with no build step: `public/index.html` (photo, message, alarms, Spotify) and `public/faces.html` (the Faces editor)
- one KV namespace, `CLOCK`

The admin pages talk JSON. The clock talks protobuf (`../proto/clock.proto`). Deployed as Worker `bhoomi-clock` on `workers.dev`, behind Cloudflare Access.

| Path | What |
|---|---|
| `src/entry.py` | Worker: HTTPS and Access gates, routing (route list in the module docstring), KV, admin and device APIs |
| `src/model.py` | **Pure Python** validation and state shape: messages, alarms, faces (`clean_face`, `face_report`, the redraw budget, `face_matches` / `pick_face`) |
| `src/wire.py` | state ↔ protobuf |
| `src/auth.py` | session cookie, bearer token, Access JWT checks (pure Python) |
| `src/spotify.py` | Spotify token bookkeeping and JSON → clock text (pure Python, no network) |
| `src/clock_pb2.py(i)` | **Generated** by `../proto/generate.sh`. Never edit |
| `public/face-motion.js`, `face-rules.js`, `face-assets.js` | Face maths, rule logic, PNG → JPEG / LVGL `RGB565A8` conversion. ES modules shared by the page and the tests |
| `public/fonts/` | Montserrat served locally (the site's headers forbid CDNs), so the preview's text matches the clock |
| `tests/` | `unittest` for Python, `tests/js/*.test.js` for `node --test`, `tests/vectors/` shared with the firmware's checks |

## Commands

```bash
npm install && uv sync
cp .dev.vars.example .dev.vars     # ADMIN_PASSWORD, DEVICE_TOKEN (and Spotify keys if needed)
npm run dev                        # http://localhost:8787 (use http://127.0.0.1:8787 for the Spotify OAuth flow)
npm test                           # all Python + JS tests; must pass before any commit
npm run deploy                     # production. ONLY with the user's go-ahead
```

Read device responses as JSON with `curl -H 'Authorization: Bearer <DEVICE_TOKEN>' 'http://localhost:8787/api/device/state?format=json'`. `/api/device/face/<id>?format=json` works the same way.

## Rules

- **Keep logic in the pure modules** (`model.py`, `auth.py`, `spotify.py`, `wire.py`) and test it there. `entry.py` should stay glue: requests, KV, fetch.
- **The page and the server must never disagree.** The Faces editor posts drafts to `/api/face-check`, so validation lives only in `model.py`. Don't duplicate checks in JS.
- **Logic that exists in two languages needs shared tests:**
  - Face rules exist in JS, Python and C++. They share `tests/vectors/face_rules.json`, so add cases there.
  - Motion maths exists in JS (`face-motion.js`) and C++ (`../firmware/BhoomiClock/face_motion.h`). Run `../tools/check_face_motion.sh` after changing either.
- **Schema changes:**
  1. Edit `../proto/clock.proto` and `clock.options`, adding fields with new numbers only.
  2. Run `../proto/generate.sh`.
  3. Keep the limits in `model.py` in step with the nanopb sizes. `tests/test_wire.py` checks that the worst case fits.
  4. Run `../build.sh` so the firmware still compiles.
- **KV free tier allows 1,000 writes a day.** The clock polls every 30 s, so nothing on the device path may write KV per poll (see `note_check_in`, which writes at most once every 10 minutes). Reads are fine at the current rate, about 5.8k a day of 100k. KV is eventually consistent (up to ~60 s): the clock treats a 404 on a fresh id as "retry later", so keep it that way.
- **Assets** are content-addressed (`asset:<hash>`, `assetpng:<hash>`, `assetmeta:<hash>`). Unused ones are garbage-collected an hour after upload. Backgrounds are 320×240 baseline JPEG (≤ 200 KB). Sprites are LVGL 9 `RGB565A8` (byte layout in `README.md`, "Asset files").
- **The redraw budget constants** (`ANIM_FPS = 15`, `BUDGET_PX = 30_000`, `SCALE_COST = 3` in `model.py`) are first guesses. Tune them only from measurements on the board, and update the README table with them.
- **Security layers, keep all of them:**
  - HTTPS-only redirect
  - Cloudflare Access, with the Worker re-checking the JWT when the `ACCESS_AUD` and `ACCESS_TEAM_DOMAIN` secrets are set
  - the admin password session
  - the device bearer token

  The Spotify callback is the only cookie-less admin path, and it's vouched for by a signed `state`. Never return secrets or refresh tokens to any client.
- **Python packages** go in `pyproject.toml` (vendored by pywrangler, pinned in `pylock.toml`). Keep `protoc`'s major version no newer than the vendored `protobuf` (see README "Notes").
- **Secrets** are set with `npx wrangler secret put …`: `ADMIN_PASSWORD`, `DEVICE_TOKEN`, `ACCESS_AUD`, `ACCESS_TEAM_DOMAIN`, `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET`. Locally they live in `.dev.vars`, which is gitignored. Never commit or echo them.
