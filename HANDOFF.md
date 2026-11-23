## Portal deployment in scheduled CI — active and verified, 24 November 2026

Added a portal job before signed firmware publication in `.github/workflows/ota.yml`. Runtime/configuration/dependency changes are compared against independent `clock-portal/<source-SHA>` deployment tags; unchanged, documentation-only and firmware-only portal inputs skip deployment. Tests run before `npm run deploy`, and a marker is created only after deployment succeeds. Failure blocks firmware publication; a retry preserves the earlier portal marker. Both jobs use the exact source SHA selected by the portal job.

Pinned the live `CLOCK` KV namespace (`3de27a66d4964e5f87417ec89f62907a`) after confirming the deployed Worker binding. Configured `CLOUDFLARE_ACCOUNT_ID`; user added `CLOUDFLARE_API_TOKEN` directly in GitHub Secrets and both names were verified. A manual `portal_only` input permits testing/deploying the portal without firmware publication. CI run `45243266932` passed tests and deployed Worker version `eb18f310-a7fd-4b2d-92e6-7a5bd3106136` from `42ab83a`, recording `clock-portal/42ab83a54065a82af380d0e54294b46468b742ee`. Live authenticated management API check passed with existing clock data accessible. Unchanged-source run `45243608826` passed with `changed=false` and skipped deployment/dependency setup. Both verification runs used `portal_only=true` and skipped firmware publication; no firmware source changes or USB installation in this change. Validation: frozen-dependency deployment dry run, 185 Python + 79 JavaScript portal tests, 11 release-tool tests, YAML/source-pinning checks and whitespace checks pass. Main is pushed; normal scheduled runs now deploy changed portal code before checking signed firmware releases.

## Video notes and alarm songs — deployed and USB flashed, 22 November 2026

Feature commit `a7c05b8` pushed to main. The admin dashboard sections collapse individually; video notes use the full-screen mood player, and alarm songs upload through the portal to the SD card. Portal Worker version `8abbfc03-53ba-4a39-9158-15f9a0b369d5` deployed from the committed snapshot. Firmware **0.12.5** USB-flashed with hash verification; build **3,210,030 bytes (51% of 6 MB), globals 85,864 bytes**. Serial showed SD mounted, Wi-Fi online and portal polling after boot; physical screen and end-to-end playback still need user confirmation. Portal tests: 178 Python and 78 JS passed. Other pre-existing workspace edits were preserved.

# Handoff: Bhoomi's Clock

## Illustrated study garden — source update, 20 November 2026

Focus now has a Garden button with the user-approved panda illustration and four bamboo growth stages, precomposed from generated source art. It counts naturally completed study sessions, not cancelled sessions or breaks, in seven local-day NVS slots. A completion before clock time is valid is not counted. The scene reaches its final stage at four sessions; the rolling seven-day count can grow beyond that. Artwork is embedded as five JPEGs and decoded into PSRAM on entry; switching between Focus and Garden frees the previous decoded image. Source assets and regeneration script are `assets/study-garden/` and `tools/prepare_garden_art.py`. Real LVGL host render and touch checks include Garden and Back; focus/pager host checks pass. Firmware **0.12.4** USB-flashed with application hash verification; bootloader and partition images verified unchanged. Build: **3,206,998 bytes (50% of 6 MB), globals 85,816 bytes**. No portal deployment. No portal countdown or Google Calendar integration was added. Preserve unrelated pending changes.

## White-on-black mode titles — source update, 20 November 2026

Clock, Focus and Night transition pages now consistently use white mode-name text on black backgrounds, both during dragging and the settled title reveal. Removed mode-dependent color overrides. Screen contents, dim backlight behavior and navigation timing are unchanged. Native pager/interrupted-animation checks pass; `./build.sh` succeeds: **3,079,906 bytes (48% of 6 MB), globals 85,816 bytes**. Compile-only change: source/running version remains 0.12.3; this build must not be installed under the same version. No USB flash or manual CI release triggered for the styling change; the next signed OTA must reserve a higher version. User requested pushing the change. Preserve unrelated pending edits.

## End-button stall / USB debug output — USB flashed 0.12.3, 20 November 2026

After the 0.12.2 capture ended, user reported another End-button freeze with delayed scrolling followed by recovery. Live snapshot showed healthy memory and no latched scroll flag; attaching a USB reader was followed by a successful swipe. Installed HWCDC writer can wait seconds for host consumption, and my page diagnostics plus other app logs were synchronous UI/task writes. The continuous reader could mask this path. Do not describe the 0.12.2 memory results as a final resolution of all freezes.

Moved every application debug print into a bounded, fixed 8 KB PSRAM queue. Only a separate low-priority task writes USB, outside app/queue locks. Producers never wait for USB; full records drop on overflow, formatted records cap at 383 bytes, and counters appear in uistats. Serial input unchanged. Writer stack is 3 KB internal RAM. No installed Arduino library was modified. Host regression with stopped output task and forbidden producer USB calls: 100,000 writes return in 12–13 ms, queue stays bounded, overflow/wraparound tests pass. Added that regression to signed OTA CI; 176 Python + 78 JS portal tests and timer/navigation checks pass.

