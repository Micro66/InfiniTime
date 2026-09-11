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
