#ifndef WIZARD_CONFIG_H
#define WIZARD_CONFIG_H

// ============================================================
//  SETUP WIZARD - CONFIG
//  A subset of the main app's Config.h - only what the wizard needs.
//  IMPORTANT: if you change pins or the OTA URLs in the main
//  SmartDartboard/Config.h, mirror the change here too. They're kept
//  as separate files (rather than shared) because this sketch is
//  flashed once, standalone, before the main app exists on the board.
// ============================================================

#define WIZARD_VERSION   "1.0.0"

// ---- Display (GC9A01 240x240 round TFT) ----
#define TFT_CS     5
#define TFT_DC     2
#define TFT_RST    4
// MOSI/SCK use the default VSPI pins via the SPI library

// ---- LED ring (WS2812B) ----
#define LED_PIN        26
#define LED_MAX_COUNT  240
#define NUM_LEDS_DEFAULT 120
#define LED_TYPE       WS2812B
#define LED_COLOR_ORDER GRB
#define LED_CAL_BRIGHTNESS_DEFAULT 128

// ---- Rotary encoder (EC11) ----
#define ENCODER_CLK    34
#define ENCODER_DT     35
#define ENCODER_SW     32

// ---- WiFi / captive portal ----
#define AP_SSID_PREFIX "SmartDartboard-Setup-"
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ---- OTA (GitHub) - must match the main app's Config.h ----
// Split into host/path (not one URL string) to sidestep an ESP32
// HTTPClient HTTPS quirk - see the main app's Config.h comment.
#define OTA_HOST          "raw.githubusercontent.com"
#define OTA_PORT          443
#define OTA_VERSION_PATH  "/harrycockles/smart-dartboard-system/main/firmware/version.txt"
#define OTA_FIRMWARE_PATH "/harrycockles/smart-dartboard-system/main/firmware/firmware.bin"

// ---- Storage (Preferences) - MUST match the main app's Config.h key
//      names exactly, since this is how the main app inherits the
//      wizard's setup without any extra glue code. ----
#define PREFS_NAMESPACE       "dartboard"
#define PREFS_KEY_WIFI_SSID    "wifi_ssid"
#define PREFS_KEY_WIFI_PASS    "wifi_pass"
#define PREFS_KEY_LED_COUNT    "led_count"
#define PREFS_KEY_LED_BRIGHT   "led_bright"
#define PREFS_KEY_SETUP_DONE   "setup_done"

#endif // WIZARD_CONFIG_H