Firmware **0.12.3** USB-flashed with hashes verified; bootloader/partitions unchanged. `./build.sh`: **3,080,190 bytes (48% of 6 MB), globals 85,816 bytes**; application file 3,080,336 bytes. Kept the USB reader closed for about 170 seconds after boot while user tested repeated Start/End and scrolling; user confirmed **No pauses; responsive**. Then 90-second capture recorded 9 samples: heap 51,012–62,148, largest internal block fixed 31,732; LVGL free fixed 339,256, fragmentation 1%, Focus artwork zero on Clock. Log backlog drained 2,194→0; no normal-use drops observed. This is bounded validation, not a multi-hour hardware soak or proof that the queue was forced full on the device.

Report: `soak-tests/reports/20261120-usb-log-stall.md`; prior report annotated with follow-up. Logs: `build/frozen-after-end-0.12.2-serial.log`, `soak-tests/usb-20261120-013315/serial.jsonl`, `soak-tests/usb-20261120-014315/serial.jsonl`. Captures ended; no reader left attached. Preserve unrelated Wi-Fi diagnostic, portal, CI cache and AGENTS.md edits.

## Freeze investigation and memory monitoring — USB flashed 0.12.2, 20 November 2026

User reported progressive lag and a stuck screen. Preserved eight 0.12.1 pre-reset samples (internal heap recovered to 59,312 bytes, largest block fixed at 31,732, LVGL fragmentation 5%), then reset without reflashing at the user's request and captured a separate baseline. Reproduced a real bug in my scroll changes: LVGL emits SCROLL_BEGIN before registering an animation, but cancelling before its first tick can omit SCROLL_END, leaving pagerMoving set and normal UI updates paused. Native regression failed before the fix. Main loop now reconciles that flag with actual pointer-scroll and animation state so it cannot remain paused when both are idle. Added read-only UI-state/artwork diagnostics alongside memstats.

Firmware **0.12.2** USB-flashed with hashes verified, bootloader/partition files unchanged. `./build.sh`: **3,079,842 bytes (48% of 6 MB), globals 85,808 bytes**; application 3,079,984 bytes. Native tests pass: 1,000 interrupted animations recover with identical LVGL free space; 200 Focus screen lifecycles return to exactly the same LVGL free space. Existing input/navigation/reuse checks pass. Collector now supports bounded `--duration` / `--interval` captures; defaults remain 24 h / 60 s. Added a serial-message-redacting memory summary CLI.

Patched hardware: **124 samples**, clock uptime **17–2,256 s**, **21 logged page transitions**, no unexpected resets or crash/backtrace markers. Sampled heap 51,904–66,744 bytes, lifetime minimum 47,416; largest internal free block **31,732 bytes throughout**. LVGL free pool stayed above 328,300 bytes, fragmentation 1–4%. All sampled Clock states released Focus artwork; 21 illustrated-Focus samples held exactly 153,600 artwork bytes and exactly 345,060 LVGL free bytes. User confirmed repeated/reversed swipes, Start/End and switching pages with a running countdown remained normal. No memory exhaustion or runaway allocation was shown in this window; long-term leaks are not ruled out. Four within-capture gaps over 30 seconds occurred, plus a sampler restart; this is not continuous monitoring across the entire ~37-minute uptime span. Final checks used caffeinate to prevent idle sleep. No hardware recovery counter increment occurred in this capture; the omitted-END path is proven by the host regression rather than an instrumented repeat of the original physical freeze.

Report: `soak-tests/reports/20261120-scroll-freeze-memory.md`. Raw local captures: `usb-20261120-003727` (pre-reset 0.12.1), `usb-20261120-003949` (reset 0.12.1), `usb-20261120-005237` and `usb-20261120-012358` (0.12.2), all under `soak-tests/`. Captures ended; raw logs remain ignored by Git. Clock and the user's running timer were left running. If lag recurs, retain the UI-state snapshot before resetting; serial-log backpressure when no reader is attached is a separate possible timing influence not excluded by a capture that drains logs. Preserve unrelated Wi-Fi diagnostic, CI cache, portal and AGENTS.md edits.

## Scrolling performance — USB flashed 0.12.1, 19 November 2026

User reported lag after the illustrated Focus UI. Pager motion now draws only lightweight mode titles; the illustrated screen/clock face roots are made transparent without deleting or reflowing them, so cancelled drags restore instantly. After snapping, the old page is unloaded and the new page opens following a 120 ms title pause (previously 450 ms). UI image application, face animation/check-in rendering and screen builds defer while the pager moves. Timer completion and alarms remain active. Start/End/break transitions within Focus retain its existing decoded PSRAM artwork, avoiding repeated SD reads; leaving Focus still frees it. Removed duplicate clock-restore calls.

Added `[pager]` scroll UI-pass and clock-build timing; SD decode logs include elapsed time. Native LVGL tests verify forward/backward navigation, cancelled drags restoring opacity, button input and same-page artwork reuse. A host software-redraw comparison for 30 frames measured illustration 25,188 us vs titles 5,168 us; this is CPU draw time on the Mac, not an on-device frame-rate claim. Hardware capture shows real scrolling max UI passes 37.5–38.9 ms, SD load/decode 130–134 ms after scrolling, and clock reconstruction 259–261 ms after scrolling. User confirmed **Smooth now**. Capture: `build/scroll-performance-0.12.1-serial.log`.

`./build.sh` succeeds: **3,079,414 bytes (48% of a 6 MB slot), globals 85,808 bytes**; application file 3,079,568 bytes. Firmware **0.12.1** USB-flashed with hashes verified; bootloader/partition files unchanged. Timer host checks, native pager checks, rendered UI input/reuse tests and diff checks pass. Existing illustration and Night palette retained. No CI release triggered. Preserve unrelated pending Wi-Fi diagnostic, CI caching and AGENTS.md changes.

