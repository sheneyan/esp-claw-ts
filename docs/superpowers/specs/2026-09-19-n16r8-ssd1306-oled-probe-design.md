# N16R8 SSD1306 OLED Probe Design

## Goal

Verify the connected 0.96-inch SSD1306 I2C OLED on the ESP32-S3 N16R8 without
changing the existing ESP-Claw TS network, Tailscale, FATFS, or provisioning
behaviour.

## Confirmed hardware wiring

The first validation contains only the display bus:

| OLED header | N16R8 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO5 |
| SDA | GPIO4 |

The OLED module header order is `GND VCC SCL SDA K4 K3 K2 K1`. Its four keys
are intentionally out of scope for this probe and remain unconnected.

GPIO0, GPIO3, GPIO19/20, GPIO33-37, GPIO38, and the UART RX/TX pins remain
reserved. GPIO6, GPIO7, GPIO15, and GPIO16 are reserved only as future key
candidates; this change does not configure them.

## Design

1. Add an I2C master peripheral to the N16R8 board profile at 100 kHz on
   SDA GPIO4 and SCL GPIO5.
2. Add a small OLED probe component that receives the board-managed I2C handle
   during application startup. It probes only the SSD1306 candidate addresses
   `0x3C` and `0x3D`.
3. If one address acknowledges, initialize that SSD1306 device, clear its
   128x64 monochrome framebuffer, and render a static success screen with the
   discovered address and a compact Wi-Fi/Tailscale-ready status indicator.
4. If neither address acknowledges, log a clear diagnostic and leave all
   existing ESP-Claw TS services running. A missing display is not fatal and
   cannot block Wi-Fi, Tailscale, Web IM, FATFS, or the provisioning button.
5. Do not add a full display framework, button handling, menu, storage/TF
   support, or Taildrop in this change.

## Safety and persistence

- Before first flashing, save a local full 16 MB flash backup outside the Git
  repository. It may contain credentials and must not be committed or shared.
- Do not use the project's default `idf.py flash` target: its generated flash
  manifest includes `storage.bin` and would overwrite the writable `/fatfs`
  partition. Flash only the rebuilt application image at `0x20000` with
  `esptool.py`; do not write `storage.bin`, `system.bin`, NVS, OTA data, or the
  partition table for this probe.
- Do not use `erase_flash`, factory reset, NVS erase, FATFS format, or a
  standalone replacement test application.
- Existing persisted Wi-Fi and Tailscale state, writable `/fatfs`, and the
  read-only system image must remain intact. This probe updates the application
  partition only.

## Verification

1. Confirm the board remains a Tailscale peer and its status API still reports
   Wi-Fi connected after flashing.
2. Capture serial evidence for either `SSD1306 found at 0x3C/0x3D` or an
   explicit no-device diagnostic.
3. Visually verify the success screen when a device is found.
4. Run the relevant host tests and a clean ESP-IDF 5.5.4 build before flashing.
5. Do not connect K1-K4 until the display path passes all of the above.

## Deferred decisions

- A periodic local status dashboard and four-key navigation.
- SD-card hardware, mount management, and format controls.
- Taildrop or other file-transfer capabilities.
