=// ============================================================
//  SMART DARTBOARD - SETUP WIZARD
//  Flash this ONCE, via USB, to a brand new board. It:
//   1. Opens a WiFi access point + captive portal so the customer can
//      scan for and connect to their home WiFi from their phone.
//   2. Walks through a quick on-device LED count + brightness
//      calibration using the encoder.
//   3. Saves all of that to the ESP32's NVS storage (Preferences),
//      under the exact same keys the main SmartDartboard app reads.
//   4. Downloads and MD5-verifies the latest main app firmware from
//      GitHub, flashes it, and reboots.
//
//  From that point on, the board is running the main app - which
//  finds its WiFi credentials and LED calibration already saved (NVS
//  survives OTA flashes), and this wizard's code is gone from flash,
//  replaced by the real app. It never runs again unless someone
//  deliberately re-flashes it (e.g. after a factory reset).
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <SPI.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#include "WizardConfig.h"

// ---- Globals ----
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
CRGB leds[LED_MAX_COUNT];
Preferences prefs;
WebServer webServer(80);
DNSServer dnsServer;
static const byte DNS_PORT = 53;

static String scannedNetworks;   // cached <option> HTML from the last scan
static volatile bool wifiConnectedFlag = false;
static volatile bool wifiConnectFailedFlag = false;

static String pendingSsid, pendingPass;
static volatile bool connectRequested = false;
static unsigned long connectRequestedAtMs = 0;

int calLedCount = NUM_LEDS_DEFAULT;
int calBrightness = LED_CAL_BRIGHTNESS_DEFAULT;

// ---- Encoder (same simple interrupt-based decode as the main app) ----
static volatile int8_t encDelta = 0;
static volatile uint8_t lastEncState = 0;
static bool lastSwState = HIGH;
static unsigned long lastDebounceMs = 0;

void IRAM_ATTR encoderIsr() {
  uint8_t clk = digitalRead(ENCODER_CLK);
  uint8_t dt  = digitalRead(ENCODER_DT);
  uint8_t state = (clk << 1) | dt;
  if (state != lastEncState) {
    if (lastEncState == 0b00 && state == 0b01) encDelta++;
    if (lastEncState == 0b00 && state == 0b10) encDelta--;
    lastEncState = state;
  }
}

int encoderGetDelta() {
  noInterrupts();
  int d = encDelta;
  encDelta = 0;
  interrupts();
  return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

bool encoderWasClicked() {
  bool sw = digitalRead(ENCODER_SW);
  bool clicked = false;
  if (sw != lastSwState && (millis() - lastDebounceMs) > 30) {
    lastDebounceMs = millis();
    if (sw == LOW) clicked = true;
    lastSwState = sw;
  }
  return clicked;
}

// ---- Simple screen helper ----
void showScreen(const String &title, const String &line1 = "", const String &line2 = "", const String &line3 = "") {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextColor(GC9A01A_GREEN);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.print(title);

  tft.setTextColor(GC9A01A_WHITE);
  tft.setTextSize(1);
  int y = 60;
  if (line1.length()) { tft.setCursor(20, y); tft.print(line1); y += 20; }
  if (line2.length()) { tft.setCursor(20, y); tft.print(line2); y += 20; }
  if (line3.length()) { tft.setCursor(20, y); tft.print(line3); y += 20; }
}

void showProgress(int percent, const String &statusText) {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextColor(GC9A01A_WHITE);
  tft.setTextSize(1);
  tft.setCursor(30, 90);
  tft.print(statusText);

  int barX = 40, barY = 130, barW = 160, barH = 14;
  tft.drawRect(barX, barY, barW, barH, GC9A01A_WHITE);
  int fillW = (barW - 4) * percent / 100;
  tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, GC9A01A_GREEN);

  tft.setCursor(110, 150);
  tft.print(percent);
  tft.print("%");
}

// ============================================================
//  STEP 1: WiFi captive portal with network scan
// ============================================================
void scanAndCacheNetworks() {
  showScreen("Setup Wizard", "Scanning for WiFi...");
  int n = WiFi.scanNetworks();
  Serial.println("[wizard] scan found " + String(n) + " network(s)");
  scannedNetworks = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "&quot;"); // basic HTML-safety for the option value
    scannedNetworks += "<option value='" + ssid + "'>" + ssid +
                        " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  if (n == 0) {
    scannedNetworks = "<option value=''>No networks found - tap Rescan</option>";
  }
}

