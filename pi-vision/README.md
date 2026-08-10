# Smart Dartboard - Camera Vision (Pi side)

Everything here is written and ready to test, but **honest caveat up
front**: none of it has run against real hardware yet, since the Pi
and ESP32-CAMs haven't arrived. The server/diffing/calibration logic
itself is straightforward Python and should just work; the parts most
likely to need tuning once you have real cameras are the diff
threshold, the trigger mechanism, and camera mounting/lighting.

## How the pieces fit together

```
[3x ESP32-CAM] --WiFi/HTTP--> [Pi: server.py] --lookup--> [board position]
     |                              |
  camera_node/                  diff_utils.py (find what changed)
  CameraNode.ino                triangulate.py (position lookup)
                                 calibrate.py (builds the lookup table)
```

1. Each ESP32-CAM (`camera_node/CameraNode.ino`) watches a trigger pin,
   captures a photo when triggered, POSTs it to the Pi.
2. `server.py` receives images at `/upload/<camera_id>`, diffs each
   against that camera's last-known baseline (`diff_utils.py`) to find
   where something changed in that camera's view.
3. Once all 3 cameras have reported, `triangulate.py` looks up the
   closest match in a calibration table to estimate the board position.
4. `send_to_dartboard()` in `server.py` sends the result over the
   **ESP32's USB port** (see `SerialLink.ino` in the firmware) - just a
   USB cable from the Pi straight into the ESP32, no GPIO wiring needed.

## Connecting the Pi to the ESP32
Plug a USB cable from one of the Pi's USB-A ports into the ESP32's
USB port - the same port used for flashing firmware. That's it, no
GPIO pins involved.

**One trade-off worth knowing**: while the Pi's cable is plugged in,
you can't also have your computer plugged into the ESP32 to reprogram
it - only one USB host at a time. Unplug the Pi's cable and use your
computer's whenever you need to re-flash firmware, then plug the Pi
back in afterward.

Find which serial device the ESP32 shows up as on the Pi:
```bash
ls /dev/tty*
# unplug the ESP32, run it again, see which entry disappeared
```
Common results: `/dev/ttyUSB0` (CP2102/CH340-based boards, most ESP32
devkits) or `/dev/ttyACM0` (some native-USB boards). `server.py`
defaults to `/dev/ttyUSB0` - change `SERIAL_PORT` near the top if
yours shows up differently.

**Protocol**: the Pi sends each completed turn's total as plain ASCII
digits + newline (e.g. `"23\n"`). This is fed through the exact same
code path keypad turn-total entry uses on the ESP32, so checkouts
still go through the normal keypad "finish on a double?" confirmation
- the physical keypad stays involved for that one interactive step,
same as manual entry.

