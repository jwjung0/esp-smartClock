# esp-smartClock

ESP32 digital clock with on-device temperature and humidity. Firmware is built and flashed from Cursor with PlatformIO (Arduino framework).

## Hardware

| Part                       | Role                            |
| -------------------------- | ------------------------------- |
| MCU (pick one at build)    | Wi‑Fi; see boards below         |
| 1.54" LCD (240×240 ST7789) | Time and sensor display         |
| BME280, or BMP280 + AHT20  | Climate (T / RH / P)            |
| VEML7700                   | Ambient lux → LCD backlight PWM |

Supported boards (pin maps in `include/board_pins_*.h`):

| Short name | PlatformIO board | Notes                |
| ---------- | ---------------- | -------------------- |
| `s2mini`   | `lolin_s2_mini`  | ESP32-S2, native USB |
| `esp32dev` | `esp32dev`       | Classic ESP32 DevKit |

**LCD (SPI)** — panel **SDA/SCL** = MOSI/SCLK (not I2C).

| LCD | `s2mini` GPIO | `esp32dev` GPIO |
| --- | ------------- | --------------- |
| VCC | 3V3           | 3V3             |
| GND | GND           | GND             |
| SDA | 11 (D7)       | 23 (D23 / MOSI) |
| SCL | 7 (D5)        | 18 (D18 / SCK)  |
| CS  | 13 (D8)       | 5 (D5 / SS)     |
| DC  | 5 (D0)        | 16 (RX2)        |
| RST | 3 (A0)        | 17 (TX2)        |
| BL  | 16 (D4)       | 22 (D22)        |

LCD panel **SDA/SCL are SPI** (MOSI/SCLK), not the BME280 I2C pins. VCC must be **3V3** (not 5V). If your module has no CS pin, leave ESP `D5` unused and set `PIN_TFT_CS` to `-1` in `include/board_pins_esp32dev.h`. On classic ESP32, do **not** use GPIO 25–27 for DC/RST — those are ADC2 and conflict with Wi‑Fi (display can freeze after HTTP).

**Climate (I2C)** — auto-detects **BME280** (`0x60`), **BMP280** (`0x58`, T+P), and/or **AHT20** (`0x38`, T+RH). Common “temp+humid” modules are **BMP280 + AHT20** on the same bus; firmware uses BMP for T/P and AHT for RH.

| BMx280 | `s2mini` GPIO | `esp32dev` GPIO |
| ------ | ------------- | --------------- |
| VCC    | 3V3           | 3V3             |
| GND    | GND           | GND             |
| SDA    | 33 (D2)       | 21 (D21)        |
| SCL    | 35 (D1)       | 19 (D19)        |

Six-pin modules also have **CSB** / **SDO** (sometimes silk **CSR** / **SDK**). Leave them at **board default** (usually unconnected). Do not wire those two to ESP GPIOs.

**Ambient light (I2C)** — **VEML7700** (`0x10`) drives LCD backlight brightness (PWM on `PIN_TFT_BL`). Not uploaded to climate ingest.

| VEML7700 | `s2mini` GPIO    | `esp32dev` GPIO    |
| -------- | ---------------- | ------------------ |
| VCC      | 3V3              | 3V3                |
| GND      | GND              | GND                |
| SDA      | 33 (D2) — shared | **32** (dedicated) |
| SCL      | 35 (D1) — shared | **33** (dedicated) |

On **s2mini**, share the climate I2C bus. On **esp32dev**, use a second bus (`Wire1`) so climate stays on 21/19.

## Features

**Now**

- PlatformIO project for **S2 Mini** and **ESP32 DevKit** (board chosen at build time)
- App firmware is the default (LVGL clock UI); optional `checkup` mode for LED + LCD + serial smoke test
- Board check: onboard LED blink, LCD color cycle, serial heartbeat
- Clock UI on the 1.54" LCD (SquareLine / LVGL)
- Wi‑Fi provisioning via local HTTP portal (SoftAP + STA)
- NTP time sync in **KST (UTC+9)** → live `ui_time` / `ui_date`
- Power policy: **10 min** config window after boot, then Wi‑Fi off; **daily** NTP resync; **every 10 min** climate data ingest (brief Wi‑Fi wake → POST → Naver weather + OpenWeather GET → Naver market GET → off). With radio off: CPU **80 MHz**, home loop idles until the next **1 Hz** colon/time tick (light sleep when auto-BL is off; timed wait when auto-BL PWM must stay running). Radio on → CPU **240 MHz**. Indoor auto-BL curve caps ~**70%** duty.
- Climate ingest deploy targets: **local** (this Mac) vs **prod** (Ubuntu) via gitignored `include/ingest_config_*.h`
- Live indoor T/RH from BME280 on `ui_temp*` / `ui_humid`
- Ambient lux from VEML7700 → auto LCD backlight brightness (PWM; no ingest). Indoor log curve (~10% night floor, ~70% day cap). Config portal **Backlight** switch turns auto on/off (off = full brightness; stored in NVS `clockui` / `bl_auto`). **Weather** source stored in NVS `clockui` / `wx_src` (`naver` | `openweather`)
- Outdoor weather → `ui_current_temp`, `ui_current_humid`, image `ui_weather`: config portal **Weather** source (default **Naver**; optional **OpenWeather**). The other provider is used if the selected one fails. OpenWeather needs a free-tier 2.5 key (One Call 3.0/4.0 need a paid plan)
- Market ticker → scrolling Label `ui_market`: defaults **KS** / **KQ** / **Nasdaq** / **USD**, plus optional custom tickers from the config portal. **KS/KQ/USD** (+ custom) from **Naver Finance**; **Nasdaq** from **Yahoo** (`^IXIC`). Up = red, down = blue, flat = white