void startWifiPortal() {
  String apName = String(AP_SSID_PREFIX) + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);

  WiFi.mode(WIFI_AP_STA); // AP for the portal, STA ready to test-connect
  WiFi.softAP(apName.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("[wizard] AP started: " + apName + " at " + WiFi.softAPIP().toString());

  scanAndCacheNetworks();

  webServer.on("/", HTTP_GET, []() {
    String html =
      "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>Dartboard Setup</title></head><body style='font-family:sans-serif'>"
      "<h2>Smart Dartboard Setup</h2>"
      "<p>Choose your home WiFi network:</p>"
      "<form action='/connect' method='POST'>"
      "<select name='ssid' style='font-size:16px;padding:8px;width:100%'>" +
      scannedNetworks +
      "</select><br><br>"
      "Password:<br>"
      "<input name='pass' type='password' style='font-size:16px;padding:8px;width:100%'><br><br>"
      "<input type='submit' value='Connect' style='font-size:16px;padding:10px 20px'>"
      "</form>"
      "<br><a href='/rescan'>Rescan networks</a>"
      "</body></html>";
    webServer.send(200, "text/html", html);
  });

  webServer.on("/rescan", HTTP_GET, []() {
    scanAndCacheNetworks();
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.on("/connect", HTTP_POST, []() {
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");
    Serial.println("[wizard] /connect received, ssid='" + ssid + "'");

    if (ssid.length() == 0) {
      webServer.send(200, "text/html", "<body>No network selected. <a href='/'>Go back</a></body>");
      return;
    }

    webServer.send(200, "text/html",
      "<body><h3>Connecting to " + ssid + "...</h3>"
      "<p>You can close this page. Check the dartboard's screen for status.</p></body>");

    // Don't call WiFi.begin() here directly: switching from AP to
    // AP+STA-connecting can change the AP's radio channel, which can
    // drop the very TCP connection carrying the response above before
    // it actually reaches the phone. Defer the real connect attempt to
    // the main loop, after a short pause to let the response go out.
    pendingSsid = ssid;
    pendingPass = pass;
    connectRequested = true;
    connectRequestedAtMs = millis();
  });

  webServer.onNotFound([]() {
    // Catches captive-portal detection probes (e.g. Android's
    // generate_204, iOS's hotspot-detect.html) and anything else
    // unmatched, redirecting them back to the setup page instead of a
    // bare 404 - keeps captive-portal auto-popup behavior working.
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.begin();
  showScreen("Setup Wizard",
             "Connect your phone to WiFi:", apName,
             "then open 192.168.4.1");
}

void runWifiStep() {
  startWifiPortal();

  while (!wifiConnectedFlag) {
    dnsServer.processNextRequest();
    webServer.handleClient();

    if (connectRequested && millis() - connectRequestedAtMs > 300) {
      connectRequested = false;
      Serial.println("[wizard] attempting WiFi.begin() to '" + pendingSsid + "'");
      WiFi.begin(pendingSsid.c_str(), pendingPass.c_str());

      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        webServer.handleClient(); // keep the portal responsive during the wait
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[wizard] WiFi connected: " + WiFi.localIP().toString());
        prefs.putString(PREFS_KEY_WIFI_SSID, pendingSsid);
        prefs.putString(PREFS_KEY_WIFI_PASS, pendingPass);
        wifiConnectedFlag = true;
      } else {
        Serial.println("[wizard] WiFi connect failed/timed out, status=" + String(WiFi.status()));
        wifiConnectFailedFlag = true;
      }
    }

    if (wifiConnectFailedFlag) {
      showScreen("Setup Wizard", "Connect failed.", "Try again from your phone",
                 "at 192.168.4.1");
      wifiConnectFailedFlag = false;
    }
    delay(2);
  }

  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA); // drop the AP now that we're on the real network

  showScreen("Setup Wizard", "WiFi connected!", WiFi.localIP().toString());
  delay(1500);
}

// ============================================================
//  STEP 2: LED calibration (count, then brightness)
// ============================================================
void runLedCalibrationStep() {
  FastLED.addLeds<LED_TYPE, LED_PIN, LED_COLOR_ORDER>(leds, LED_MAX_COUNT);
  FastLED.setBrightness(calBrightness);

  // --- LED count ---
  bool done = false;
  showScreen("LED Setup", "Turn: set LED count", "Click: confirm");
  delay(800);
  while (!done) {
    int d = encoderGetDelta();
    if (d != 0) {
      calLedCount += d;
      if (calLedCount < 1) calLedCount = 1;
      if (calLedCount > LED_MAX_COUNT) calLedCount = LED_MAX_COUNT;
      FastLED.clear();
      for (int i = 0; i < calLedCount; i++) leds[i] = CRGB::White;
      FastLED.show();
      showScreen("LED Count", String(calLedCount) + " LEDs", "Click to confirm");
    }
    if (encoderWasClicked()) done = true;
    delay(2);
  }
  prefs.putInt(PREFS_KEY_LED_COUNT, calLedCount);

  // --- Brightness ---
  done = false;
  fill_solid(leds, calLedCount, CRGB::White);
  FastLED.show();
  showScreen("LED Brightness", String(calBrightness), "Click to confirm");
  while (!done) {
    int d = encoderGetDelta();
    if (d != 0) {
      calBrightness += d * 4;
      if (calBrightness < 4) calBrightness = 4;
      if (calBrightness > 255) calBrightness = 255;
      FastLED.setBrightness(calBrightness);
      FastLED.show();
      showScreen("LED Brightness", String(calBrightness), "Click to confirm");
    }
    if (encoderWasClicked()) done = true;
    delay(2);
  }
  prefs.putInt(PREFS_KEY_LED_BRIGHT, calBrightness);

  FastLED.clear();
  FastLED.show();
  showScreen("LED Setup", "Saved!");
  delay(1000);
}

// ============================================================
//  STEP 3: fetch + MD5-verify + flash the main app, then reboot into it
//  (Same integrity approach as the main app's own OTA.ino - see that
//  file's comments for why this is done manually rather than via a
//  one-call helper.)
// ============================================================
static bool isValidMd5(const String &s) {
  if (s.length() != 32) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isHexadecimalDigit(s[i])) return false;
  }
  return true;
}