## Illustrated panda focus UI — USB flashed 0.12.0, 19 November 2026

Rebuilt normal Focus from the user's cozy study-room references. Generated new art with a black-and-white panda writing in a notebook (no bear), lavender window, lamp, plants and cream timer panel. Native LVGL clock/date, 15/25/45 duration chips, selected-duration preview and Start studying button overlay the art. Running sessions show the countdown, thin ring and End; completion offers Take 5 min and Done. No pause/reset/streak behavior was added. Native horizontal scrolling is retained. Focus entered from Night keeps the dim red UI without artwork.

Artwork is installed automatically at SD `/ui/focus-study-v1.jpg` (31,513 bytes, SHA-256 `cf7bed0427ce011ef2e814d4ee59599e45184b8ff406342a0bdc52f55ddaf657`). A versioned firmware provisioning copy checks/repairs that managed file at boot, avoiding the unavailable LAN service and leaving user photos alone. Opening Focus reads the SD JPEG in 1 KB chunks, decodes 153,600 RGB565 bytes into PSRAM and frees it after deleting the screen. Source, SD JPEG, exact ImageGen prompt and real 320×240 LVGL previews are in `assets/focus-study/`. Preparation and render/test scripts are `tools/prepare_focus_art.py` and `tools/render_focus_preview.py`.

Validation: `./build.sh` succeeded, **3,078,662 bytes (48% of a 6 MB slot), globals 85,792 bytes**; application file 3,078,816 bytes. Host timer checks, native LVGL pager checks and rendered UI input checks pass, including duration selection requiring Start, End, optional break, drag-over-button suppression and scrolling away during study. Firmware **0.12.0** USB-flashed with hashes verified; bootloader/partition files matched existing flash, layout unchanged. Serial confirms `[focus-art] SD install ok (31513 bytes)`, repeated successful SD decodes for idle/studying/ended states, and return to Clock. Capture: `build/focus-ui-0.12.0-serial.log`. User confirmed **Looks good and scrolling works**. No CI release triggered. Preserve separate pending Wi-Fi diagnostic, CI caching and AGENTS.md edits.

## Full clock restored — USB flashed 0.11.9, 19 November 2026

Restored the full clock source with the required version increment to **0.11.9**. `./build.sh` encountered the previously
documented corrupt shared LVGL archive; isolated compile in
`/private/tmp/bhoomi-restore-0.11.9` succeeded: **3,042,774 bytes (48% of a 6 MB app
slot), globals 85,696 bytes**; application file 3,042,928 bytes. Artifacts are under
`build/restore-0.11.9/`. USB upload completed and hashes verified, partition layout
unchanged. Serial confirms full clock startup, CST3530 touch, SD mounting, photo/face
cache loading and NTP synchronization. Saved settings and SD artwork retained.
Startup capture: `build/restore-0.11.9/serial.log`. Physical screen confirmation is
still needed.

## Horizontal scrolling correction — USB flashed 0.11.7, 19 November 2026

User reported the initial mode navigation did not scroll and left Focus inaccessible to exit. Removed the timer shortcut and the custom release-only swipe detector. Clock, Focus and Night now live in a native LVGL horizontal snap pager: pages track the finger, reveal the adjacent mode name while dragging, and settle one page at a time. The destination is constructed after snapping and the brief name reveal; the previous screen's rendered widgets/images are released then. Scroll propagation works through nested content and buttons, whose taps are cancelled during drags. Focus can be left in either direction without cancelling its running timer. Done/completion use the same pager. Visual redesign is deferred at the user's request.

Added `tools/check_mode_pager.py`, which compiles the installed LVGL for the host and exercises the actual firmware pager function using pointer input. Verified Clock → Focus → Night → Focus → Clock, bounds, drag-over-button cancellation and ordinary taps; focus timer checks and diff checks pass. `./build.sh` still encounters the previously corrupted shared LVGL cache; isolated Arduino compile succeeds: **3,042,774 bytes (48% of 6 MB), globals 85,696 bytes**. User authorized direct flashing. Firmware **0.11.7** USB-uploaded with all hashes verified; application file 3,042,928 bytes, partition layout unchanged. Serial confirms normal boot, CST3530 touch and cached face reconstruction. Capture: `build/focus-scroll-0.11.7-serial.log`. User confirmed pages follow the finger and Focus can be exited before and after starting a timer. The USB startup capture also contains one lwIP DNS core-lock assertion from NetworkManager::hostByName/dns_clear_cache in Arduino ESP32 3.3.12, reached by portal HTTPS; this is separate from pager handling. USB became unavailable before a second recovery capture; the portal subsequently received a fresh firmware 0.11.7 report, confirming network recovery. No CI release triggered for this correction.

## Focus and manual night modes — signed OTA installed, 19 November 2026

Added horizontal Clock → Focus → Night navigation with a 450 ms name reveal and a timer shortcut. Focus offers 15/25/45 minutes, a large countdown, thin ring and a small LVGL panda reading a book. Study suppresses Spotify, the note inbox and mindfulness prompts. One gentle flash-resident chime fires at study completion; Take 5 min is opt-in, Done exits, and End cancels a running timer. Timers survive screen changes and NTP adjustments, handle millis rollover, and reset on reboot. No streaks.

