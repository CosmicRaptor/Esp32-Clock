# Clock portal

The admin page for Bhoomi's clock. From anywhere, set the **photo** (cropped to the 320×240 screen in the browser), a **message**, and **alarms**, connect both of your **Spotify** accounts so the clock shows what you're each playing, and design animated **clock faces** on the [Faces page](#faces). The clock polls this service over the internet. It's a Python Cloudflare Worker with a static page and one KV namespace. The admin page talks JSON, and the clock talks protobuf, with the schema in [`../proto/clock.proto`](../proto/clock.proto).

```
public/          admin page (plain HTML/CSS/JS, no build step), served by Workers Static Assets
public/faces.*   the Faces page; face-motion.js, face-rules.js, face-assets.js are its shared libraries; fonts/ is Montserrat
src/entry.py     the Worker: routing, KV, the admin and device APIs
src/model.py     validation and the state shape, faces and their budget (pure Python)
src/auth.py      session cookie and token checks (pure Python)
src/wire.py      state <-> protobuf messages for the clock
src/spotify.py   Spotify without the network: token bookkeeping, JSON -> clock text (pure Python)
src/clock_pb2.py generated from ../proto/clock.proto (don't edit; run ../proto/generate.sh)
tests/           unit tests (Python: unittest; JS: node --test in tests/js; shared test vectors in tests/vectors)
```

## Run locally

Needs Node and [uv](https://docs.astral.sh/uv/) (`brew install uv`).

```bash
npm install && uv sync
cp .dev.vars.example .dev.vars   # set ADMIN_PASSWORD and DEVICE_TOKEN
npm run dev                      # pywrangler: vendors protobuf, then wrangler dev on http://localhost:8787
npm test                         # unit tests: Python, then the JS libraries (needs Node 20+)
```

Local KV data lives in `.wrangler/` and is wiped if you delete that folder.

## Deploy

```bash
npx wrangler login
npm run deploy                            # first deploy also creates the KV namespace
npx wrangler secret put ADMIN_PASSWORD    # a long one: it's the only thing guarding the page
npx wrangler secret put DEVICE_TOKEN      # e.g. python3 -c 'import secrets;print(secrets.token_urlsafe(24))'
```

### HTTPS only, behind Cloudflare Access

The Worker runs first for every request (`run_worker_first`), static files included:

- **HTTPS only.** Plain `http://` gets a `301` to `https://` for GET/HEAD and a `403` for anything else, and the page sends HSTS (`public/_headers`). `wrangler dev` on localhost or a LAN IP is exempt.
- **Cloudflare Access** guards the whole site, `/api/device/*` included. People log in with their email (one-time PIN); the clock uses a **service token**. The Worker also re-checks the JWT that Access adds (`Cf-Access-Jwt-Assertion`: RS256 signature against the team's keys, the application's AUD tag, issuer, expiry), so the site stays closed if Access is ever switched off. That check is on once both of these secrets are set:

```bash
npx wrangler secret put ACCESS_AUD            # the Access application's "Application Audience (AUD) Tag"
npx wrangler secret put ACCESS_TEAM_DOMAIN    # https://<team>.cloudflareaccess.com
```

Setting up Access (Cloudflare dashboard): Workers & Pages, **bhoomi-clock**, Settings, Domains & Routes, enable **Cloudflare Access** on the `workers.dev` route. Then in Zero Trust, Access, Applications, edit that application: an **Allow** policy for your two emails, and a **Service Auth** policy for a service token made under Access, Service credentials. Keep them out of `.dev.vars`, or local dev will want Access tokens too.

The admin password stays as a second lock behind Access.

## Clock API

The messages are defined in [`proto/clock.proto`](../proto/clock.proto). The firmware uses the nanopb code in `clock.pb.{c,h}` at the sketch root. Every request goes to `https://` and sends three headers: `Authorization: Bearer <DEVICE_TOKEN>`, plus the Access service token's `CF-Access-Client-Id: <id>` and `CF-Access-Client-Secret: <secret>`. Without the Access headers the request never reaches the Worker: Access answers `403`.

**`GET /api/device/state?tz=Asia/Kolkata&fw=0.6.0&rssi=-71`** returns a `DeviceState` (`application/x-protobuf`). It is the clock's one heartbeat: poll it every ~10 s. Send the last `ETag` as `If-None-Match` to get a bare `304` when nothing changed. The ETag moves with the portal's `version` *and* with what's playing, so a new song is a new `200` (and so is a message or alarm edit, which now reaches the clock within ~10 s). The query parameters are optional. They power the "Clock online" line, the time zone label on alarms, and the weak Wi-Fi hint. Add `&format=json` to read the response with curl.

- `note`: up to 120 characters and 4 lines. It's unset when there's no message.
- `photo`: a new `id` means a new photo to download. It's unset when there's no photo.
- `alarms`: only enabled alarms are listed, sorted by time, in the clock's own time zone. `days` is a bitmask, `1 << tm_wday`. When it's `0`, the alarm rings once on `date`. An empty `sound` means the built-in beep.
- `faces`, `pinned_face`, `default_face`: the clock faces, see [Faces on the clock](#faces-on-the-clock). `faces` is only ids and revisions (about 30 bytes each), so the state stays small.
- `nowplaying`: always set, with a `left` and a `right` listener (the first and second person, in the order of `spotify.py`'s `SIDES`). Each has a `status` (`OFF`, `PLAYING`, `PAUSED`), a `title` and `artist` (ASCII only, up to 60 characters: accents are stripped and other scripts become `?`, because the clock's fonts are Latin) and an `art_id`. The portal asks Spotify on every poll, and keeps the last good answer if Spotify rate-limits or hiccups.

**`GET /api/device/image/<id>`** returns exactly 320×240 baseline JPEG data, ready for TJpgDec. Its length is in `photo.size`. KV can take up to about 60 s to reach every location, so a fresh id can briefly return 404. Keep the old photo and retry on the next poll.

**`GET /api/device/art/<art_id>`** returns the 64×64 baseline JPEG cover (about 3 KB), proxied from Spotify's CDN. The id is checked to be 40 hex characters, so this can't be pointed anywhere else. Download it when a side's `art_id` changes, like the photo.

**`GET /api/device/face/<id>`** returns a `Face` (`application/x-protobuf`, or JSON with `?format=json`). Its `ETag` is the face's `rev`, so `If-None-Match` gets a `304`. Fetch it when a `FaceRef` in the state has a new `id` or `rev`. `404` if the face is gone.

**`GET /api/device/asset/<hash>`** returns a face's background (`image/jpeg`, 320×240 baseline) or a sprite (`application/octet-stream`, see [Asset files](#asset-files)). The hash names the content, so a file at a URL never changes: cache it as long as a face refers to it. A fresh hash can briefly return `404` (KV takes up to ~60 s to reach every location): keep the old face and retry on the next poll.

**`PUT /api/device/sounds`** takes a `SoundList` body and returns `204`. Send it at boot and whenever the card's `/sounds` folder changes. It fills the alarm sound picker.

### Changing the schema

1. Edit `proto/clock.proto`, and `proto/clock.options` if you add strings or lists. Only add fields with new numbers, so older clocks keep working.
2. Run `proto/generate.sh`. It needs `protoc` and `uv` (`brew install protobuf uv`).
3. Keep the limits in `src/model.py` in step with `clock.options`. `tests/test_wire.py` checks that the worst case fits the nanopb buffers.

## Faces

A **face** is a layered scene, not a flat image: a clean background, transparent sprites that move (a panda bobbing, "zzz" floating up, a heart pulsing), particle effects (snow, petals, stars), and a time and a date slot whose position, size, colour and shadow change per face. The page is `/faces` (linked from the main page). The clock gets faces as protobuf and does the drawing; this task is the portal and API only, the LVGL renderer comes separately.

On the page:

- **List**: create, duplicate, delete, reorder (▲▼) and **📌 pin**. An "On the clock right now" line says which face the clock would show, in the clock's time zone (as last reported at check-in), using the same rule logic as the clock.
- **Editor**: an animated 320×240 preview (about 30 fps). Drag the sprites, text and particle areas on it (corner handle to resize an area), or use the x/y fields and the arrow pad: on a phone the preview stays pinned at the top while the form scrolls. Pick a layer to edit its motions, copies and stagger. Upload the background and sprites under **Images**. The **Redraw budget** meter updates as you edit.
- **When**: hours (may wrap midnight), months, weekdays and special dates, see below.

The rules for choosing a face (the clock evaluates them itself, offline, in its own local time):

1. The **pinned** face, if there is one.
2. Otherwise the first face in the list whose `when` matches. Put the special ones (Valentine's Day) before the general ones (winter).
3. Otherwise the **fallback** face, if one is set ("Use as fallback" in the ⋯ menu).
4. Otherwise the clock's built-in face.

A `when` holds an hour range `[from, until)` (whole hours; `22 → 06` wraps past midnight; `until` can be 24), months, weekdays (Sunday is 0) and special dates (`day/month`, every year or one year). **Everything that is set must hold** (AND); within a list, any entry will do (December *or* January; Valentine's *or* Christmas). A face with no rules at all never matches by itself: it only shows when pinned or as the fallback. The rule logic is in [`public/face-rules.js`](public/face-rules.js) and [`model.py`](src/model.py) (`face_matches`, `pick_face`); [`tests/vectors/face_rules.json`](tests/vectors/face_rules.json) is run against both, and is the thing to port-test the firmware against.

### Assets

Upload PNGs (or any image); the browser converts them, like the photo cropper does:

- **Backgrounds** become a 320×240 baseline JPEG (centre-cropped to fill; transparent parts go black).
- **Sprites** become an LVGL 9 `RGB565A8` raw image (see [Asset files](#asset-files)), so the clock only copies bytes, plus a PNG kept for the preview. They are shrunk to fit within a size you choose ("fit within", 64 px to start with, 160 at most) and never enlarged. For an animated **sprite sheet**, put equal frames side by side in one PNG and set "frames across" before uploading (a sheet is up to 1280×160 and 51,200 pixels in all); then add a **Frames** motion.

Assets are named by a content hash (16 hex chars of the SHA-256) and stored in the `CLOCK` KV: `asset:<hash>` (the clock's file), `assetpng:<hash>` (the preview PNG), `assetmeta:<hash>` (`kind`, size, upload time; one key per asset so uploads never race). A face (`face:<id>`) lists the assets it uses with their sizes; the state holds only each face's `id` and `rev`, plus the order, default and pinned ids. Saving or deleting a face deletes the assets no face uses any more, an hour after they were uploaded (so a sprite you just uploaded survives while you finish the face). Saving writes a face and the state: 2 KV writes, plus 3 per new sprite.

### Limits and the redraw budget

Everything is checked in `model.py` (`clean_face`, `face_report`), on every save and live while you edit (the page posts the draft to `/api/face-check`, so the page and server can't disagree):

| | |
|---|---|
| faces | 12 |
| sprite layers | 8, each up to 12 copies, 24 sprites in all (the icon counts) |
| motions | 3 per layer, 1 `frames`, and a `frames` layer can't also `pulse` (the clock would show the neighbouring frames of the sheet when it scales) |
| particle effects | 3, each 1-3 sprites (at most 48×48 px) and 1-40 particles, 60 in all |
| sprite size | 160×160 (one frame); the icon 64×64 |
| memory on the clock | 1.5 MB per face: the decoded background (150 KB) plus every sprite file |
| on screen | every sprite, **at every point of its motion and every copy**, the particle areas, and the time and date boxes must lie inside 320×240. A pulse grows about the centre; stacked motions add up |

The clock redraws whatever moves, over a 40 MHz SPI link (at best ~2.5 M pixels/s, about half of that in practice, shared with audio and Wi-Fi). So a face may change at most **30,000 pixels per animation tick**, at **15 ticks a second**: 450 k px/s, about a fifth of the link (a full screen is 76,800). The meter shows the estimate as a percentage of that, and a face over 100% is refused. The estimate, per sprite copy, with `tick = 1000/15` ms, `a`×`b` the sprite's size and `step` the farthest it can move in one tick (`amp·2π/P·tick` for bob and sway, `|d|·π/P·tick` for a ping-pong path, `|d|/P·tick` for a loop path):

- moving: `(a + ceil(stepx)) · (b + ceil(stepy)) · min(1, stepx + stepy)` (old box plus new box, redrawn when a whole pixel is crossed)
- fade: `a·b` every tick; blink: `a·b · 2·tick/P` (it flips twice per period); frames: `a·b · fps/15`
- pulse: the biggest box `a·b·(max/100)²`, **times 3** (the clock scales in software)
- the largest of those applies to a layer; copies multiply
- particles: falling or rising, per particle `(w + ceil(sway·2π/(0.75·P)·tick)) · (h + ceil(speed_max/15)) · min(1, …)`; twinkling, `w·h`

The time and date change once a minute and aren't counted. The constants are at the top of `model.py` (`ANIM_FPS`, `BUDGET_PX`, `SCALE_COST`); they are a first guess until the renderer runs on the board, and are the one place to tune.

### Motions

Shared by the preview (`public/face-motion.js`, the only implementation) and written out here for the firmware. **No rotation**: LVGL rotates in software, which is slow; use a `frames` sheet.

Every motion is a pure function of the time `t`, in milliseconds since the face was shown. Copy `i` (0-based, of `copies`) of a layer sits at `(x + i·stagger_x, y + i·stagger_y)` and runs at `t_i = t + i·stagger_phase_ms`. A motion with period `P` and phase `F` has `u = ((t_i + F) mod P) / P`, in [0, 1) (`mod` is never negative, also for a negative `stagger_phase_ms`). A layer's motions combine: **offsets add, opacities multiply, scales multiply**.

| type | formula |
|---|---|
| `BOB` | `dy = amp · sin(2πu)` |
| `SWAY` | `dx = amp · sin(2πu)` |
| `PATH` | `(dx, dy) · w`, where `w = 0.5 − 0.5·cos(2πu)` (there and back), or with `loop`, `w = u` (then a jump back) |
| `FADE` | opacity % by linear interpolation through the keyframes `(at, pct)` at `x = 1000·u`. Past the last key it runs back to the first key's value, reaching it at `x = 1000`. Before the last key, between the two keys around `x` |
| `BLINK` | visible while `(t_i + F) mod P < on_ms`, hidden (opacity 0) otherwise |
| `PULSE` | `s = min_pct + (max_pct − min_pct) · (0.5 − 0.5·cos(2πu))`, in percent, about the sprite's **centre** |
| `FRAMES` | `frame = floor((t_i + F) · fps / 1000) mod frames`; draw the `w/frames`-wide cell at `x = frame·w/frames` |

The drawn position is `round(x + i·stagger_x + Σdx)`, `round(y + i·stagger_y + Σdy)`. With a scale `s` (percent) the size is `round(w·s/100)` × `round(h·s/100)` and the box is re-centred: `left = round(cx − w'/2)` with `cx = x + w/2 + Σdx` (`w` = one frame's width). `round(v) = floor(v + 0.5)`. Opacity percent → LVGL opa: `round(pct · 255 / 100)`.

### Particles

An emitter has `count` particles inside `area` (they are **clipped to it**), with sprites `sprites[]`, `speed_min..speed_max` (px/s), `sway` (px), `period_ms`, `phase_ms`, `front`/back and a `seed`. Particle `i` draws six numbers `r1..r6` from a seeded PRNG, so the clock and the preview agree. All of it is 32-bit integer maths (`uint32_t`):

```
mix32(seed, i):  h = seed ^ ((i + 1) * 0x9E3779B1);   h = (h ^ (h >> 16)) * 0x85EBCA6B;
                 h = (h ^ (h >> 13)) * 0xC2B2AE35;    h = h ^ (h >> 16);   return h == 0 ? 1 : h
xorshift32(s):   s ^= s << 13;  s ^= s >> 17;  s ^= s << 5;  return s
r1..r6:          s = mix32(seed, i);  six times: s = xorshift32(s);  r_k = s >> 16        (0..65535)
```

Pinned values to test against: `mix32(1, 0) = 2980047484`, and `r1..r6` for `(seed 1, i 0)` are `55001, 3692, 11333, 12611, 43924, 54468`; for `(4294967295, 39)` they are `15324, 24416, 27980, 23327, 7000, 48366` (also in `tests/js/face-motion.test.js`).

```
startFrac = r1/65536      xFrac = r2/65536      swayTurn = r4/65536
speed     = speed_min + (speed_max − speed_min) · r3/65536                    (px/s)
period    = period_ms · (0.75 + 0.5 · r5/65536)
sprite    = sprites[ r6 mod n ]        (sw, sh = its size; a = area; T = t + phase_ms)
x0        = a.x + xFrac · (a.w − sw)

FALL:     travel = a.h + sh;  d = (speed·T/1000 + startFrac·travel) mod travel
          x = x0 + sway·sin(2π(T/period + swayTurn));   y = a.y − sh + d           (enters above, leaves below)
RISE:     same d and x;                                  y = a.y + a.h − d
TWINKLE:  x = x0;  y = a.y + startFrac·(a.h − sh)
          opacity % = 100 · (0.5 − 0.5·cos(2π·(T/period + swayTurn)))
```

The PRNG, `r1..r6` and the choice of sprite are exact. The sines and cosines can use any libm; a one-pixel difference there is fine.

### Time and date text

`time` is `HH:MM` (24 h); `date` is one of `LONG` "Friday, 7 November", `SHORT` "Fri, 7 Nov", `DAY_MONTH` "7 November", `WEEKDAY` "Friday", `NUMERIC` "07/11/2026" (English, local time). A slot is a Montserrat Medium label (`lv_tiny_ttf_create_data(montserrat_medium_ttf, len, size)`, so `size` is the em size in px, the same as a CSS `font-size`): `y` is the **top of the line box** (line height `1.219·size`, baseline at `y + 0.968·size`); `x` is its left edge, centre or right edge by `align`. A shadow is the same text in the shadow colour, drawn first, offset `max(1, size/24)` pixels right and down. The preview draws with the same font (`public/fonts/montserrat-medium-latin.woff2`, a Basic Latin subset of the clock's own copy, served locally; see the notice there: SIL OFL).

### Asset files

- Background: 320×240 baseline JPEG, ready for TJpgDec, at most 200 KB. `SpriteRef.size` is the length.
- Sprite: an LVGL 9 `LV_COLOR_FORMAT_RGB565A8` (`0x14`) raw image, as `lv_image_dsc_t` data: the 12-byte `lv_image_header_t` (little-endian: `0x19` magic, `0x14`, flags `0` (2 bytes), `w` (2), `h` (2), `stride = 2·w` (2), `0` (2)), then the RGB565 plane (`h` rows of `w` little-endian 16-bit pixels, `stride` bytes per row), then the A8 plane (`h` rows of `w` alpha bytes). Total `12 + 3·w·h` bytes. The clock copies the bytes after the header into a buffer and points an `lv_image_dsc_t` (`header.cf = LV_COLOR_FORMAT_RGB565A8`, `stride = 2·w`) at it, with `LV_DRAW_BUF_STRIDE_ALIGN` left at 1.

### Faces on the clock

How the firmware does it (`face_view.cpp`, `face_motion.h`, `face_rules.h`, `portal_client.cpp`; see the main README, "Faces"): it polls `/api/device/state` as before; for each `FaceRef` it fetches `/api/device/face/<id>` when the `rev` is new, then the images; it picks the face with the four rules above from the local date and time (when a face arrives, and once a minute); when the picked face changes it builds it and runs the motions. A face that isn't completely downloaded (404 right after an edit) is not shown: the previous one stays, and the clock retries.

## Spotify

Each of you connects your own Spotify account once. The portal keeps a refresh token per person in KV and asks Spotify's "currently playing" for each when the clock polls. Nothing about Spotify is stored on the clock.

1. Make an app at the [Spotify dashboard](https://developer.spotify.com/dashboard) (Web API). Add these **Redirect URIs**, exactly: `https://<your-worker>.workers.dev/api/spotify/callback`, and for working on your Mac `http://127.0.0.1:8787/api/spotify/callback` (Spotify doesn't accept `localhost`, so open the admin page at `http://127.0.0.1:8787` when connecting).
2. An app in Spotify's *development mode* only works for people you add under **User Management** (their name and Spotify email), and Spotify limits how many and may require the app owner to have Premium. Check their current rules. Add both of you.
3. Give the portal the app's keys: `npx wrangler secret put SPOTIFY_CLIENT_ID` and `SPOTIFY_CLIENT_SECRET` (locally, in `.dev.vars`; see `.dev.vars.example`). Names on the admin card come from `ME_NAME` / `HER_NAME` in `wrangler.toml`.
4. Open the admin page, **Music** card, and tap **Connect** next to each name. The consent page opens on whichever device you're using, so the other person can tap it on their own phone (log in to the admin page there) or sign in to their Spotify on yours. **Disconnect** forgets the token. If someone revokes access in their Spotify account, their side switches itself off and shows "Connect" again.

On the clock your song is on the left and hers on the right, in a small strip under the time (or at the top over the photo). A side with nothing playing is left out, a paused song is dimmed and disappears after 5 minutes, and with neither playing the strip is gone. It works with whatever device is playing (phone, laptop, speaker). A Spotify private session shows nothing.

To try it without Spotify, the `SPOTIFY_ACCOUNTS_URL` / `SPOTIFY_API_URL` / `SPOTIFY_ART_URL` variables can point at a fake server.

## Admin API

This is what the pages use. Requests need the session cookie from `POST /api/login`. See the header of `src/entry.py` for the routes. For faces: `GET /api/faces` (everything the Faces page needs), `POST /api/faces`, `PUT`/`DELETE /api/faces/<id>`, `POST /api/faces/<id>/duplicate`, `PUT /api/face-settings` (`order`, `default`, `pinned`), `POST /api/face-check` (a draft's errors and budget, nothing saved), and the asset uploads `PUT /api/assets/bg`, `PUT /api/assets/sprite` and `PUT /api/assets/<hash>.png`.

## Notes

- KV's free tier allows 1,000 writes a day. Check-ins write at most once every 10 minutes (and the isolate remembers the last one, so most polls don't even read), so a clock polling all day uses about 150 of them. Spotify tokens are written only when one is refreshed (hourly per person). Each 10 s poll reads KV twice (state, Spotify), about 17,000 reads a day against the free tier's 100,000.
- Python packages the Worker imports go in `pyproject.toml`. `pywrangler` vendors them into `python_modules/` (gitignored) and pins them in `pylock.toml`. `protoc`'s version must not be newer than the vendored `protobuf`: `protoc` 36.x pairs with `protobuf` 7.36.x.
- `clock.pb.{c,h}` (sketch root) are generated with nanopb 0.4.9.1. The firmware needs the matching Nanopb runtime: it isn't in the Arduino Library Manager index, so copy `pb.h pb_common.[ch] pb_decode.[ch] pb_encode.[ch]` from the upstream `0.4.9.1` tag into `~/Documents/Arduino/libraries/Nanopb/` (with a `library.properties`).
