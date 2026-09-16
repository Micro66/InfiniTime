# ESP32-S3 1.75C validation

Tested 2026-09-11 on the user's connected device. This is a UI/application port,
not a claim that every PineTime feature or peripheral has been ported.

## Hardware identification and recovery

- ESP32-S3 QFN56 revision 0.2; 8 MiB embedded octal PSRAM.
- Flash JEDEC manufacturer 0xC8 / device 0x4019: **32 MiB**, measured with esptool.
- I2C scan: **0x18, 0x34, 0x40, 0x5A, 0x6B**. No 0x51 RTC or 0x20 expander.
- This matches the manufacturer's **1.75C** hardware. Display reset GPIO1,
  touch reset GPIO2. Non-C board documentation must not be used for these pins.
- A full **33,554,432-byte** original flash backup was taken before writing.
  `esptool verify_flash` reported `verify OK (digest matched)` for the full image.
  The private backup and SHA-256 manifest are stored outside the repository.
- Bootloader, partition table and ESP32-S3 application writes each passed esptool
  hash verification. The new 64 KiB diagnostic coredump partition was initialized.

## Initial port build (272b50b0)

- InfiniTime base: `6c119eb52206b580b556b41633dddc1e1b66a8da`.
- Waveshare C driver submodule: `6d19f7e` (full pinned ID in the gitlink).
- pioarduino platform 53.03.13, Arduino-ESP32 3.1.3, ESP-IDF 5.3.2 libraries.
- `pio run`: **PASS**. Static RAM 24,360 bytes; linked flash usage 603,528 bytes.
- Firmware binary: **603,888 bytes**.
- Firmware SHA-256: `181d11f2fe4cb7d49818fe1f4303da58e1534d947051efbae8a0c9013466c66f`.
- Four host transport tests: **PASS** (RGB565 channel decoding, fragmented reads,
  CRC failure/retransmission/duplicate handling, truncated frames and wrong geometry).
- Partition table decoder: **PASS**, separate 6 MiB app / 4 MiB filesystem.
- `git diff --check`: **PASS**.

## Initial port on-device results

See [startup log](docs/evidence/boot-after-settings.log),
[UI regression](docs/evidence/serial-regression.json) and
[settings/power regression](docs/evidence/settings-power-regression.json).

| Check | Result |
| --- | --- |
| Startup | `AXP2101 OK`, `CST9217 OK`, `CO5300 466x466 OK`, `READY`; no restart loop |
| Power readings | 100% reported battery, approximately 4.12 V, actual PMIC reads |
| Clock | USB sync to UTC+8; internal RTC validity retained across USB resets |
| Native rendering | All 7 pages captured from device framebuffer and visually inspected |
| Calculator | Injected LVGL taps produced 1 + 2 = 3 |
| Stopwatch | Injected start/pause; displayed 00:01.62 while running |
| Twos | 16 injected directional swipes; merged tiles and score 68 observed |
| Navigation stress | 92 page requests, including 10 full app cycles; no spontaneous reset |
| Heap stability | Digital-page heap remained 328,472–328,484 bytes after warm-up |
| Display off/wake | Serial off/on passed; 15-second automatic timeout passed |
| Settings persistence | Brightness Medium → High survived USB reset; restored to Medium |
| Date validation | 2026-02-30 rejected without changing the clock |
| USB screenshots | All bytes CRC32 checked, with explicit chunk acknowledgments |

## Limits of these results

- Framebuffer screenshots verify software rendering, **not optical panel appearance**.
- Scripted input passes through LVGL, **not the physical touch sensor**. Physical
  touch IRQ count was zero during unattended regression. Finger gestures, touch
  orientation, BOOT and PWR presses still need the user's physical confirmation.
- Complete battery removal / power loss, long-duration clock drift and battery
  life were not tested. USB reset retention does not imply independent RTC backup.
- Display off currently sets AMOLED brightness to zero; it is not deep sleep.
- BLE/Gadgetbridge, OTA, audio, motion/activity tracking and Wi-Fi time sync are
  not implemented. There is no heart-rate sensor or vibration motor on this board.
- The original Nordic/PineTime target was not rebuilt with the Nordic toolchain.
  Its resolution defaults remain 240×240; the selected round layouts are conditional.

