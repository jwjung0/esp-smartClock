# esp-smartClock

ESP32 digital clock with on-device temperature and humidity. Firmware is built and flashed from Cursor with PlatformIO (Arduino framework).

## Hardware

| Part                | Role                     |
| ------------------- | ------------------------ |
| ESP32 DevKit        | MCU, Wi‑Fi, Bluetooth    |
| 1.54" LCD (240×240) | Time and sensor display  |
| BME280              | Temperature and humidity |

## Features

**Now**

- PlatformIO project for ESP32
- Selectable firmware modes: `checkup` (LED + serial) and `app` (LVGL clock UI)
- Board check: onboard LED blink + serial heartbeat
- Clock UI on the 1.54" LCD (SquareLine / LVGL)
- Wi‑Fi provisioning via local HTTP portal (SoftAP + STA)
- NTP time sync in **KST (UTC+9)** → live `ui_time` / `ui_date`
- Power policy: **10 min** config window after boot, then Wi‑Fi off; **daily** NTP resync

**Planned**

- Live temperature and humidity from BME280

## Wi‑Fi + NTP setup

1. Flash app firmware: `./scripts/flash.sh app`
2. On first boot (or if saved Wi‑Fi fails), the device opens SoftAP **`SmartClock`**.
3. Join that AP on your phone, then open **http://192.168.4.1/** (captive portal usually redirects there).
4. Pick a network from the scan list (or type SSID), enter the password, tap **Connect**.
5. After join, open **http://smartclock.local/** on the same Wi‑Fi (mDNS). Fallback: LAN IP from Serial.
6. SoftAP setup stays **http://192.168.4.1/**.
7. Portal stays up for **10 minutes after boot**, then Wi‑Fi turns off. Power-cycle for another config window.
8. NTP resyncs about **once per day** (brief Wi‑Fi wake, then off again). Time zone: **KST**.

Credentials are stored in NVS (`Preferences` namespace `wifi`).

## Firmware modes

| Mode      | Env          | What runs                                      |
| --------- | ------------ | ---------------------------------------------- |
| `checkup` | `-e checkup` | `board_check` — LED ON/OFF every 1s + serial   |
| `app`     | `-e app`     | `clock_app` — LVGL UI, Wi‑Fi portal, NTP clock |

Selected at **compile time** via `build_flags` in `platformio.ini`.

## Working flow

1. Edit firmware in Cursor (`src/`, `platformio.ini`).
2. Install PlatformIO Core on your machine (once):
   ```bash
   brew install platformio
   # or: pip install platformio
   ```
3. Connect the ESP32 over USB and note the serial port (`ls /dev/cu.*`).
4. From the repo root:
   ```bash
   ./scripts/build.sh checkup   # compile only (default: checkup)
   ./scripts/build.sh app       # compile app firmware only
   ./scripts/flash.sh checkup   # compile + upload board check (default)
   ./scripts/flash.sh app       # compile + upload app firmware
   ./scripts/checkup.sh         # flash checkup + serial monitor
   ```
   Or use PlatformIO directly:
   ```bash
   pio run -e checkup
   pio run -e checkup -t upload
   pio device monitor -e checkup
   ```
5. Board check OK when the LED blinks every 1s and the monitor prints `LED ON` / `LED OFF`.

First `pio run` downloads the ESP32 platform and toolchain automatically.

## Project layout

```
platformio.ini              # shared settings + checkup / app envs
src/main.cpp                # setup/loop — switches on FIRMWARE_MODE_*
src/board_check.h/.cpp      # LED + serial smoke test
src/clock_app.h/.cpp        # LVGL clock app
src/wifi_config.h/.cpp      # SoftAP/STA portal, NVS credentials
src/time_sync.h/.cpp        # NTP (KST) + UI time/date formatters
src/loading_gif.h/.c        # embedded assets/loading.gif (regenerate via script)
assets/loading.gif          # loading animation source
scripts/embed_loading_gif.py  # python3 scripts/embed_loading_gif.py
scripts/build.sh            # ./scripts/build.sh [checkup|app] — compile only
scripts/flash.sh            # ./scripts/flash.sh [checkup|app] — compile + upload
scripts/checkup.sh          # flash checkup + serial monitor
```

On boot the app shows the SquareLine **loading** screen (animated GIF) while joining Wi‑Fi and syncing NTP, then switches to **home**.

## Board config

Shared `board = esp32dev` under `[env]`. Change that if your DevKit uses a different PlatformIO board id.

App partition scheme is `huge_app.csv` (larger app, smaller SPIFFS) so LVGL + Wi‑Fi fit in flash.