Per the user's clarification, Night is **manual only**, always showing dim red HH:MM on black at 4% LCD backlight; no schedule or tap-to-peek. Playing Spotify titles appear as red text without covers. Running study/break countdowns replace the time; Focus retains the dim palette when entered from Night. Clock restores normal brightness. Alarms retain priority and restore the selected brightness afterward.

Leaving Clock deletes its clock/photo/Spotify widgets, decoded images and rendered face/font caches. Other mode roots are deleted on departure; timer state and cached portal source assets remain separate. Setup, note and alarm overlays remain allocated. README documents operation and limitations. Added wraparound/completion/cancellation checks in `tools/check_focus_timer.cpp`, also run by OTA CI.

Validation: final isolated Arduino compile **3,042,550 bytes (48% of a 6 MB slot), globals 85,680 bytes**; focus host checks, 176 Python + 78 JS portal tests, six release tests and diff whitespace checks pass. The normal `./build.sh` was attempted but collided with another task's concurrent Arduino cache writes (truncated archives); the successful build used `/private/tmp/bhoomi-focus-build` and exports to `build/focus-review/`. The OTA transfer repair is now committed separately and verified through signed OTA 0.11.5. User subsequently authorized pushing these mode changes and manually triggering the signed release. CI will reserve the next version above the installed/published version before building; the source header is intentionally left for CI to version. Physical swipe, brightness, audio and memory-recovery checks remain for after OTA installation.

