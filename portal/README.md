# Clock portal

The admin page for Bhoomi's clock. From anywhere, set the **photo** (cropped to the 320×240 screen in the browser), a **message**, and **alarms**, and connect both of your **Spotify** accounts so the clock shows what you're each playing. The clock polls this service over the internet. It's a Python Cloudflare Worker with a static page and one KV namespace. The admin page talks JSON, and the clock talks protobuf, with the schema in [`../proto/clock.proto`](../proto/clock.proto).

```
public/          admin page (plain HTML/CSS/JS, no build step), served by Workers Static Assets
src/entry.py     the Worker: routing, KV, the admin and device APIs
src/model.py     validation and the state shape (pure Python)
src/auth.py      session cookie and token checks (pure Python)
src/wire.py      state <-> protobuf messages for the clock
src/spotify.py   Spotify without the network: token bookkeeping, JSON -> clock text (pure Python)
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
- `nowplaying`: always set, with a `left` and a `right` listener (the first and second person, in the order of `spotify.py`'s `SIDES`). Each has a `status` (`OFF`, `PLAYING`, `PAUSED`), a `title` and `artist` (ASCII only, up to 60 characters: accents are stripped and other scripts become `?`, because the clock's fonts are Latin) and an `art_id`. The portal asks Spotify on every poll, and keeps the last good answer if Spotify rate-limits or hiccups.

**`GET /api/device/image/<id>`** returns exactly 320×240 baseline JPEG data, ready for TJpgDec. Its length is in `photo.size`. KV can take up to about 60 s to reach every location, so a fresh id can briefly return 404. Keep the old photo and retry on the next poll.

**`GET /api/device/art/<art_id>`** returns the 64×64 baseline JPEG cover (about 3 KB), proxied from Spotify's CDN. The id is checked to be 40 hex characters, so this can't be pointed anywhere else. Download it when a side's `art_id` changes, like the photo.

**`PUT /api/device/sounds`** takes a `SoundList` body and returns `204`. Send it at boot and whenever the card's `/sounds` folder changes. It fills the alarm sound picker.

### Changing the schema

1. Edit `proto/clock.proto`, and `proto/clock.options` if you add strings or lists. Only add fields with new numbers, so older clocks keep working.
2. Run `proto/generate.sh`. It needs `protoc` and `uv` (`brew install protobuf uv`).
3. Keep the limits in `src/model.py` in step with `clock.options`. `tests/test_wire.py` checks that the worst case fits the nanopb buffers.

## Spotify

Each of you connects your own Spotify account once. The portal keeps a refresh token per person in KV and asks Spotify's "currently playing" for each when the clock polls. Nothing about Spotify is stored on the clock.

1. Make an app at the [Spotify dashboard](https://developer.spotify.com/dashboard) (Web API). Add these **Redirect URIs**, exactly: `https://<your-worker>.workers.dev/api/spotify/callback`, and for working on your Mac `http://127.0.0.1:8787/api/spotify/callback` (Spotify doesn't accept `localhost`, so open the admin page at `http://127.0.0.1:8787` when connecting).
2. An app in Spotify's *development mode* only works for people you add under **User Management** (their name and Spotify email), and Spotify limits how many and may require the app owner to have Premium. Check their current rules. Add both of you.
3. Give the portal the app's keys: `npx wrangler secret put SPOTIFY_CLIENT_ID` and `SPOTIFY_CLIENT_SECRET` (locally, in `.dev.vars`; see `.dev.vars.example`). Names on the admin card come from `ME_NAME` / `HER_NAME` in `wrangler.toml`.
4. Open the admin page, **Music** card, and tap **Connect** next to each name. The consent page opens on whichever device you're using, so the other person can tap it on their own phone (log in to the admin page there) or sign in to their Spotify on yours. **Disconnect** forgets the token. If someone revokes access in their Spotify account, their side switches itself off and shows "Connect" again.

On the clock your song is on the left and hers on the right, in a small strip under the time (or at the top over the photo). A side with nothing playing is left out, a paused song is dimmed and disappears after 5 minutes, and with neither playing the strip is gone. It works with whatever device is playing (phone, laptop, speaker). A Spotify private session shows nothing.

To try it without Spotify, the `SPOTIFY_ACCOUNTS_URL` / `SPOTIFY_API_URL` / `SPOTIFY_ART_URL` variables can point at a fake server.

## Admin API

This is what the page uses. Requests need the session cookie from `POST /api/login`. See the header of `src/entry.py` for the routes.

## Notes

- KV's free tier allows 1,000 writes a day. Check-ins write at most once every 10 minutes (and the isolate remembers the last one, so most polls don't even read), so a clock polling all day uses about 150 of them. Spotify tokens are written only when one is refreshed (hourly per person). Each 10 s poll reads KV twice (state, Spotify), about 17,000 reads a day against the free tier's 100,000.
- Python packages the Worker imports go in `pyproject.toml`. `pywrangler` vendors them into `python_modules/` (gitignored) and pins them in `pylock.toml`. `protoc`'s version must not be newer than the vendored `protobuf`: `protoc` 36.x pairs with `protobuf` 7.36.x.
- `clock.pb.{c,h}` (sketch root) are generated with nanopb 0.4.9.1. The firmware needs the matching Nanopb runtime: it isn't in the Arduino Library Manager index, so copy `pb.h pb_common.[ch] pb_decode.[ch] pb_encode.[ch]` from the upstream `0.4.9.1` tag into `~/Documents/Arduino/libraries/Nanopb/` (with a `library.properties`).
