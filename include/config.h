#pragma once

#include <cstdint>

#include <driver/gpio.h>

// --- Board pin overrides (defaults = ESP32-C3 Super Mini) ---
// Boards that do not break out GPIO0/GPIO1 (e.g. Seeed XIAO ESP32C3) override
// these from platformio.ini build_flags; see [env:xiao_c3].
#ifndef PR_PIN_DISPLAY_RST
#define PR_PIN_DISPLAY_RST 0
#endif
#ifndef PR_PIN_DISPLAY_CS
#define PR_PIN_DISPLAY_CS 1
#endif
#ifndef PR_PIN_DISPLAY_DC
#define PR_PIN_DISPLAY_DC 10
#endif
#ifndef PR_PIN_DISPLAY_MOSI
#define PR_PIN_DISPLAY_MOSI 3
#endif
#ifndef PR_PIN_DISPLAY_SCLK
#define PR_PIN_DISPLAY_SCLK 4
#endif

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (active LOW) ---
// Super Mini: BOOT. XIAO ESP32C3: onboard "B" button. Both on GPIO9.
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = static_cast<gpio_num_t>(PR_PIN_DISPLAY_RST);
constexpr gpio_num_t kDisplayPinCs = static_cast<gpio_num_t>(PR_PIN_DISPLAY_CS);
constexpr gpio_num_t kDisplayPinDc = static_cast<gpio_num_t>(PR_PIN_DISPLAY_DC);
constexpr gpio_num_t kDisplayPinMosi = static_cast<gpio_num_t>(PR_PIN_DISPLAY_MOSI);  // display SDA
constexpr gpio_num_t kDisplayPinSclk = static_cast<gpio_num_t>(PR_PIN_DISPLAY_SCLK);  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