Release completed: source `dbbd560` pushed to main; manually triggered [GitHub Actions run 44569983636](https://github.com/CosmicRaptor/Esp32-Clock/actions/runs/44569983636) passed all checks and published signed **0.11.6**, tag `clock-ota/0.11.6`. CI compile: **3,042,322 bytes (48% of 6 MB), globals 85,680 bytes**; uploaded signed application **3,042,464 bytes**, SHA-256 `f99dc481698adf14bf5fefccdbf084a7a65635ec3cc7b2026aed1abd12451bee`. Device diagnostics confirmed firmware 0.11.6, the exact installed digest, `ota_state=0`, `ota_code=0`, and successful authenticated polling. At 68 seconds uptime: heap 64,932, minimum 51,220, largest block 31,732, RSSI −95 dBm. No USB flash was performed. Physical visual/touch/audio confirmation remains with the user. Preserved the separate AGENTS.md edit.

## Scheduled signed OTA — active and verified, 19 November 2026

Monday/Wednesday/Friday 09:17 IST Actions workflow is pushed and active. It skips unchanged firmware inputs, tests/builds, assigns a higher version, signs ECDSA P-256/SHA-256 metadata, uploads and approves the exact digest. The clock verifies its own public key and version/hash/size before writing flash, and keeps an NVS version floor across rollback. Actions are pinned to commits. All six GitHub repository secrets are configured. Private signing key stays gitignored in `.ota-secrets/signing.pem`, mode 600; back it up securely. Portal deployed as Worker `cd21d40a-dac0-497c-99e1-1e57a1ed5b9a`.

Trusted baseline 0.11.2 was USB-flashed (3,036,730 bytes, globals 85,616); user confirmed screen/touch. GitHub release run `44564817450` signed and published 0.11.3 (compile 3,036,486 bytes; upload 3,036,640 bytes), source `7727d07`, SHA-256 `62ce5e058be10c30d43806f3d83f35b35ede7afc462cc970710541b33e2a9ae2`, tag `clock-ota/0.11.3`. First download triggered IDLE0 watchdog in HTTPClient's bulk read loop; a retry then installed it and the portal confirmed firmware 0.11.3, the exact installed digest, and `ota_state=0`. Pending approval was cancelled during investigation. The first CI run's Node 22 test-directory incompatibility was fixed using an explicit test file glob.

OTA now uses the existing yielding response helper with 100 ms socket waits and a ten-minute body deadline; zero-byte short reads do not call flash writes. Added regression checks for long transfers, zero-byte reads, overflow and flash failures. Corrected baseline **0.11.4** was USB-flashed with hash verification from isolated `/private/tmp/clock-ota-activation`: **3,036,702 bytes (48% of a 6 MB slot), globals 85,616 bytes**. Partition offsets/sizes are unchanged. Concurrent Focus/Night work remains unreleased and is excluded from this baseline and commit. Build/cache collision evidence is retained under `build/ota-investigation/`; use isolated sketch/build paths for simultaneous work. Next CI release should be 0.11.5.

Final verification: GitHub run `44567926134` published **0.11.5**, source `486e2f3`, tag `clock-ota/0.11.5`, whole-file SHA-256 `894bc3678a29561860aaa5280b3ccf259a1b0ae02bf9946c06beffe82ff481bd`. Compile **3,036,490 bytes (48% of 6 MB), globals 85,616 bytes**; uploaded application 3,036,640 bytes. The repaired download completed over weak Wi-Fi without a watchdog reset, serial reported verified firmware and passed startup health checks, and portal diagnostics confirmed firmware 0.11.5, that exact installed digest, `ota_state=0`, `ota_code=0`, and the matching signing-key fingerprint. Latest report: heap 64,972, lifetime minimum 51,548, largest block 31,732, RSSI −89 dBm. User confirmed the corrected USB baseline screen/touch was normal.

Unchanged-source run `44568798052` succeeded with `changed=false`, skipping all build/sign/upload/approval steps. Validation: 176 portal Python + 78 JS tests, six release/signing tests, host checks and local authenticated HTTP integration pass. Main is pushed and scheduling is active. Temporary serial capture stopped; repaired-run log is `/private/tmp/clock-ota-activation/build/repaired-signed-ota-serial.log`. Preserve the separate user `AGENTS.md` edit and unreleased Focus/Night files/README/workflow/handoff changes. Back up the private signing key securely.

## Message box — deployed and flashed, 8 November 2026

The portal note is now opened from a corner message box with a persistent unread indicator; each face can set its position in the Faces editor, with automatic placement opposite the Spotify strip on existing faces. The portal still stores one current note. Portal Worker version `11211060-a038-445a-8901-b2a7a4eff1df` was deployed. Firmware 0.11.1 was USB flashed with hash verification; it uses 3,035,290 bytes (48% of a 6 MB slot), globals 85,616 bytes. Portal tests and face rules/motion checks passed. User confirmed the physical screen and touch work. The previous USB soak log `soak-tests/usb-20261108-223752/serial.jsonl` was preserved and its collector stopped for flashing; a new 24-hour capture started at 23:13 IST in `soak-tests/usb-20261108-231307/serial.jsonl`, with a wake lock. Treat the two captures as separate firmware runs.

## Low-volume playback investigation — 8 November 2026

User reports both sound and video stutter at volume 15 and below, while 16–18 are smooth. Disabled the audio library's default per-sample three-band IIR filters and VU metering (neither is used by the clock), leaving volume control enabled and the existing 16-descriptor DMA reserve unchanged. Added `[video] slow frame` timing when JPEG loading/decoding exceeds the 200 ms frame budget. This is a candidate CPU-load fix, not a confirmed diagnosis of the volume dependency. Build and diff checks pass: 3,033,582 bytes (48% of the 6 MB slot), globals 85,576. USB flashed with hash verification under existing soak-test fix/flash authorization; the sampler was paused during upload and resumed afterward. Startup logs show Wi-Fi online and SD face-cache restoration. User confirmed both audio and video are smooth at the previously affected low volume after flashing. Capture continues in `soak-tests/usb-20261108-223752/serial.jsonl`; this log now spans a firmware restart and must not be treated as one uninterrupted soak.

## Playback memory changes — 8 November 2026

USB soak captured SD DMA allocation failure during video/audio, with lifetime minimum internal heap 3,380 bytes and recovered largest block 7,668. Reduced Audio's DMA descriptor count from 32 to 16 before setPinout (32 KB rather than 64 KB of 32-bit stereo DMA, ~85 ms at 48 kHz). Video JPEG reads are chunked to 1 KB to bound SD bounce-buffer demand. At the user's request, mood playback now unloads the rendered face and fonts before allocating video frames; refreshFace is suppressed during playback and the current scheduled face is rebuilt after completion, touch dismissal, alarm interruption or failure. Portal assets remain cached in PSRAM. Build: 3,033,470 bytes (48% of 6 MB slot), globals 85,576. Compile, host watchdog regression and diff checks pass. USB flash/hash verified. User confirmed screen and playback look fine; serial shows face reconstruction after two playback stops, no SD DMA allocation errors in that playback check, and recovered internal heap 65,020 bytes with largest block 31,732 and minimum 41,480. Active USB capture is `soak-tests/usb-20261108-223752/serial.jsonl`, started 22:37 IST for 24 hours; cloud reports remain hourly. The preceding watchdog-only firmware ran nearly two hours without another reset in `soak-tests/usb-20261108-204155/serial.jsonl`.

## USB soak watchdog diagnosis — 8 November 2026

At 20:21:40 IST the captured portal task starved IDLE0 in `Stream::timedRead` via HTTPClient response-header parsing during management polling. The 15-second single-byte read busy-wait could trip the watchdog on a partial header. Added `CooperativeClient` for plain/TLS sockets: unsuccessful single-byte reads yield one FreeRTOS tick, preserving timeout/bulk reads. Host regression `c++ -std=c++11 tools/check_cooperative_client.cpp -o /tmp/check_cooperative_client && /tmp/check_cooperative_client` passed. Build uses 3,033,338 bytes (48% of 6 MB slot), globals 85,576. USB flash/hash verification succeeded and the fixed firmware is being sampled once a minute in `soak-tests/usb-20261108-203939/serial.jsonl` (fresh 24-hour run from 20:39 IST). Original log, decoded investigation and ELF are retained under `soak-tests/usb-20261108-191243/`. Last pre-reset heap was 29,608 with largest block 14,324; lifetime minimum 4,392 needs continued monitoring. The user authorized reasonable fixes and USB flashes during the soak. Do not increase cloud reporting.

## USB memory soak test — 8 November 2026

Added read-only `memstats` serial command: internal heap free/minimum/largest block, PSRAM free/minimum/largest block, LVGL pool free/largest/fragmentation, uptime, reset reason and RSSI. USB flashed and hash verified; command response confirmed. Build: 3,032,894 bytes (48% of 6 MB slot), globals 85,576 bytes. `soak-tests/usb_collect.py` requests it every 60 seconds and captures timestamped serial locally for 24 hours, with USB reconnection retries. Active capture began approximately 19:12 IST in `soak-tests/usb-20261108-191243/serial.jsonl`; keep the Mac awake and USB attached. Cloud diagnostics remain hourly. Early samples include startup/cache warm-up and are not a stable baseline. Review after the capture; no leak conclusion yet.

## Face artwork SD cache — flashed, 8 November 2026

Firmware 0.11.0 caches verified face protobufs and SHA-256-addressed images under `/portal/face-cache` and `/portal/face-assets` on microSD. It restores cached faces in the portal task before its first network poll, prioritises the current face when time is valid, and retains the last complete on-card face until a new definition and all its images are cached. Missing/corrupt entries fall back to the network; the cache is bounded and obsolete files are pruned only after current faces are committed. See README for the valid-time limitation and cache behavior. USB flash verified; all ten current faces populated their caches. On a second USB restart, serial confirmed the current sunset face was built from cached files before the first portal poll. No check-in was triggered. Firmware uses 3,031,906 bytes (48% of the 6 MB slot), with 85,576 bytes global RAM. Tests: 174 Python, 76 JS, and `tools/check_face_cache.cpp` host checks passed. Physical screen confirmation is still needed.

## Face revision repair — 8 November 2026

The café and sunset faces had published state revisions that did not match their stored documents, causing repeated downloads. Their existing documents were saved one at a time with time for KV propagation; the references now match and their 08–12 and 18–20 schedules remain intact. Video logs confirmed moods 1–3 were already installed rather than re-downloaded. Faces and artwork still live only in PSRAM and must be fetched again after each reboot; the board reported weak Wi-Fi around −96 dBm during the check.

## Separate speaker volumes — deployed and flashed, 8 November 2026

The admin and LAN pages have separate alarm and mood-video sliders (0–21, default 18). LAN changes persist and take effect immediately; pending local changes win over cloud settings until the server confirms the same values. See the portal README for synchronization behaviour. The clock and portal were updated; deployed Worker version `f7cfc93a-051b-4363-b76f-78d03c107e67`. Firmware uses 3,021,410 bytes (48% of a 6 MB slot), with 85,432 bytes global RAM. Python/JS tests, host conflict checks and isolated HTTP integration passed; USB flash verified and the clock's LAN volume endpoint reported 18/18 with no pending edit. A physical audio and live settings-change check is still needed.

## Mindfulness and mood videos — deployed and flashed, 8 November 2026

The five-point check-in runs every three hours and closes after five minutes. Responses queue offline and the private portal retains them for 30 days. Each mood can have a note and a video with sound: both portals prepare uploads in the browser, and the clock downloads remote uploads to microSD. `checkin` on serial shows the prompt; `moodstatus` reports installed clip hashes, frame counts and audio. The clock and portal were deployed, and Aryan confirmed the flow works. Moods 1–3 have videos on the clock; 1 and 2 currently use the same video. Portal version `98b3cd8d-9f8d-4acb-a57e-0f7fbda945c1`. Firmware including serial download diagnostics uses 3,014,762 bytes (47% of a 6 MB slot), with 85,432 bytes global RAM. The portal indicates a saved video but has no device-confirmed delivery status. No pending firmware install was approved after USB flashing. See the portal README for limits, playback and offline behaviour.

## Weather face variations — deployed and flashed, 7 November 2026

All nine panda faces now have default, cloudy/foggy and stormy rain scenes in production. Eighteen edited backgrounds and transparent rain/lightning sprites are under `portal/public/panda-collection/`. The Faces editor previews them; setup upgrades existing face IDs in place while preserving base edits, schedules, order and pin. The larger renderer copy is in PSRAM. Added protobuf fields are backward compatible, and code 97 is now accepted as a thunderstorm.

Aryan requested lightning **every second at new random sky positions**. This uses `scatter.period=1000`, a matching one-second blink/fade, and per-scene sky bounds; the café/bedroom use their windows. `face-motion.js` and `face_motion.h` agree on reseeding at each cycle. Weather classification agrees in Python, JS and C++.

All 27 browser-converted combinations passed the real model checks; maximum estimated animation budget is under 8%. Checks: **167 Python + 71 JS tests**, 291 C++/JS motion lines and 23 rule cases pass. Firmware **0.10.0** is **2,990,666 bytes (47% of a 6 MB slot)**, with 83,168 bytes global RAM. Portal deployment: `57165159-e0df-4ecf-bb28-7a466a25f85d`. All nine live faces were upgraded in place; schedules, fallback, order and pin were preserved. No extra portal page was added. Local review remains at `http://127.0.0.1:8790/weather-review`; its files are outside the deployed public directory.

USB flashing and hash verification succeeded. Serial polling remained healthy for several minutes on weak Wi-Fi; Aryan confirmed the physical screen looks right. During asset fetching we found an idle-task watchdog starvation path in HTTPClient's body loop. Small buffered responses now use cooperative yielding, 100 ms socket read waits and a 15-second overall body deadline; stalled transfers retain the previous state. The large OTA image stream keeps its separate transfer handling. A host test exercises the actual helper's yielding, deadline and write-error behavior.

The old approved 0.9.1 OTA digest was cancelled before USB flashing to avoid a downgrade. The optional release-library upload was blocked by automatic approval review and was not retried; the stored library release remains 0.9.1 with no pending approval. No secrets or firmware binaries belong in this commit. Artwork and maintenance details: [portal/ARTWORK.md](portal/ARTWORK.md).

**2026-11-07 OTA update:** custom two-slot 6 MB layout installed by USB; authenticated OTA and adjustable diagnostics deployed. A real OTA transfer passed startup health checks, and a deliberately rejected trial was confirmed to roll back to the previous app with the failed digest remembered. Final normal firmware 0.9.1 was delivered over OTA and passed startup health checks; it uses 2,988,306 bytes (47% of a 6 MB slot); no rejection-test code remains in the source. Portal checks: 160 Python + 68 JS tests and the local HTTP management checks pass. Portal deployment: `3a42f0c9-11a3-49c3-be96-dfba5ba7a446`. See the release workflow at the end. The older status below is historical.

*Written 2026-11-07, when `main` was at `e03f3ad` (pushed to origin, the same code deployed to the portal and flashed on the board).*

Start with `AGENTS.md` (rules and commands), then this file (state and what's next). `README.md` and `portal/README.md` hold the details.

## In one paragraph

A gift clock for Bhoomi (Aryan's girlfriend), a panda fan. It runs on a Waveshare ESP32-S3-Touch-LCD-2.8 in a 3D-printed stand. Aryan controls it from anywhere through an admin portal on Cloudflare: the photo, a short message, alarms, animated layered **clock faces** chosen by rules (night, winter, special dates…), and a strip showing what each of them is playing on Spotify. The clock polls the portal every 30 s over one kept-alive HTTPS connection, using protobuf with ETags. Everything time-based (alarms, which face to show) is decided on the clock, so it keeps working offline.

## Weather deployed and flashed: 7 November 2026

Weather is deployed as Worker version `d3c7e41e-9bd8-4f18-8518-f3e5be1e192a`, and the corrected firmware `0.8.0` was flashed over USB with the user’s approval. Firmware size is **2,978,470 bytes (94%)**, with 86,856 bytes of global RAM. Checks: 155 Python and 66 JavaScript tests pass, plus `bash tools/check_weather_icons.sh` against the real LVGL headers.

Weather appears beside the date after a city is chosen in the admin portal or LAN settings. Both change the same location. A separate 15-minute protobuf request uses an on-demand Open-Meteo KV cache. Aryan selected Mumbai in the portal; the live endpoint returned clear night, 32°C, and the clock’s LAN `/weather` confirmed Mumbai with no pending update. `/portal` reported `state=ok`, HTTP 304. Serial confirmed the sunset face rendered with seven particles, continued asset downloads and sound-list upload (204). Aryan confirmed the physical display works after the correction.

The first weather build rebooted when rendering a weather symbol: its bitmap callback returned packed bytes instead of the LVGL 9 A8 draw-buffer object. The decoded crash was in `lv_draw_sw_blend_color_to_rgb565`, reached from `draw_letter_cb`. The generator now expands into the supplied draw buffer and returns it; the host regression checks all eight glyphs, padded rows and buffer bounds. The repaired firmware was flashed and verified on the board.

See the Weather sections in both READMEs. Direct cloud verification needed the clock’s normal `ESP32HTTPClient` User-Agent; Python’s default User-Agent was rejected with HTTP 403.

## Live update: 7 November 2026

The approved nine illustrated panda faces are now installed through the existing `/faces` editor, with themed Spotify placement and the approved daily Asia/Kolkata schedule (see `portal/README.md`). `Paris sunset with cat` is the fallback, no face is pinned, and the old `Paris 1` is retained. No separate collection page is deployed; its local review files were archived outside `public`.

Portal version `ae172a85-d5c5-4b09-b01e-10f5e31ec32d` is deployed. The current working-tree firmware was flashed over USB: **2,969,722 bytes (94%)**, with the Spotify text-column gap and themed cover placeholders. Serial confirmed all nine face definitions and their images fetched, then the sunset face (`b0cc96514c6a`, rev `a385c740e48f`) built with seven particles. Aryan confirmed the physical screen looks right. The collection, firmware styling, assets, tests and documentation are included in the panda collection change on `main`. Portal checks: 147 Python and 63 JS tests passing.

## What's live right now

| Thing | State |
|---|---|
| Repo | `github.com/CosmicRaptor/Esp32-Clock`, branch `main`. The branches `faces-firmware`, `panda-faces` and `spotify-now-playing` are fully merged, so they can be deleted (ask first) |
| Portal | Deployed Worker `bhoomi-clock` at `https://bhoomi-clock.bhoomi-clock-portal.workers.dev`, version `d3c7e41e-9bd8-4f18-8518-f3e5be1e192a`. Behind Cloudflare Access (email one-time PIN for people, a service token for the clock) plus an admin password |
| Clock firmware | Flashed with weather support, version 0.8.0 (2,978,470 bytes, 94%). On Aryan's Wi-Fi (weak, about −85 dBm). Not handed over to Bhoomi yet |
| Secrets | In `firmware/BhoomiClock/portal_config.h` (gitignored), `portal/.dev.vars` (local, gitignored), and Worker secrets in Cloudflare. The clock's keys can also be replaced from its LAN settings page (saved in NVS). No values are written in any doc |
| Outside the repo | `../hardware/reference/` holds board schematics and Waveshare demo zips (the authoritative pin maps); `../hardware/enclosures/` holds case and stand models (`Waveshare28_case_v3`, etc.) |

## Feature status

| Feature | Built | Seen working on the board |
|---|---|---|
| Wi-Fi captive-portal setup with QR code, time zone, NTP | ✅ | ✅ |
| LAN settings page: photos and sounds on SD, cloud keys | ✅ | ✅ |
| Portal: photo, message, alarms (sound from `/sounds` or the built-in beep, touch to stop) | ✅ | ✅ |
| Spotify now-playing strip (both people) | ✅ | ✅ |
| HTTPS to the portal (mbedTLS buffers moved to PSRAM) | ✅ | ✅ |
| **Clock faces**: portal editor, validation, redraw budget, device API, LVGL renderer | ✅ | ✅ All nine collection faces downloaded; sunset face with seven particles visually confirmed |
| Per-face Spotify strip placement, layer size %, random scatter | ✅ | ⚠️ Same as above |

**What to check first on the faces** (from `README.md`, "Faces"):
- a sprite sheet shows exactly one frame at a time
- pulsing sprites scale about their centre
- the real frame rate compared with the redraw budget (`BUDGET_PX` in `portal/src/model.py`)
- the time and date text land exactly where the portal preview puts them

## What's next, in priority order

### Remaining work before the clock is given away

1. **OTA and diagnostics are implemented** (firmware 0.9.1). See the main README's “Firmware updates and diagnostics” and the portal README's API section. The USB baseline installs the custom two-slot 6 MB layout; future app releases can be uploaded and approved remotely. Bootloader rollback is enabled, with Arduino's early auto-confirmation deferred until the main loop and authenticated cloud pass startup checks. Reports default to hourly with remotely selectable 5 min–6 hour intervals or off.
2. **App partition space is resolved.** `partitions.csv` has two 6 MB app slots, ~3.9 MB FFat and coredump. `build.sh` exports application binaries in `build/`, checks the actual 6 MB slot limit, and uploads those exact artifacts. The initial migration was authorized and performed over USB, preserving Wi-Fi NVS and SD files.
3. **Confirm the faces work on the board** (see the checklist above), then tune the budget constants from real measurements.
4. **Test at Bhoomi's place, or with her router's signal level.** At −90 dBm or worse, every remote feature gets flaky. Consider a Wi-Fi extender near the clock.

### Should do

5. **Faces survive a power cut.** Today faces and their images live only in PSRAM and download again after a reboot. Cache them on the SD card by content hash.
6. **Security hardening:**
   - **Cloudflare Access service token expiry:** check its lifetime in Zero Trust and set a reminder. The clock's LAN page can take a new token, but only from inside her network.
   - **LAN settings page:** it has no PIN, so anyone on her Wi-Fi can use it.
   - **Setup hotspot:** it's open and comes back when Wi-Fi is lost.
   - **Optional, strong:** sign the state document with an Ed25519 key Aryan holds, and put the public key in the firmware. Then even a compromised portal couldn't put content on her screen.
7. **Real art.** Panda faces need a clean background plus separate transparent sprite PNGs (moving parts painted out of the background). The reference sheet of 9 face mock-ups is in the design conversation, not the repo. The art pipeline is the bottleneck, not the code.

### Ideas discussed, not started

- Alarm snooze.
- Message read receipts: the clock reports when she opens it.
- A "send a heart back" tap.
- Full-screen animations as server-converted MJPEG, decoded with JPEGDEC.
- Polling less often at night or when idle.
- A Telegram bot as a second way to control the clock.

## Key decisions and why

- **The clock pulls, the server never pushes.** Her router blocks inbound connections, and on −90 dBm Wi-Fi one kept-alive poll every 30 s is sturdier than MQTT or WebSockets. With ETags it's mostly empty 304s, about 3% of the Workers free tier.
- **Protobuf for the clock, JSON for the admin pages.** nanopb gives fixed-size structs on the ESP32, and `clock.proto` is the one contract between them.
- **Cloudflare Worker + KV, no VPS.** It's free and needs no maintenance, which suits a gift meant to run for years. KV's limit of 1,000 writes a day shapes the device path (see `portal/AGENTS.md`).
- **Images are converted in the browser** (baseline JPEG, LVGL `RGB565A8`), so neither the Worker nor the clock decodes or resizes anything heavy.
- **Faces are layered scenes, not GIFs, Lottie or video.** That gives control of each item's motion, small downloads, and only the moving parts redraw. Every motion is a deterministic function of time, so the portal preview and the clock match. There's no rotation, because it's software-rendered and expensive.
- **Rules run on the clock:** pinned face, then the first matching rule, then the fallback, then the built-in face. Alarms work the same way, so both keep working offline.
- **Defence in depth on the portal:** HTTPS only, Cloudflare Access with the JWT re-checked in the Worker, the admin password, and the device bearer token.

## Known quirks

- Joining the `Bhoomi-Clock` hotspot takes Aryan's Mac offline. An agent must hand over and wait, not keep running.
- The Arduino IDE being open holds the serial port. Uploads may need **BOOT** held while replugging.
- Running packages fetched with `npx` (e.g. `lv_font_conv`) was blocked by the permission classifier earlier. Prefer offline routes (the clock uses LVGL Tiny TTF with the bundled Montserrat).
- The Nanopb runtime isn't in the Arduino Library Manager. It's copied by hand into `~/Documents/Arduino/libraries/Nanopb/`.
- `protoc`'s version must not be newer than the `protobuf` package the Worker vendors.
- Spotify's development-mode app only works for users listed in its dashboard, and OAuth redirects locally need `127.0.0.1`, not `localhost`.

## OTA release workflow (2026-11-07)

The admin password is saved in gitignored `portal/.dev.vars` (mode 600), not in this document. `tools/firmware_release.py` uses it plus the existing Access service credentials from `firmware/BhoomiClock/portal_config.h`. `upload` publishes `build/BhoomiClock.ino.bin`; `install <sha256>` explicitly approves it; `status`, `cancel`, and `interval <seconds>` manage it. A Git push alone does not install firmware. Uploaded firmware is retained in content-addressed KV keys. Reports are latest snapshots rather than a log archive.

The physical screen still needs the user's visual check after future updates. Serial confirms initialization and the health gate, not pixel correctness. Keep all existing pre-deployment and pre-flash authorization rules.
