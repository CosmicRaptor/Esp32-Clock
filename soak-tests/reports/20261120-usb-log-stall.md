# End-button stall follow-up — 20 November 2026

The earlier 0.12.2 memory investigation did **not** fully resolve the reported problem. After its bounded capture ended, the user stopped Focus and reported another freeze: the time-choice screen remained visible, swiping was delayed, then the clock eventually recovered.

## Evidence

A live snapshot during this incident still responded to serial commands: internal RAM about 65.7 KB, PSRAM above 6.5 MB, LVGL free 344,052 bytes, fragmentation 4%, and both pager pause/activity flags false. A swipe to Night was recorded after the USB reader was attached. This did not resemble memory exhaustion or the previously fixed latched-scroll flag.

The installed USB CDC writer can wait on its transmit buffer for seconds when the host stops consuming logs. Application debug messages, including my added page diagnostics, were being written synchronously from UI callbacks and other tasks. The delay/recovery pattern and reader attachment made logging backpressure a strong lead. A continuously draining memory capture can mask that path. The exact physical incident cannot be proven solely from these snapshots; the blocking path itself was verified in the driver code.

## Change installed as 0.12.3

All application debug output now enqueues records into a fixed 8 KB PSRAM buffer. A separate low-priority writer task handles USB output without holding the app or queue locks. Producers never call the USB writer. A full queue drops the new record, and each formatted record is capped at 383 bytes, so diagnostic delivery cannot hold up UI, portal or audio callers. Serial command input remains direct. The writer uses a 3 KB internal-RAM stack; dropped/queued counts are exposed in UI diagnostics. No installed Arduino library was changed for this fix.

## Validation

- Actual logger compiled with a completely stopped output task and a USB writer that aborts if called by a producer: 100,000 producer writes returned in 12–13 ms on the host, fixed queue stayed at or below 8,192 bytes, and overflow was dropped. Queue wraparound/order checks pass.
- 176 portal Python + 78 JS tests, timer checks, and native scroll/interruption tests pass. The stalled-output regression is included in signed OTA CI.
- Firmware **0.12.3** USB-installed with hash verification. Build **3,080,190 bytes (48% of 6 MB), globals 85,816 bytes**; application file 3,080,336 bytes. Bootloader/partitions unchanged.
- **No USB reader was attached for roughly the first 170 seconds after boot.** User tested repeated Focus Start/End and scrolling and confirmed **“No pauses; responsive.”** This explicitly covers the condition omitted from the first monitoring test.
- Reattached for a 90-second capture: 9 memory samples, uptime 171–253 s. Internal heap 51,012–62,148 bytes; largest internal free block remained 31,732. LVGL free stayed exactly 339,256 bytes, fragmentation 1%. Focus artwork was released on Clock. Pending log bytes drained from 2,194 to zero, with no dropped records in this normal-use hardware test. The forced-full/stopped writer case is covered by the host test above, not a claim of deliberate full-driver saturation on hardware.

Raw local captures: `build/frozen-after-end-0.12.2-serial.log`, `soak-tests/usb-20261120-013315/serial.jsonl` (incident follow-up), and `soak-tests/usb-20261120-014315/serial.jsonl` (0.12.3). Logs remain local/ignored. Captures ended. This is a bounded test, not a completed multi-hour soak.
