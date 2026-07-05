# WAG One

Wear your GIFs. WAG One is a wearable pendant / earring / pin / ring that loops your favorite GIFs on a tiny round display, built around an ESP32-C3 (V2 with ESP32-C6 incoming with the new PCB)

## Gallery

![v2 pin cycling through multiple GIFs](docs/media/v2-pin-multigif.gif)

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware](#hardware)
- [How It Works](#how-it-works)
- [Getting Started](#getting-started)
  - [Requirements](#requirements)
  - [Wiring / Pinout](#wiring--pinout)
  - [Flashing the Firmware](#flashing-the-firmware)
  - [First Boot](#first-boot)
- [Web Interface](#web-interface)
- [API Reference](#api-reference)
- [Project Structure](#project-structure)
- [Roadmap](#roadmap)
- [Troubleshooting](#troubleshooting)
- [License](#license)
- [Contributing](#contributing)
- [Credits](#credits)

## Overview

WAG One turns a tiny round display (160x160, GC9D01) into a wearable accessory that plays your favorite GIFs on loop. Upload GIFs from your phone through a built-in web interface, pick the screen orientation, then go fully standalone — no Wi-Fi needed once your GIFs are loaded onto the device.

The current build runs on an off-the-shelf Waveshare ESP32-C3-LCD-0.71 board. A custom PCB (v2) is in development for a more compact, battery-powered wearable form factor, but is not yet available in this repo — only code, photos and GIFs of the current prototype are published for now.

## Features

- Upload multiple GIFs at once from any phone or laptop, no app required
- Live storage bar showing flash usage before you run out of space
- Screen rotation toggle (normal / 90°), saved across reboots
- Fully offline playback loop once GIFs are on the device (Wi-Fi off = better battery life)
- One-button (BOOT) re-entry into Wi-Fi/config mode without re-flashing
- Runs on a $5-ish dev board, no exotic parts required

## Hardware

| Component | Detail |
|---|---|
| Board | Waveshare ESP32-C3-LCD-0.71 |
| Display | GC9D01 driver, 160x160, round |
| MCU | ESP32-C3 (RISC-V, single-core, Wi-Fi + BLE) |
| Storage | Internal flash via LittleFS (~4 MB, partially reserved for firmware) |
| Enclosure | 3D-printed (STL files in `/hardware/stl`) |
| Power | USB-powered on v1; LiPo + charging circuit planned for v2 PCB |

v2 will ship a custom PCB designed specifically for wearables: smaller footprint, integrated LiPo charging, and a form factor suited for pendants, rings and pins. Design files will be added to this repo once finalized — for now this is a v1 / proof-of-concept release built on the Waveshare dev board.

## How It Works

1. On boot, the screen shows "WAG One" centered on the display.
2. The device tries to join a pre-configured Wi-Fi hotspot (2-minute timeout).
3. If it connects, the IP address is shown on screen and a lightweight web server starts.
   - From your phone: upload several GIFs, pick screen rotation, check remaining flash storage, wipe all GIFs, or turn Wi-Fi off.
4. If it fails to connect within 2 minutes (or you tap "Turn off Wi-Fi"), the radio is shut down and the device loops through the GIFs stored in flash.
5. Pressing the physical BOOT button during playback re-opens Wi-Fi/config mode so you can swap GIFs again.

## Getting Started

### Requirements

- Waveshare ESP32-C3-LCD-0.71 board
- Arduino IDE or PlatformIO
- Libraries: `TFT_eSPI`, `lvgl` (v8.x), `LittleFS`, `Preferences`
- A 2.4 GHz Wi-Fi hotspot to configure GIFs (your phone's hotspot works fine)

### Wiring / Pinout

No external wiring is required for v1 — the display and buttons are already integrated on the Waveshare board. Only the BOOT button (GPIO9) is used by the firmware to re-trigger the config portal.

### Flashing the Firmware

1. Clone this repository.
2. Open `firmware/wag_one.ino` in Arduino IDE or PlatformIO.
3. Install the required libraries listed above through the Library Manager.
4. Configure `TFT_eSPI`'s `User_Setup.h` for the GC9D01 driver (a reference config is provided in `firmware/User_Setup`).
5. Edit `WIFI_SSID` and `WIFI_PASS` in the code to match the hotspot you'll use to upload GIFs.
6. Select the correct ESP32-C3 board profile and flash.

### First Boot

On first boot the device will try to join your configured Wi-Fi for up to 2 minutes. Once connected, its IP address appears on screen — open that address in your phone's browser to reach the upload portal.

## Web Interface

The built-in portal (served directly from the ESP32, no cloud, no account) lets you:

- Drag-and-drop or pick multiple `.gif` files to upload
- See live upload progress and per-file status (OK / error)
- Monitor flash storage usage with a visual bar
- Switch screen orientation between normal and 90°
- Wipe all stored GIFs
- Turn Wi-Fi off and start standalone playback immediately (5 to 6 hours of playback using a 200mah LiPo battery)

## API Reference

The web portal talks to a few simple REST-ish endpoints exposed by the firmware, useful if you want to script uploads or build your own client:

| Method | Endpoint | Description |
|---|---|---|
| GET | `/` | Serves the web UI |
| GET | `/list` | Returns JSON with flash usage, free space, GIF count, and current rotation |
| POST | `/setrot` | Sets screen rotation (`v=0` or `v=1`) |
| POST | `/clearall` | Deletes all stored GIFs |
| POST | `/wifioff` | Shuts down Wi-Fi and starts playback loop |
| POST | `/upload` | Multipart file upload for a single GIF |

## Project Structure

```
wag-one/
├── README.md
├── LICENSE              (MIT, firmware/software)
├── LICENSE-HARDWARE     (CC BY-NC-SA 4.0, hardware design)
├── .gitignore
├── firmware/
│   └── wag_one.ino
├── hardware/
│   └── stl/             (3D-printable enclosures)
└── docs/
    └── (photos, demo GIFs)
```

## Roadmap

- [ ] STL files for pendant, pin, and ring enclosures
- [ ] Custom v2 PCB (schematics + gerbers)
- [ ] Integrated LiPo battery management
- [ ] Dedicated mobile app (replacing the web portal)
- [ ] Multi-device sync for matching sets (e.g. pendant + earrings)

## Troubleshooting

- **Device won't connect to Wi-Fi**: double-check `WIFI_SSID`/`WIFI_PASS` match your hotspot exactly, and that it's on 2.4 GHz (the ESP32-C3 doesn't support 5 GHz).
- **Upload fails or GIF doesn't play**: make sure the file is a valid `.gif` and that you have enough free flash space (check the storage bar).
- **Screen stays black**: verify the `TFT_eSPI` `User_Setup.h` matches the GC9D01 pin mapping for the Waveshare board.
- **BOOT button does nothing during playback**: hold it briefly (a short press, not a long hold) — the debounce window is intentionally tight.

## License

This project uses a dual license: one for software, one for hardware.

- **Firmware / code** (`/firmware`): [MIT](LICENSE) — free to use, modify, and redistribute, including commercially.
- **Hardware design** (`/hardware`: schematics, CAD, STL files, product visuals): [CC BY-NC-SA 4.0](LICENSE-HARDWARE) — free to build, modify, and share for personal or non-commercial use, as long as you credit the original project and share your modifications under the same license. **Commercial resale of the designs or manufactured units derived from them is not permitted without prior written permission.**

Assembled PCBs and official kits are sold directly by the project maintainer. If you want to build your own for personal use, go for it — that's exactly what this is for.

## Contributing

Firmware pull requests are welcome (MIT licensed, low friction). For hardware changes, please open an issue first so we can coordinate with the ongoing v2 PCB design before merging anything major.

## Credits

Designed and built by Rob1. Built on top of the Waveshare ESP32-C3-LCD-0.71 board, `TFT_eSPI`, and `lvgl`.
