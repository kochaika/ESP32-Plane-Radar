# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** or a **Seeed Studio XIAO ESP32C3**, plus a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

## What's different in this fork

Forked from [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar).

| Change | Details |
|--------|---------|
| **Seeed XIAO ESP32C3 support** | The XIAO doesn't break out GPIO0/GPIO1, which upstream used for the display's RST and CS. Display pins are now `#ifndef`-guarded `PR_PIN_DISPLAY_*` macros in `include/config.h`, overridden per board from `platformio.ini`. New `xiao_c3` env (now the default); the original `supermini` env builds unchanged. See [Wiring](#wiring) |
| **South-up radar** | The plot is rotated so bearing 180° is at the top, with all text still upright. Driven by one constant, `kUpBearingDeg` in `include/ui/radar_projection.h` (`0` = north up). The projection math, previously duplicated between the radar and runway overlay, now lives in that shared module |
| **Metric altitude** | Aircraft tags show `457 m` / `10.7 km` instead of raw feet. Follows the units checkbox, which is relabelled **Imperial units (miles, feet)** since it now governs both distance and altitude |
| **Range selector in the web portal** | The four range presets are also pickable from a dropdown in the setup portal, not just by cycling the BOOT button. Both write the same NVS setting |
| **Build fix: LovyanGFX ≥ 1.2.26** | Upstream's `namespace fonts = lgfx::v1::fonts;` aliases collide with the real global `namespace fonts` that LovyanGFX added after 1.2.7, so the floating `^1.2.7` range stopped compiling. Aliases removed and the dependency floor raised |

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~5 s).

## Controls (BOOT, GPIO 9, active LOW — the onboard **B** button on the XIAO)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, units, or runway overlay; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set) |
| **Radar range** | Dropdown of the same four presets the BOOT tap cycles through; the stored one is preselected |
| **Imperial units (miles, feet)** | Ring scale in **mi** instead of **km** (`6mi` vs `10km`), and aircraft altitude in **ft** instead of **m / km** (`35000 ft` vs `10.7 km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Pick a preset either by tapping **BOOT** (cycles) or from the **Radar range** dropdown in the
setup portal — both write the same setting. Preset and units persist across reboot
(`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (5 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | `PR_PIN_DISPLAY_*` pin defaults (per-board overrides in `platformio.ini`), `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Radar orientation | `kUpBearingDeg` in `include/ui/radar_projection.h` — true bearing drawn at the top (`0` = north up, `180` = south up). Rotates the plot only; text stays upright |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_projection.h       — lat/lon → screen, and the south-up rotation
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Wiring

Pins live in `include/config.h` as `PR_PIN_DISPLAY_*` macro defaults (Super Mini values); the
`xiao_c3` env overrides them from `platformio.ini` build flags.

### GC9A01 ↔ ESP32-C3 Super Mini (env `supermini`)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

### GC9A01 ↔ Seeed XIAO ESP32C3 (env `xiao_c3`)

The XIAO does **not** break out GPIO0 or GPIO1, so the display is remapped. Of the 11 pins it
does expose, GPIO **2 / 8 / 9** are strapping pins and GPIO **20 / 21** are UART0 — leaving
exactly six usable pins (3, 4, 5, 6, 7, 10) for the display's five signals.

| Display | XIAO silk | GPIO |
|---------|-----------|------|
| VCC | 3V3 | — |
| GND | GND | — |
| RST | **D2** | 4 |
| CS | **D10** | 10 |
| DC | **D3** | 5 |
| SDA (MOSI) | **D5** | 7 |
| SCL (SCLK) | **D4** | 6 |
| user button | onboard **B** | 9 |

SCLK and MOSI sit on the ESP32-C3's SPI2 IOMUX pads (FSPICLK / FSPID), so the bus bypasses the
GPIO matrix — which caps SPI master at 40 MHz. D1/GPIO3 is left free.

> Don't be tempted by the XIAO's silkscreened **SCK** (D8/GPIO8): it's a strapping pin, and
> driving it low at reset prevents the board from entering download mode.

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO envs: **`xiao_c3`** (default) and **`supermini`** — pick one with `-e <env>`
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini`; both boards have native USB

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin` for the `xiao_c3` env. Pick a board with `--env`, and
skip the rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --env supermini
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/<env>/firmware-merged.bin`):

```bash
pio run -e xiao_c3
pio run -t merge -e xiao_c3
```

Put the board in download mode, then flash with Chrome/Edge over USB — hold **BOOT** and tap
**RESET** on the Super Mini, or hold **B** and tap **R** on the XIAO. Both boards flash over
native USB Serial/JTAG, so this is usually unnecessary.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `plane-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `plane-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32-C3, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — **≥ 1.2.26** (earlier versions lack the global `namespace fonts` this code relies on)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