## Status dashboard - navigate to the Pi's IP in a browser
```
http://<pi-ip>:5000
```
Shows a card per camera: a green/red dot (green = checked in within
the last 15 seconds, via heartbeat or an actual throw image; red =
hasn't), and that camera's most recently received frame. Auto-refreshes
every 5 seconds. This isn't true live video - cameras only send frames
after a trigger or a lightweight heartbeat ping, not a continuous
stream - so the image updates whenever a new one arrives, not in
real time between throws.

**Note the `:5000`** - Flask's dev server needs a non-standard port
without extra setup (binding port 80 needs root/special permissions),
so "the IP" on its own won't work, you need the port too.

## Setting up real OTA updates, using your existing repo
`update.sh` already does the actual update logic (git pull + restart),
but it needs a real git repo on the Pi to pull from - if you got the
code onto the Pi via `scp` earlier rather than `git clone`, there's no
`.git` folder yet for it to work with. Since you're consolidating this
into your existing `smart-dartboard-system` repo (the same one the
ESP32 firmware's `firmware/` folder lives in), here's the walkthrough:

**1. Move this project into your existing repo, as a `pi-vision` folder**
(from your Chromebook, wherever your existing repo is checked out):
```bash
cd ~/smart-dartboard-system      # your existing repo checkout
cp -r ~/PiDartVision ./pi-vision
git add pi-vision
git commit -m "Add Pi vision system"
git push origin main
```
Your repo now has `firmware/` (the ESP32 side) and `pi-vision/` (this
project) side by side.

**2. On the Pi: remove the scp'd copy, clone the whole repo instead**
```bash
ssh pi@dartpi.local
rm -rf ~/PiDartVision
git clone https://github.com/harrycockles/smart-dartboard-system.git ~/smart-dartboard-system
cd ~/smart-dartboard-system/pi-vision
pip3 install -r requirements.txt
```
You're cloning the *whole* repo (including `firmware/`), but the Pi
only ever runs what's inside `pi-vision/` - the rest just comes along
for the ride, harmlessly.

**3. Set up the systemd service** (paths already point at this
`pi-vision` subfolder structure):
```bash
cd ~/smart-dartboard-system/pi-vision
sudo cp dartboard-vision.service /etc/systemd/system/
sudo systemctl enable dartboard-vision
sudo systemctl start dartboard-vision
sudo systemctl status dartboard-vision   # confirm it's actually running
```

**4. Add the cron job**, same idea as the firmware's midnight OTA check:
```bash
crontab -e
# add this line:
0 2 * * * /home/pi/smart-dartboard-system/pi-vision/update.sh >> /home/pi/update.log 2>&1
```

From then on: push a change to `pi-vision/` on GitHub from your
Pi picks it up automatically overnight (or run `./update.sh` manually
on the Pi any time to check immediately).

## Setup on the Pi

```bash
sudo apt update
sudo apt install python3-pip python3-numpy python3-pil git
git clone <your repo> PiDartVision
cd PiDartVision
pip3 install -r requirements.txt
python3 server.py
```

Leave that running and check `http://<pi-ip>:5000/status` from another
machine to confirm it's up.

### Running it as a service (recommended once it's working)
```bash
sudo cp dartboard-vision.service /etc/systemd/system/
sudo systemctl enable dartboard-vision
sudo systemctl start dartboard-vision
```
Check it's running: `sudo systemctl status dartboard-vision`

### Auto-updates (optional, mirrors the ESP32's OTA setup)
`update.sh` does a `git pull` + service restart. To check periodically
like the firmware's midnight OTA check, add a cron job:
```bash
crontab -e
# add this line to check every night at 2am:
0 2 * * * /home/pi/PiDartVision/update.sh >> /home/pi/update.log 2>&1
```

## Setup on each ESP32-CAM
1. Open `camera_node/CameraNode.ino` in Arduino IDE.
2. Board: whatever matches your specific ESP32-CAM (AI-Thinker is the
   common one - Tools -> Board -> ESP32 Wrover Module usually works
   for it, or the specific "AI Thinker ESP32-CAM" entry if your board
   list has one).
3. Edit the top of the file: set `CAMERA_ID` (1/2/3, different on each
   board), `WIFI_SSID`/`WIFI_PASSWORD`, and `SERVER_HOST` to the Pi's
   actual IP address.
4. Flash each board - you'll need an FTDI/USB-serial adapter for these
   (most ESP32-CAM boards don't have USB built in), and typically need
   to bridge GPIO0 to GND during upload, then remove it and reset to run.

## Calibration - required before anything gets detected
The system starts with zero calibration data, so `triangulate.py` will
return "no match" for every throw until you build up the table:
```bash
python3 calibrate.py
```
Walks you through: reset baselines, throw a dart at a known spot, tell
it what/where you hit, it records what each camera saw. Repeat for as
many positions as you're willing to throw at - more coverage across
the whole board (not just clustered near center) gives much better
accuracy. See the comment at the top of `calibrate.py` for the full
walkthrough.

## What's genuinely untested / likely needs tuning once hardware arrives
- **ESP32 resetting when the Pi's serial connection opens** - some
  USB-serial chips reset the board when DTR/RTS toggle on connect
  (the same well-known quirk that affects Arduino boards too).
  `server.py` explicitly holds both low to avoid this, but if you
  still see the ESP32 reboot every time `server.py` starts, that's
  the first thing to investigate.
- **`DIFF_THRESHOLD` and `MIN_CHANGED_PIXELS`** in `diff_utils.py` -
  these depend entirely on your actual cameras' noise level and how
  large a dart appears in-frame. Start here if detection seems
  too twitchy (false positives) or too insensitive (misses real darts).
- **The trigger mechanism** - `CameraNode.ino` currently just polls a
  GPIO pin going LOW. You'll want this to be the same physical signal
  all 3 cameras see simultaneously (e.g. a piezo sensor's output wired
  to all 3 boards, or a relay/signal from the main ESP32), so all 3
  cameras capture the same moment.
- **Lighting consistency** - the LED ring should probably hold steady
  (plain white, which is the current default per the main firmware) at
  least during the capture window, since color/brightness changes will
  register as "changes" the diffing can't distinguish from a dart.
- **Camera mounting rigidity** - if a camera shifts position after
  calibration, its calibration data becomes wrong. Worth mounting
  securely before doing a full calibration pass.
- **The 3-darts-per-turn accumulator** doesn't yet handle early
  checkouts (finishing on dart 1 or 2 of a turn) - see the detailed
  comment in `process_throw()` in `server.py` for the two practical
  fixes and why this is genuinely tricky without a "turn is over"
  signal from somewhere.

## Known simplifications, on purpose
- Calibration uses nearest-neighbor lookup, not real camera geometry/
  triangulation math - see the comment at the top of `triangulate.py`
  for why this is the more practical choice for a hobbyist setup
  without precise camera calibration equipment.
- The Flask dev server (not a production WSGI server) is used
  deliberately - this is low-traffic, turn-based (a few requests per
  throw), so it doesn't need production-grade serving, and keeping it
  simple matters more on a weak single-core Pi B+.
