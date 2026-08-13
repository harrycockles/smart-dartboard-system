# Setup Wizard

Flash this **once**, via USB, to a brand-new board — before the main
SmartDartboard app is ever installed on it.

## What it does
1. Opens a WiFi access point (`SmartDartboard-Setup-XXXXXX`) with a
   captive portal. Connect your phone to it, and it'll open (or
   navigate to `192.168.4.1` if it doesn't auto-open) a page that
   scans for nearby networks and lets you pick your home WiFi + enter
   the password.
2. Once connected, walks through a quick on-device LED calibration
   using the encoder: turn to set LED count, click to confirm; turn to
   set brightness, click to confirm.
3. Saves WiFi credentials + LED calibration to the ESP32's NVS storage
   (`Preferences`), under the same key names the main app uses.
4. Downloads the latest main app firmware from GitHub, verifies its
   MD5 hash, flashes it, and reboots.

From that point on, the board runs the main SmartDartboard app, which
finds its WiFi and LED calibration already saved — **NVS storage
survives OTA flashes**, so nothing needs to be re-entered. This
wizard's own code is gone from flash, replaced by the real app; it
won't run again unless someone deliberately re-flashes it (e.g. after
a factory reset wipes NVS).

## Setup
Same library requirements as the main app: `FastLED`, `Adafruit GFX
Library`, `Adafruit GC9A01A`. Also needs the **same OTA-enabled
partition scheme** selected in Tools → Partition Scheme (e.g. "Minimal
SPIFFS (1.9MB APP with OTA)") — this sketch uses the ESP32's `Update`
class to flash the main app onto the second OTA partition, exactly
like the main app's own self-update mechanism does.

## Before shipping boards to customers
This wizard's final step downloads and flashes whatever's currently
published at your GitHub repo's `firmware.bin` / `version.txt` (see
the main app's README for the exact `version.txt` format — it needs
the `version|md5` format). **Publish a real release before running the
wizard on a customer-bound board**, or it'll fail the download step and
prompt to retry (it won't brick anything - see `runInstallStep()`,
which just keeps retrying until it succeeds rather than giving up
partway through).

## Safety net
If this sketch is ever flashed onto a board that's already completed
setup (`setup_done` already true in NVS), it skips straight to
reinstalling the latest app firmware rather than wiping the customer's
saved WiFi and LED calibration.

## Known gaps
- Portal is plain HTML, not styled to match the round display's look —
  functional but not pretty. Low priority since it's only seen once
  per board, briefly, on a phone.
- No password confirmation field / no "hidden network" manual SSID
  entry option.
- If the WiFi step fails repeatedly, there's no fallback (e.g. skip
  WiFi and configure later) - it'll just keep prompting to retry via
  the portal.