## Touch orientation correction (2026-09-11)

The user reported that physical taps hit the wrong screen positions. The initial
port omitted the two-axis mirror configuration used by the manufacturer's
[C-board example](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/blob/6d19f7e16fb9a3be219e9eed43ca9eb56c88d01c/examples/arduino/examples/05_LVGL_Widgets/13_LVGL_Widgets.ino#L134).
The initial `test-tap` tests bypassed this sensor-to-display transformation and
therefore could not detect the 180-degree input mismatch.

The CST9217 driver now uses `setMaxCoordinates(466, 466)` and
`setMirrorXY(true, true)`. SensorLib converts both axes before LVGL pointer input
and swipe tracking. The architecture diagram reflects this shared input path.

- Rebuild and device flash/hash verification: **PASS**.
- Current linked flash usage: 603,836 bytes; static RAM: 24,360 bytes.
- Current firmware binary: 604,208 bytes; SHA-256 `d3eb99a9f81f083a30af2ba0d026021ed8fa89d290d8042e28165466b76e0c4f`.
- New `validate_touch.py` regression exercises the **actual configured SensorLib
  transform** using raw-coordinate injections, then checks LVGL hit targets.
- Raw (233,83) hit Apps at display (233,383): **PASS**.
- Raw (319,298) hit the upper-left Calculator target: **PASS**.
- Raw (147,176) hit the lower-right Settings target: **PASS**.
- Raw-coordinate calculator taps produced 1 + 2 = 3: **PASS**, screenshot inspected.
- Evidence: [transcript](docs/evidence/touch-regression.json) and
  [calculator screenshot](docs/screenshots/raw-touch-calculator.png).
- Physical finger confirmation of the corrected firmware is still pending;
  injected raw coordinates test the mapping but do not synthesize sensor packets.

Run `python tools/validate_touch.py --port /dev/cu.usbmodem101 --output /path/to/results`
from the port directory. It leaves the device on the digital clock.

## Button wake timeout correction (2026-09-11)

The user reported that short presses could turn the display off but not back on.
The loop cached `now` before processing buttons. `wake()` restored panel brightness
and assigned a later `lastActivity = millis()`. The subsequent unsigned
`now - lastActivity` then wrapped and immediately turned the display off again.
Serial `wake` ran after the timeout check, so the original serial power regression
did not cover this failure.

Timeout evaluation now samples `millis()` after button and navigation handlers;
touch/swipe activity also records current time instead of reusing the earlier
loop timestamp. Unsigned elapsed subtraction is retained for clock rollover.
The README sequence diagram was checked against the final loop ordering.

- Build: **PASS**; static RAM 24,368 bytes; linked flash usage 604,164 bytes.
- Firmware: 604,528 bytes; SHA-256
  `27853660127e7cedda5919b4920428d045bcc54f09a28e97fe2f4512e847f173`.
- Device flash: **PASS**, bootloader, partitions and application hashes verified.
- Four host USB transport tests: **PASS**.
- On-device `validate_buttons.py`: **PASS**, 10 PWR off/on cycles, BOOT wake
  without a page change, BOOT launcher/clock navigation, configured 15-second
  automatic timeout, and PWR wake after that timeout.
- 13 injected events recorded activity timestamps newer than the cached loop
  timestamp. For example, `sampled=6815 activity=6816` would produce unsigned
  elapsed time 4,294,967,295 ms in the old timeout expression; the corrected
  device remained awake.
- Evidence: [button regression transcript](docs/evidence/button-regression.json).
- Test events are queued into the physical button handling branches before the
  real timeout check. Status observations do not call `wake` or `capture`.
  They test application behavior, not GPIO input, PMIC IRQ generation, or optical
  panel brightness.
- Physical confirmation: the user pressed PWR to turn the display off and pressed
  it again, then confirmed that the display now wakes normally.

## Three watch faces and animated character badge (2026-09-11)

Added Orbit, Studio and Pulse to the watch-face carousel and Badge to the
launcher. Badge contains the original Mochi, Beep and Lil' Orbit characters with
animation, tap reactions and an optional scoped stay-on wake lock. Watch-face
selection and badge character are stored in separate NVS namespaces. Navigation
from physical and diagnostic swipes now shares one dispatcher; existing page IDs
0–6 remain unchanged. Settings action IDs were moved outside the page-ID range.

The README architecture and lifecycle diagrams were reviewed against the final
implementation. Each artwork screen owns its LVGL object and refresh task;
leaving Badge also releases its wake lock. The final visual adjustment bounds
the moon's entire orbit, including its radius and bob, above the caption.

- Final build: **PASS**, RAM 24,504 bytes, linked flash 632,736 bytes.
- Final firmware: 633,104 bytes, SHA-256
  `f66eb8cfd030a515c017206ccc8a9b72c72a3e15f31b1edce84ce121f2ab230d`.
- Final flash: bootloader, partition table and app hashes verified.
- Four host transport tests and `git diff --check`: **PASS**.
- Full on-device artwork regression: **PASS** on the version before the final
  planet geometry adjustment. All five watch faces wrap in the carousel; BOOT
  returns to the selected watch face; each new watch face sleeps/wakes with PWR.
- Raw sensor-coordinate taps open Badge, trigger all three character reactions
  and enable stay-on. Badge stays awake beyond the user's **60-second** timeout;
  PWR can still turn it off and BOOT wakes the same page.
- Exiting a pinned Badge releases the wake lock; the launcher subsequently sleeps
  at the configured timeout. Re-entering Badge restores the character with
  stay-on disabled.
- Eight cycles through pages 7, 8, 9, 10, 2, 0: digital-page free heap stabilized at
  **328,024 bytes**. No restart or accumulated screen-resource leak observed.
- Final-firmware smoke check: **PASS**. The chosen Orbit face and planet character
  survive flash/reset, stay-on starts disabled, planet tap reaction works, PWR
  toggles off/on, and BOOT exits Badge and returns from the launcher to Orbit.
- Device framebuffer captures for the three new faces, all character themes and
  their reactions, and the updated launcher were visually inspected. Final planet
  normal/reaction frames were recaptured after the geometry adjustment.
- Evidence: [full regression](docs/evidence/artwork-regression.json),
  [final smoke check](docs/evidence/artwork-final-smoke.json), and the README previews.

The initial USB connection allowed flashing but stopped returning application
traffic; reconnecting the USB cable restored communication without a firmware
change. Diagnostic capture blocks GUI processing during transfer, so the
regression waits for the injected pointer's release/reaction instead of treating
command acknowledgment as a completed click.

These are software-input tests on the real board and framebuffer checks. They do
not replace the user's optical or physical touch assessment of the new screens.
Brightness, timeout and clock settings were preserved. The final screen is Orbit.

## Five pocket applications (2026-09-11)

Added Lucky Dice (dice/fortune/yes-no), Gravity marble physics, microphone-driven
Sound Buddy, persistent Garden focus/rest timers, and local phone photo upload.
The launcher has three pages. The README Mermaid diagram was reviewed against
sensor ownership, the background timer and HTTP-worker/main-task boundaries.
Shared artwork drawing primitives were extracted without changing existing faces.

- Final build: **PASS**, static RAM 47,480 bytes, linked flash 1,217,464 bytes.
- Final firmware: 1,217,824 bytes, SHA-256
  `1dd4fe47bf7a32ae5d89462c9ad09b75b727fd45e670f630c0b7558206cd128a`.
- Flash: bootloader, partition table and app hashes verified. NVS and LittleFS
  were retained; the user's uploaded photo still reports `image=1` after flashing
  and subsequent resets. No private photo or AP-password screenshot is included.
- Host logic: **PASS**, pause/resume, once-only flower reward, rest/reset and
  64-bit timer deadlines; 20,000 bounded physics steps and wall/bumper collisions;
  silence/DC rejection and a correctly resolved 1 kHz spectrum tone.
- Four host USB transport tests, clang-format checks and `git diff --check`: **PASS**.
- Phone crop UI: selection, zoom and drag visually checked at 393×852. The user
  connected to the actual board's AP and confirmed **upload and save succeeded**.
- Microphone: the new I2S driver failed GDMA initialization because its callback
  context landed in PSRAM. The pinned manufacturer's legacy I2S path initializes
  successfully; its SDK deprecation warning is documented rather than suppressed.
- The 1.75C schematic shows ADC1/2 microphones and SDOUT1 → GPIO10. Corrected the
  example's ADC3/4 gain selection to ADC1 at 37.5 dB with ONLY_LEFT mono capture,
  giving 16 kHz samples to the spectrum analyzer. Other ADC channels are disabled.
- Live microphone samples advanced continuously. During the user's speech,
  observed RMS rose from about 0.001 to 0.03547 (full-scale normalized). The user
  confirmed **robot and spectrum changes are obvious**. Only numeric diagnostics
  were retained; no microphone recording was saved.

An initial lifecycle test read status after a fixed 0.7-second wait and failed
while a photo was loading. The delayed reply showed the page transition had
completed and the normal inactivity timeout still operated; no reboot occurred.
The regression now waits for actual page/status conditions with a bounded timeout.

The full device regression before the final axis/header changes **passed**:
all three launcher pages and five
applications, dice modes/taps, real IMU/audio sample progression, microphone PWR
off/on, scoped stay-on beyond 15 seconds and normal timeout after exit, background
focus/pause/reset, and photo AP open/close/re-entry. Six complete IMU → microphone
→ photo AP → launcher cycles each ended with **288,452 bytes** free heap. Sensor
valid flags cleared on exit and the stored photo remained present.
See [device transcript](docs/evidence/play-regression.json) and
[speech diagnostics](docs/evidence/play-audio-live.json).

The user's physical tilt test exposed the original Gravity mapping: holding the
screen upright with readable text made the ball roll left. The two axes were
interchanged. The follow-up physical test confirmed vertical motion and exposed
an inverted horizontal sign. Gravity maps calibrated sensor readings to
`screenX = sensorY`, `screenY = sensorX`. Host checks cover
neutral calibration and all four signed directions. The architecture diagram
was updated accordingly; diagnostics also expose the stored neutral offsets.

[Smoke test before the final horizontal sign change](docs/evidence/play-final-smoke.json): **PASS**. The
stored photo loads, the focus timer continues while the display is off, BOOT
wakes the same page, pause/reset restores the original ready timer, microphone
blocks advance, and IMU/audio stop on exit. The final Garden title was moved
inside the decorative ring and its device-rendered frame was checked again.

After flashing the final horizontal correction, the user confirmed **all tilt
directions are now correct**. Final host direction/physics/timer/spectrum checks
passed, flash hashes verified, and [live Gravity diagnostics](docs/evidence/play-gravity-live.json)
showed real motion and bumper scoring. The device was left in Gravity for play.

Physical sensor readings and user confirmations are distinct from diagnostic
touch/button injections. CRC-mismatch and interrupted HTTP uploads have been
reviewed in code but have not been fault-injected over the physical AP.

## Scan-to-connect photo portal (2026-09-11)

Photo Badge generates a standard Wi-Fi QR for each session and a second QR for
the local photo page. An associated phone switches the display to the page QR;
the switch row also works manually. Tapping the QR itself leaves its controls
available. Captive DNS is advertised by DHCP, answers A queries with the AP IP,
and returns empty AAAA answers. HTTP connectivity probes redirect to the local
page. The README sequence diagram was checked against startup, client detection,
code switching, HTTP handling and socket shutdown. Automatic opening depends
on the phone's network-assistant behavior; the URL QR remains available.

- Final build: **PASS**, static RAM 47,480 bytes, linked flash 1,228,272 bytes.
- Firmware: 1,228,640 bytes, SHA-256
  `49cf886bb080adef00add820fe2b8be520e9549e9a0378ed7f3bdab877e90b11`.
- Flash: bootloader, partitions and application hashes verified. The uploaded
  photo remains present through reset and repeated portal sessions.
- Independent ZXing decoding: host-rendered Wi-Fi and URL payloads **PASS**;
  29-module Wi-Fi QR at 6 pixels/module, 25-module URL QR at 7 pixels/module,
  both with four white modules of quiet zone.
- On-device QR regression: four cycles **PASS**, actual framebuffer Wi-Fi QR
  decoding, session-password rotation, code switching, actual URL QR decoding,
  QR-center taps, close/reopen and photo retention. Live Wi-Fi frames were decoded
  in private temporary directories and deleted; no credentials were logged.
- Host captive DNS checks: **PASS**, A answer and transaction ID, empty AAAA
  answer, insufficient output capacity, truncated input, invalid/compressed labels,
  response-packet and multiple-question rejection.
- Four USB transport tests and port-owned clang-format/diff checks: **PASS**.
  The unmodified Nayuki sources retain upstream formatting (including trailing
  whitespace), so vendor sources are excluded from the port whitespace check.
- Four portal cycles each ended at **290,692 bytes** free heap. See the
  [QR regression](docs/evidence/photo-qr-regression.json) and
  [password-free URL QR preview](docs/screenshots/photo-page-qr.png).
- During the phone test, `clients` changed to 1, the displayed code changed to
  `page`, and the DNS response counter reached 73. The
  [numeric connection trace](docs/evidence/photo-qr-phone.json) contains no query
  names or credentials. This proves connection/detection/DNS activity; whether
  the phone automatically displayed the page requires the user's observation.


## Badge Studio iOS companion (2026-09-16)

- ESP32 firmware build: **PASS**; image 1,854,128 bytes, SHA-256
  `3e7e17c352560a936554efbb25577fcfe69b9ad47a562fd85c0f8a6fcdc5ee6a`.
- Signed iOS device build: **PASS**, Xcode 27 beta / Swift 6, deployment target
  iOS 17. Installed and launched on the connected iPhone 17 Pro running iOS 27.
  No external iOS packages. Only Xcode's unused App Intents metadata warning remains.
- Actual first connection: BLE pairing completed; device telemetry reported
  `connected=1 paired=1`, successful command acknowledgements and a valid clock.
  Watch/character changes were received with successful results in the user's
  first interaction session. This is device telemetry, not an automated UI test.
- After updating and restarting the app/device, the saved bond reconnected,
  time synchronization was acknowledged and `notify_errors=0`. The UTC anchor
  also survived the observed firmware/USB reset. Numeric-only
  [phone trace](docs/evidence/companion-phone.json) contains no passkeys or photos.
- Firmware host checks: **PASS**, actual PhotoStore compiled against test doubles.
  Covered source ownership, transfer IDs, offsets, invalid CRC, cancellation,
  timeout, write/close/rename failures, old-photo retention and successful commit.
- Swift host checks: **PASS**, independent CRC32 golden vector, wire endianness,
  invalid status frames, RGB565 primary colors, crop bounds and preview/output parity.
- Existing captive DNS checks and four USB transport tests: **PASS**.
- Full-area hit testing is now explicit on custom button labels, style cards,
  photo picker/send controls and navigation cards. Small sync icon has a 44-point
  hit target. The user reported text-only activation in the initial build;
  the fix was compiled and installed. Physical confirmation is still pending.
- Physical BLE photo upload/visual orientation and the updated button hit regions
  still require user confirmation. No automated E2E or AI verify was run.
- Mermaid diagrams were checked against command queue ownership, main-loop clock
  and settings updates, guarded GATT snapshots, native copying notifications,
  shared photo receiver, durable commit and app acknowledgement handling.

Bootloader/partition/application flashing preserves NVS and LittleFS. Personal
photos, original backup, signing profiles and device pairing codes are not part
of the source commit. The new shared store also serves the existing Wi-Fi page;
its network transport was not manually re-tested in this session.

### Native photo pinch zoom (2026-09-16)

- Replaced the photo slider and SwiftUI drag accumulator with a native iOS
  `UIScrollView` crop viewport: one-finger pan, focal-point two-finger pinch,
  1–4× zoom and no bounce beyond image bounds. Gestures are disabled during upload.
- The decorative crop ring is excluded from hit testing. Viewport content offset
  maps back to the existing normalized crop used for RGB565 export. Delegate
  publication is suppressed during programmatic viewport configuration.
- Added 36 round-trip/bounds cases across portrait, landscape and square images,
  multiple zoom levels and edge offsets. Swift host checks **PASS**; signed iOS
  build **PASS**. Crop-state Mermaid diagram reviewed against implementation.
- No firmware changes, automated E2E or AI verify. Physical two-finger gesture
  feel and behavior with VoiceOver have not been verified in this session.
