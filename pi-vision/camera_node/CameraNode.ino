// ============================================================
//  CAMERANODE.INO - runs on each of the 3 ESP32-CAM boards.
//  Connects to WiFi, waits for a trigger, captures a photo, and POSTs
//  it to the Pi's server.py at /upload/<CAMERA_ID>. Also sends a
//  lightweight heartbeat every few seconds so the Pi's status
//  dashboard (http://<pi-ip>:5000) can show whether each camera is
//  actually online, not just when it last threw a dart.
//
//  Pinout below is for the common AI-Thinker ESP32-CAM board. If
//  you've got a different ESP32-CAM variant, the camera pin numbers
//  will likely need to change - check your specific board's pinout.
//
//  IMPORTANT: set CAMERA_ID below to a different value (1, 2, or 3)
//  on each of the 3 boards before flashing - this is how server.py
//  tells them apart.
// ============================================================
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ---- Per-board config - CHANGE THIS on each of the 3 boards ----
#define CAMERA_ID "1"   // "1", "2", or "3" - must match one board each

// ---- WiFi / server config - same on all 3 boards ----
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SERVER_HOST   "192.168.1.XXX"  // the Pi's IP address on your network
#define SERVER_PORT   5000

// ---- Trigger input ----
// Simplest approach: wire a GPIO on this board to the same signal
// that triggers all 3 cameras at once (e.g. driven by a piezo sensor
// on the main SmartDartboard ESP32, or a simple shared button for
// testing). GPIO13 is a safe, free pin on most AI-Thinker boards -
// double check it's not in use for your specific camera pinout.
#define TRIGGER_PIN 13

// ---- AI-Thinker ESP32-CAM pin map ----
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Modest resolution/quality on purpose: the Pi only needs to find
  // "where did this image change", not a detailed photo. Smaller
  // frames also mean faster WiFi upload and faster diffing on a weak
  // Pi B+. Bump these up later if diffing accuracy needs it.
  config.frame_size = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 12;          // 0-63, lower = higher quality/larger file
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) delay(1000); // halt - nothing useful to do without a camera
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());
}

void captureAndSend() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  HTTPClient http;
  String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) +
               "/upload/" + CAMERA_ID;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(fb->buf, fb->len);
  Serial.printf("POST %s -> HTTP %d\n", url.c_str(), httpCode);

  http.end();
  esp_camera_fb_return(fb);
}

// Periodic lightweight ping so the Pi's dashboard status dot reflects
// "is this camera actually connected right now" rather than only
// updating whenever a throw happens (which could be minutes apart).
#define HEARTBEAT_INTERVAL_MS 5000
unsigned long lastHeartbeatMs = 0;

void sendHeartbeat() {
  HTTPClient http;
  String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) +
               "/heartbeat/" + CAMERA_ID;
  http.begin(url);
  http.POST(""); // body doesn't matter, the endpoint just needs the request to arrive
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  initCamera();
  connectWiFi();

  // Send an initial frame immediately on boot - this becomes the
  // server's baseline for this camera before any darts are thrown.
  delay(500); // let auto-exposure settle before the first shot
  captureAndSend();

  Serial.println("Ready - waiting for trigger.");
}

void loop() {
  if (millis() - lastHeartbeatMs > HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatMs = millis();
  }

  // TODO: replace this simple polling trigger with whatever your real
  // trigger mechanism ends up being (e.g. a piezo sensor's digital
  // output, or a message from the main SmartDartboard ESP32 over
  // WiFi). This version just watches for TRIGGER_PIN going LOW.
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println("Triggered - capturing...");
    delay(200); // brief settle time after impact before capturing
    captureAndSend();

    // Simple debounce: wait for the trigger to release before
    // watching for the next one, so one throw doesn't fire twice.
    while (digitalRead(TRIGGER_PIN) == LOW) delay(10);
  }
  delay(20);
}
