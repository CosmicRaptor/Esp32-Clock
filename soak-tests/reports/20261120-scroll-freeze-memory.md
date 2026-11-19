# Clock memory and scroll-freeze investigation — 20 November 2026

**Follow-up:** the user subsequently reported another End-button stall after this capture ended. The memory findings below remain valid, but 0.12.2 was not the final resolution. See [the 0.12.3 USB-log follow-up](20261120-usb-log-stall.md) for the live snapshot, background logging fix and no-reader validation.

Firmware **0.12.2** was USB-installed after preserving the original frozen-state capture and a separate reset run of 0.12.1. User reported repeated scrolling, reversed swipes, Start/End and a running timer remained responsive after the fix.

## Reproduced regression

My page-navigation pause flag depended on paired LVGL scroll events. LVGL sends SCROLL_BEGIN synchronously before registering an animated scroll. Cancelling it before the first animation tick can omit SCROLL_END, leaving the flag set indefinitely. The main loop still responds to serial memory requests, but normal UI updates remain paused. A native LVGL regression test failed before the patch and passes after it.

The main loop now reconciles the flag against actual pointer-scroll and animation activity; an interrupted animation cannot leave it paused once both are idle. Added read-only `[uistats]` alongside memstats to distinguish this state from memory pressure and track artwork ownership. The first physical frozen state had no UI-state telemetry, so the host reproduction establishes a real cause in my changes without proving it was the sole cause of that original incident.

## Hardware observations

124 samples on patched firmware, from **00:52:38 to 01:29:56 IST**, spanning clock uptime 17–2256 seconds. Observed 21 page transitions, 0 unexpected uptime resets, and 0 crash/backtrace markers.

| Measurement | Observed |
|---|---:|
| Free internal RAM, sampled | 51,904–66,744 bytes |
| Lifetime minimum internal RAM | 47,416 bytes |
| Largest internal free block | 31,732 bytes throughout |
| Free PSRAM, including startup/cache changes | 6,371,224–7,330,532 bytes |
| Free LVGL pool | 328,300–363,664 bytes |
| LVGL fragmentation | 1–4% |

The internal heap recovers to about 65.7 KB between network requests; temporary request allocations recover. Every sampled Clock state has zero Focus artwork bytes. In 21 illustrated-Focus samples, LVGL free memory stayed exactly 345,060 bytes and the expected artwork allocation stayed 153,600 bytes. Font caches warm as new clock digits are rendered; pool values across different pages/cache states are not directly comparable. The measurements do not show memory exhaustion or a runaway allocation during this test. They do not prove a long-term leak is impossible.

There were **4 within-capture gaps over 30 seconds**, largest 466.5 seconds, plus a deliberate sampler restart. This is **not** a claim of continuous monitoring across the full wall-clock span. The Mac was kept awake for the final checks. Serial-reader backpressure is another possible timing influence when no reader is attached; this capture itself drains logs and does not rule out that separate path.

## Validation and installed build

- 1,000 pre-first-tick animation cancellations: UI recovered each time; LVGL free bytes unchanged at 2,078,648 in the native host harness.
- 200 Focus create/start/finish/delete cycles: LVGL free bytes 2,080,928 before and after in the host harness. Host pool differs from the device; this tests allocation lifetime rather than device capacity.
- Existing forward/backward scrolling, cancelled drags, button input and artwork reuse checks pass.
- `./build.sh`: **3,079,842 bytes (48% of 6 MB), globals 85,808 bytes**. Application file 3,079,984 bytes. USB upload hash verified, partition layout unchanged.

## Captures and reproduction

Pre-reset frozen-state memory: `soak-tests/usb-20261120-003727/serial.jsonl` (0.12.1). Reset-run baseline: `soak-tests/usb-20261120-003949/serial.jsonl` (0.12.1). Patched firmware: `soak-tests/usb-20261120-005237/serial.jsonl` and `soak-tests/usb-20261120-012358/serial.jsonl`. Raw serial files remain local and ignored by Git.

Use `caffeinate -i python3 soak-tests/usb_collect.py --duration 900 --interval 10` for a bounded awake USB capture on macOS. `python3 soak-tests/summarize_memory.py <capture-folder> ...` summarizes samples without printing private serial messages. Native tests: `python3 tools/check_mode_pager.py` and `uv run --with pillow python tools/render_focus_preview.py`.
