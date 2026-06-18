#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
Preferences prefs;

const char* AP_NAME = "SMARTDART_SETUP";

const char* FW_URL =
"https://raw.githubusercontent.com/harrycockles/smart-dartboard-system/main/firmware/firmware.bin";

void show(String msg) {
  tft.fillScreen(0x0000);
  tft.setTextSize(2);
  tft.setTextColor(0xFFFF);
  tft.setCursor(10, 100);
  tft.println(msg);
}

// ================= WIFI CONNECT =================
bool connectWiFi() {

  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  show("WiFi...");

  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 30) {
    delay(500);
    t++;
  }

  return WiFi.status() == WL_CONNECTED;
}

// ================= AP =================
void startAP() {

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME);

  show("SETUP MODE\nSMARTDART");

  server.on("/", []() {
    server.send(200, "text/plain",
      "Connect WiFi via future UI (basic mode running)");
  });

  server.begin();
}

// ================= OTA =================
void runOTA() {

  show("OTA CHECK");

  WiFiClientSecure client;
  client.setInsecure();

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret =
    httpUpdate.update(client, FW_URL);

  if (ret == HTTP_UPDATE_OK) {
    show("SUCCESS");
  } else {
    show("NO UPDATE");
  }

  delay(2000);
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  tft.begin();
  tft.setRotation(0);

  show("BOOT");
  delay(1000);

  if (connectWiFi()) {

    show("WIFI OK");
    delay(1000);

    runOTA();

  } else {

    startAP();
  }
}

// ================= LOOP =================
void loop() {
  server.handleClient();
}
