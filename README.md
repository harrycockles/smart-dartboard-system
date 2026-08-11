# Smart Dartboard System

A smart electronic dartboard built on the ESP32 - round TFT display, WS2812B LED ring, rotary encoder and keypad input, full game engine (301/501/701, multiplayer, legs), and OTA firmware updates pulled directly from this repo.

## Hardware

| Part | Notes |
|---|---|
| ESP32 Dev Module | Main controller |
| GC9A01 240x240 round TFT display | Score/menu display |
| WS2812B LED ring | Visual feedback |
| EC11 rotary encoder | Menu navigation |
| 3x4 matrix keypad | Score entry, text entry |

## Software

- [Arduino IDE](https://www.arduino.cc/en/software)
- ESP32 board package (Espressif) - installed via Boards Manager
- Libraries (installed via Library Manager):
  - FastLED
  - Adafruit GFX Library
  - Adafruit GC9A01A
  - Keypad
- [Git](https://git-scm.com/)

## Repo structure

```
firmware/       Main ESP32 firmware source, compiled firmware.bin, version.txt
pi-vision/      Raspberry Pi camera-based auto-scoring (in progress)
```

---

## Setup: from a blank ESP32 to a running board

### 1. Clone the repo
```bash
git clone https://github.com/harrycockles/smart-dartboard-system.git
cd smart-dartboard-system
```

### 2. Install Arduino IDE + ESP32 support
1. Install Arduino IDE.
2. File -> Preferences -> "Additional Boards Manager URLs", add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Tools -> Board -> Boards Manager -> search "esp32" -> install the package by **Espressif Systems**.
4. Tools -> Board -> ESP32 Arduino -> **ESP32 Dev Module**.
5. Tools -> Partition Scheme -> pick one with OTA support (e.g. "Minimal SPIFFS (1.9MB APP with OTA)") - required for firmware updates to work.

### 3. Install the required libraries
Sketch -> Include Library -> Manage Libraries, install: **FastLED**, **Adafruit GFX Library**, **Adafruit GC9A01A**, **Keypad**.

### 4. Wire the hardware
Pin assignments are defined in `firmware/Config.h`. Wire the display, LED ring, encoder, and keypad to the GPIOs specified there before powering on.

### 5. Flash the firmware
1. Open `firmware/SmartDartboard.ino` in Arduino IDE (opening this file auto-loads the other `.ino` files in the sketch as tabs).
2. Connect the ESP32 via USB, select the correct Port under Tools.
3. Click Upload.

### 6. First boot - connect to WiFi
On first boot with no saved WiFi credentials, the board opens its own WiFi access point with a captive portal. Connect to it from a phone, follow the prompt to select your home network and enter the password.

---

## Publishing an OTA update

The board checks this repo for updates automatically (nightly, and on-demand via Settings -> update).

1. Bump `FW_VERSION` in `firmware/Config.h`.
2. In Arduino IDE: Sketch -> Export Compiled Binary.
3. Rename the exported file to `firmware.bin`.
4. Generate its hash:
   ```bash
   md5sum firmware.bin
   ```
5. Update `firmware/version.txt` with the new version and hash, in this exact format:
   ```
   2.7.0|9f86d081884c7d659a2feaa0c55ad015
   ```
6. Commit and push:
   ```bash
   git add firmware/firmware.bin firmware/version.txt
   git commit -m "Release 2.7.0"
   git push origin main
   ```

Every board checks `firmware/version.txt` and only updates if the version is newer and the MD5 hash of the downloaded `firmware.bin` matches what's listed - a mismatch or malformed entry means the update is rejected rather than applied.
