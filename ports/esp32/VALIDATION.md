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

## Build

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

## On-device results

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