**SquareLine outdoor weather**

- Widgets: Image `ui_weather`, Labels `ui_current_temp` / `ui_current_humid` (+ `ui_current_celcus` / `ui_current_pct`)
- Assets: `weather_clear`, `weather-clouds`, `icon_cloudy_sun` / `icon_cloudy_night`, `weather-rain`, `weather-thunder`, `weather-snow`, `weather-atmosphere`
- **Naver:** maps KMA `wetrCd` / `wetrTxt` (맑음 → clear; 구름많음 → sun|moon+clouds; 흐림 → clouds; 비/눈/낙뢰/안개… → matching asset)
- **OpenWeather:** maps `weather[0].main` / `id` (`Rain`/`Drizzle` → rain, mist/fog/… → atmosphere, `Tornado` → thunder; Clouds `801`–`802` → sun|moon+clouds)
- Create gitignored `include/weather_config.h` with:
  - `OPENWEATHER_API_KEY` and lat/lon (default Seoul-ish `37.49056` / `127.02`)
  - `NAVER_WEATHER_REGION_CODE` (8-digit 읍·면·동). To get it: open your area on [weather.naver.com](https://weather.naver.com), then copy the code from the URL path — e.g. `https://weather.naver.com/today/09650107?...` → `09650107`

**SquareLine market ticker**

- Widget: Label `ui_market` (fixed width ~220–240 px, long mode **Scroll circular**, recolor on)
- Firmware overwrites text with colored segments, e.g. `KS 5730.74 -3.48%  KQ 724.38 -3.83%  Nasdaq 25437.00 -2.86%  USD 1335.80 -0.35%`
- Refreshes on boot (if STA up) and on the same ~10 min climate wake as weather
- **Config portal → Stock Market**: switch hides/shows the home scrollbar immediately; toggle default KS/KQ/Nasdaq/USD; add custom rows with English **Name** + Naver **Code** (e.g. `Samsung` / `005930`). Max 4 custom, 8 quotes total. Stored in NVS namespace `market`. Custom marquee uses the Name you enter (`Samsung 220,500 -5.17%`). Code kinds: 6-digit stock, `KOSPI`/`KOSDAQ`/`KPI200`, world index (`.IXIC`), FX (`FX_USDKRW`)

**Checkup**

- LCD color cycle + BL blink
- BME280 over I2C — Serial prints pin map, probe `OK`/`FAIL`, then `T` / `RH` / `P` each loop
- VEML7700 over I2C — probe `OK`/`FAIL`, then `lux=…` + `BL duty=` each loop (PWM follows lux; shared bus on s2mini; GPIO32/33 on esp32dev)
- Weather once at boot — STA from NVS (same credentials as app portal) → Naver KMA + OpenWeather HTTPS → Serial + LCD (`T` / `RH` / `main`), then Wi‑Fi off. Needs `include/weather_config.h` and a prior app Wi‑Fi provision

## Wi‑Fi + NTP setup

1. Flash app firmware (default): `./scripts/flash.sh` or `./scripts/flash.sh esp32dev local`
2. On first boot (or if saved Wi‑Fi fails), the device opens SoftAP **`SmartClock`**.
3. Join that AP on your phone, then open **http://192.168.4.1/** (captive portal usually redirects there).
4. Pick a network from the scan list (or type SSID), enter the password, tap **Connect**.
5. After join, open **http://smartclock.local/** on the same Wi‑Fi (mDNS). Fallback: LAN IP from Serial.
6. SoftAP setup stays **http://192.168.4.1/**.
7. Portal stays up for **10 minutes after boot**, then Wi‑Fi turns off. Power-cycle for another config window.
8. NTP resyncs about **once per day** (brief Wi‑Fi wake, then off again). Time zone: **KST**.
9. After the config window, climate readings POST about **every 10 minutes** (Wi‑Fi on → POST → Naver weather + OpenWeather GET → Naver market GET → off). Set URL/token in gitignored `include/ingest_config_local.h` / `include/ingest_config_prod.h`.
10. Same wake fetches outdoor weather (Naver KMA preferred, OpenWeather fallback) and market quotes (Naver). Set keys/region in gitignored `include/weather_config.h`.

Credentials are stored in NVS (`Preferences` namespace `wifi`). UI colors + backlight auto use `clockui`; market defaults/customs use `market`.

## Firmware envs

App is default — scripts take **board** + **deploy** only (do not pass `app`).

| Mode      | Board      | Deploy  | Env (`-e`)           | What runs                         |
| --------- | ---------- | ------- | -------------------- | --------------------------------- |
| `app`     | `esp32dev` | `prod`  | `app_esp32dev_prod`  | Clock + ingest → Ubuntu (default) |
| `app`     | `esp32dev` | `local` | `app_esp32dev_local` | Clock + ingest → Mac LAN          |
| `app`     | `s2mini`   | `prod`  | `app_s2mini_prod`    | same on S2 Mini                   |
| `app`     | `s2mini`   | `local` | `app_s2mini_local`   | same on S2 Mini                   |
| `checkup` | `esp32dev` | —       | `checkup_esp32dev`   | LED + LCD color cycle + serial    |
| `checkup` | `s2mini`   | —       | `checkup_s2mini`     | same                              |

Selected at **compile time** in `platformio.ini`.

## Working flow

1. Edit firmware in Cursor (`src/`, `include/ingest_config_*.h`, `include/weather_config.h`, `platformio.ini`).
2. Install PlatformIO Core on your machine (once):
   ```bash
   brew install platformio
   # or: pip install platformio
   ```
3. Connect the ESP32 over USB and note the serial port (`ls /dev/cu.*`).
4. From the repo root (defaults: **esp32dev** + **prod**):
   ```bash
   ./scripts/build.sh
   ./scripts/build.sh esp32dev local
   ./scripts/flash.sh
   ./scripts/flash.sh esp32dev local
   ./scripts/flash.sh s2mini prod
   ./scripts/flash.sh checkup esp32dev
   ./scripts/checkup.sh esp32dev
   ```
   Or use PlatformIO directly:
   ```bash
   pio run -e app_esp32dev_prod
   pio run -e app_esp32dev_local -t upload
   pio run -e checkup_esp32dev
   pio device monitor -e checkup_esp32dev
   ```
5. Board check OK when the LED blinks every 1s, the LCD cycles solid colors (RED/GREEN/BLUE/…), and the monitor prints `LED ON` / `LED OFF` plus `LCD fill …`. If backlight is on but colors never change, re-check SPI wires (MOSI/SCLK/CS/DC/RST) — BL alone only proves power + BL pin.

First `pio run` downloads the ESP32 platform and toolchain automatically.

## Project layout

```
platformio.ini              # envs: app_<board>_<local|prod>, checkup_<board>
include/board_pins_*.h      # per-board LCD / BME280 / VEML7700 / LED pins
include/ingest_config_*.h   # local vs prod URL/token (gitignored)
include/weather_config.h    # OpenWeather key/lat/lon + Naver region code (gitignored)
src/main.cpp                # setup/loop — switches on FIRMWARE_MODE_*
src/board_check.h/.cpp      # LED + serial smoke test
src/clock_app.h/.cpp        # LVGL clock app
src/data_ingest.h/.cpp      # BME280 → HTTP climate ingest (10 min wake)
src/weather_fetch.h/.cpp    # Naver KMA + OpenWeather 2.5 (HTTPS; Naver preferred)
src/market_fetch.h/.cpp     # Naver Finance KS/KQ/Nasdaq/USD ticker (HTTPS)
src/wifi_config.h/.cpp      # SoftAP/STA portal, NVS credentials
src/time_sync.h/.cpp        # NTP (KST) + UI time/date formatters
src/bme_sensor.h/.cpp       # BME280 I2C read
src/light_sensor.h/.cpp     # VEML7700 lux
src/bl_ctrl.h/.cpp          # lux → LCD backlight PWM (app + checkup)
src/power_policy.h/.cpp     # CPU 80/240 MHz with radio off/on
scripts/build.sh            # ./scripts/build.sh [board] [local|prod]
scripts/flash.sh            # ./scripts/flash.sh [board] [local|prod]
scripts/checkup.sh          # ./scripts/checkup.sh [board]
```

On boot the app shows the SquareLine **loading** screen while joining Wi‑Fi and syncing NTP, then switches to **home**.

## Board config

App partition scheme is `huge_app.csv` (larger app, smaller SPIFFS) so LVGL + Wi‑Fi fit in flash.

S2 Mini uses USB CDC for serial. If upload fails, hold **BOOT**, tap **RST**, release **BOOT**, then retry.