bool fetchLatestVersionAndMd5(String &version, String &md5) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  // Cache-busting: see the main app's OTA.ino comment - GitHub's raw
  // CDN can serve a stale cached 404 for a while after content becomes
  // available; varying the URL per request avoids hitting that cache.
  String versionPath = String(OTA_VERSION_PATH) + "?t=" + String(millis());
  http.begin(client, OTA_HOST, OTA_PORT, versionPath.c_str(), true);
  http.addHeader("User-Agent", "SmartDartboard-ESP32"); // some CDNs 404 requests with no UA
  int code = http.GET();
  Serial.println("[wizard] GET version.txt -> HTTP " + String(code));
  if (code != HTTP_CODE_OK) {
    Serial.println("[wizard] response body: " + http.getString());
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  body.trim();
  Serial.println("[wizard] version.txt body: " + body);

  int sep = body.indexOf('|');
  if (sep < 0) {
    Serial.println("[wizard] version.txt missing '|<md5>'");
    return false;
  }

  version = body.substring(0, sep);
  version.trim();
  md5 = body.substring(sep + 1);
  md5.trim();
  md5.toLowerCase();

  if (!isValidMd5(md5)) {
    Serial.println("[wizard] md5 field invalid: '" + md5 + "'");
    return false;
  }
  return true;
}

bool installMainApp(const String &expectedMd5) {
  showProgress(0, "Downloading app firmware...");

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  String firmwarePath = String(OTA_FIRMWARE_PATH) + "?t=" + String(millis());
  http.begin(client, OTA_HOST, OTA_PORT, firmwarePath.c_str(), true);
  http.addHeader("User-Agent", "SmartDartboard-ESP32");
  int code = http.GET();
  Serial.println("[wizard] GET firmware.bin -> HTTP " + String(code));
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  Serial.println("[wizard] firmware.bin size: " + String(contentLength));
  if (contentLength <= 0) { http.end(); return false; }

  if (!Update.begin(contentLength)) {
    Serial.println("[wizard] Update.begin() failed: " + String(Update.errorString()));
    http.end();
    return false;
  }
  Update.setMD5(expectedMd5.c_str());

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buff[1024];
  int written = 0;

  while (http.connected() && written < contentLength) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = avail > sizeof(buff) ? sizeof(buff) : avail;
      int readBytes = stream->readBytes(buff, toRead);
      size_t w = Update.write(buff, readBytes);
      if (w != (size_t)readBytes) break;
      written += readBytes;
      showProgress((written * 100) / contentLength, "Verifying & flashing...");
    } else {
      delay(1);
    }
  }
  http.end();
  Serial.println("[wizard] wrote " + String(written) + " / " + String(contentLength) + " bytes");

  if (written != contentLength) {
    Update.abort();
    return false;
  }

  bool ok = Update.end(true); // finalizes + checks the MD5 we set above
  if (!ok) {
    Serial.println("[wizard] Update.end() failed: " + String(Update.errorString()));
  }
  return ok;
}

void runInstallStep() {
  bool ok = false;

  while (!ok) {
    showProgress(0, "Checking for app firmware...");
    String version, md5;
    if (fetchLatestVersionAndMd5(version, md5)) {
      ok = installMainApp(md5);
    }

    if (!ok) {
      showScreen("Setup Wizard", "Couldn't install app firmware.",
                 "Check your GitHub release,", "then click to retry.");
      while (!encoderWasClicked()) delay(10);
    }
  }

  prefs.putBool(PREFS_KEY_SETUP_DONE, true);
  showProgress(100, "Done! Rebooting into app...");
  delay(1000);
  ESP.restart();
}

// ============================================================
//  SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);

  prefs.begin(PREFS_NAMESPACE, false);

  tft.begin();
  tft.setRotation(0);
  showScreen("Setup Wizard", "Starting...");

  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderIsr, CHANGE);

  // Safety net: if this ever gets flashed onto a board that's already
  // been set up (e.g. by accident), don't wipe its WiFi/calibration -
  // just skip straight to reinstalling the latest app firmware.
  if (prefs.getBool(PREFS_KEY_SETUP_DONE, false)) {
    showScreen("Setup Wizard", "Already set up.", "Reinstalling app firmware...");
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin(prefs.getString(PREFS_KEY_WIFI_SSID, "").c_str(),
               prefs.getString(PREFS_KEY_WIFI_PASS, "").c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) delay(250);
    runInstallStep();
    return;
  }

  runWifiStep();
  runLedCalibrationStep();
  runInstallStep();
}

void loop() {
  // Everything happens in setup(); once installMainApp() succeeds the
  // board reboots into the main app and this code never runs again.
}
