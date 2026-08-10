#!/usr/bin/env python3
"""
server.py - runs on the Pi. Receives a JPEG from each of 3 ESP32-CAMs
after every throw, diffs each against that camera's stored baseline to
find where the dart appears in that camera's 2D view, then looks up
the board position from the calibration table (see calibrate.py).

Endpoints:
  GET  /                       - status dashboard (camera feeds + online/offline dots)
  POST /upload/<camera_id>     - ESP32-CAM posts a JPEG here after a throw
                                  (camera_id = 1, 2, or 3)
  POST /heartbeat/<camera_id>  - lightweight liveness ping, sent every few
                                  seconds regardless of throws
  POST /baseline/<camera_id>   - (re)capture this camera's baseline from
                                  whatever image it sends next
  GET  /image/<camera_id>      - that camera's most recent frame as a JPEG
  GET  /status                 - JSON: current baseline/latest state per camera
  GET  /last_centroids         - JSON: most recent detected centroids (calibrate.py)

Once all 3 cameras have posted a new frame since the last processed
throw, this automatically runs the diff + lookup and reports the
result to the main SmartDartboard ESP32 over serial (see
send_to_dartboard() below and SerialLink.ino in the firmware).
"""

import io
import json
import logging
import threading
import time
from pathlib import Path

import serial
from flask import Flask, request, jsonify, Response, send_file
from PIL import Image

from diff_utils import find_change_centroid
from triangulate import triangulate
from scoring import segment_to_score, is_double_segment

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("dartvision")

app = Flask(__name__)

CAMERA_IDS = ["1", "2", "3"]
DATA_DIR = Path(__file__).parent / "data"
DATA_DIR.mkdir(exist_ok=True)

# Direct wired link to the ESP32 over USB - just a USB cable from the
# Pi straight into the ESP32's USB port, reusing the same USB-serial
# connection used for programming (see SerialLink.ino in the firmware
# for the matching side). No GPIO wiring needed.
#
# Port name depends on the ESP32 board's USB-serial chip:
#   /dev/ttyUSB0 - common for CP2102/CH340-based boards (most ESP32 devkits)
#   /dev/ttyACM0 - some boards with native USB enumerate this way instead
# If this doesn't connect, run `ls /dev/tty*` on the Pi right after
# plugging the ESP32 in to see which one actually appeared.
SERIAL_PORT = "/dev/ttyUSB0"
SERIAL_BAUD = 115200
_serial_conn = None

# In-memory state per camera. Baseline = "board with no new dart yet".
# Latest = most recent frame posted since the last processed throw.
state_lock = threading.Lock()
cameras = {cam_id: {"baseline": None, "latest": None} for cam_id in CAMERA_IDS}
last_centroids = {cam_id: None for cam_id in CAMERA_IDS}  # for calibrate.py to read
last_seen = {cam_id: None for cam_id in CAMERA_IDS}  # unix timestamp, for the dashboard's status dots

# A camera counts as "online" (green dot) if it's checked in within
# this many seconds - either a heartbeat or an actual image upload.
# CameraNode.ino sends a heartbeat every 5s, so 15s gives some slack
# for a missed beat or two before flipping red.
ONLINE_THRESHOLD_SECONDS = 15

# Darts accumulate here until a full turn (3 darts) is ready to report -
# see the KNOWN LIMITATION note in process_throw() below about checkouts
# that happen on dart 1 or 2, before a 3rd dart is ever thrown.
current_turn_darts = []  # list of (score, segment) tuples


def load_image(raw_bytes: bytes) -> Image.Image:
    return Image.open(io.BytesIO(raw_bytes)).convert("L")  # grayscale - diffing doesn't need color


def _get_serial():
    global _serial_conn
    if _serial_conn is None:
        _serial_conn = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
        # Some USB-serial chips (common on ESP32/Arduino boards) reset the
        # board when DTR/RTS toggle on connect - pyserial can assert these
        # by default depending on the platform. Explicitly holding them
        # low avoids an unexpected ESP32 reboot every time this connects.
        try:
            _serial_conn.dtr = False
            _serial_conn.rts = False
        except (OSError, IOError):
            pass  # not all platforms/chips support this - harmless if so
        log.info(f"Opened serial link on {SERIAL_PORT} @ {SERIAL_BAUD} baud")
    return _serial_conn


def send_to_dartboard(turn_total: int, last_dart_was_double: bool = False):
    """
    Sends the completed turn total to the ESP32 over the direct wired
    serial link (see SerialLink.ino). last_dart_was_double is accepted
    for compatibility but not currently sent - over serial, the ESP32
    resolves checkouts via its own keypad "finish on a double?" prompt,
    the same interactive step manual keypad entry uses. Errors are
    caught and logged rather than crashing the vision service.
    """
    try:
        ser = _get_serial()
        ser.write(f"{turn_total}\n".encode())
        log.info(f"Sent to dartboard via serial: {turn_total}")
    except serial.SerialException as e:
        log.error(f"Serial write to {SERIAL_PORT} failed: {e}")


@app.route("/upload/<camera_id>", methods=["POST"])
def upload(camera_id):
    if camera_id not in CAMERA_IDS:
        return jsonify({"error": f"unknown camera_id '{camera_id}'"}), 400

    raw = request.get_data()
    if not raw:
        return jsonify({"error": "empty body"}), 400

    try:
        img = load_image(raw)
    except Exception as e:
        log.warning(f"camera {camera_id}: couldn't decode image - {e}")
        return jsonify({"error": "invalid image"}), 400

    # Save a copy to disk too - handy for debugging/re-tuning the diff
    # threshold later without needing to re-capture from hardware.
    img.save(DATA_DIR / f"cam{camera_id}_latest.jpg")

    ready_to_process = False
    with state_lock:
        last_seen[camera_id] = time.time()
        cam = cameras[camera_id]
        if cam["baseline"] is None:
            # First image ever from this camera - treat it as the baseline
            # rather than a throw (nothing to diff against yet).
            cam["baseline"] = img
            log.info(f"camera {camera_id}: baseline captured (first image)")
        else:
            cam["latest"] = img
            log.info(f"camera {camera_id}: new frame received")

        ready_to_process = all(cameras[c]["latest"] is not None for c in CAMERA_IDS)

    if ready_to_process:
        process_throw()

    return jsonify({"ok": True})


@app.route("/heartbeat/<camera_id>", methods=["POST"])
def heartbeat(camera_id):
    """Lightweight liveness ping - CameraNode.ino calls this every few
    seconds regardless of whether a throw has happened, so the
    dashboard's status dots reflect "is this camera actually connected
    right now" rather than just "did it ever send an image"."""
    if camera_id not in CAMERA_IDS:
        return jsonify({"error": f"unknown camera_id '{camera_id}'"}), 400
    with state_lock:
        last_seen[camera_id] = time.time()
    return jsonify({"ok": True})


@app.route("/baseline/<camera_id>", methods=["POST"])
def recapture_baseline(camera_id):
    """Force the NEXT image this camera sends to /upload to become its
    new baseline, instead of being treated as a throw. Useful after
    moving the board, changing lighting, or removing darts."""
    if camera_id not in CAMERA_IDS:
        return jsonify({"error": f"unknown camera_id '{camera_id}'"}), 400
    with state_lock:
        cameras[camera_id]["baseline"] = None
        cameras[camera_id]["latest"] = None
    log.info(f"camera {camera_id}: baseline reset, next upload becomes the new baseline")
    return jsonify({"ok": True})


@app.route("/status", methods=["GET"])
def status():
    with state_lock:
        return jsonify({
            cam_id: {
                "has_baseline": cameras[cam_id]["baseline"] is not None,
                "has_latest": cameras[cam_id]["latest"] is not None,
            }
            for cam_id in CAMERA_IDS
        })


@app.route("/last_centroids", methods=["GET"])
def get_last_centroids():
    """Used by calibrate.py to automatically pull the most recent
    per-camera change centroids, instead of you having to copy them
    from the terminal log by hand."""
    with state_lock:
        return jsonify(last_centroids)


@app.route("/image/<camera_id>", methods=["GET"])
def get_camera_image(camera_id):
    """Serves that camera's most recently received frame as a JPEG.
    Used by the dashboard's <img> tags - not real live video (cameras
    only send a frame after a trigger, not a continuous stream), just
    whatever the last received frame was."""
    if camera_id not in CAMERA_IDS:
        return jsonify({"error": f"unknown camera_id '{camera_id}'"}), 400

    path = DATA_DIR / f"cam{camera_id}_latest.jpg"
    if not path.exists():
        return jsonify({"error": "no image received yet"}), 404

    return send_file(path, mimetype="image/jpeg")


@app.route("/", methods=["GET"])
def dashboard():
    """The status dashboard - what you see navigating to the Pi's IP
    directly in a browser. Auto-refreshes every few seconds so the
    status dots and images update without needing to manually reload."""
    with state_lock:
        now = time.time()
        cams_info = []
        for cam_id in CAMERA_IDS:
            seen = last_seen[cam_id]
            online = seen is not None and (now - seen) < ONLINE_THRESHOLD_SECONDS
            seconds_ago = f"{now - seen:.0f}s ago" if seen is not None else "never"
            cams_info.append({"id": cam_id, "online": online, "last_seen": seconds_ago})

    rows = ""
    for cam in cams_info:
        dot_color = "#2ecc71" if cam["online"] else "#e74c3c"
        status_text = "online" if cam["online"] else "offline"
        rows += f"""
        <div class="cam-box">
          <div class="cam-header">
            <span class="dot" style="background:{dot_color}"></span>
            <span class="cam-label">Cam {cam['id']}</span>
            <span class="cam-status">{status_text} &middot; last seen {cam['last_seen']}</span>
          </div>
          <img src="/image/{cam['id']}?t={int(time.time())}"
               onerror="this.style.display='none'"
               class="cam-image" alt="Camera {cam['id']}">
        </div>
        """

    html = f"""
    <html>
    <head>
        <title>Smart Dartboard Vision</title>
        <meta http-equiv="refresh" content="5">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            body {{ font-family: sans-serif; background: #1a1a1a; color: #eee;
                    margin: 0; padding: 20px; }}
            h1 {{ font-size: 1.3em; }}
            .grid {{ display: flex; flex-wrap: wrap; gap: 16px; }}
            .cam-box {{ background: #262626; border-radius: 8px; padding: 10px;
                        width: 300px; }}
            .cam-header {{ display: flex; align-items: center; gap: 8px;
                           margin-bottom: 8px; font-size: 0.85em; }}
            .dot {{ width: 12px; height: 12px; border-radius: 50%; flex-shrink: 0; }}
            .cam-label {{ font-weight: bold; }}
            .cam-status {{ color: #999; margin-left: auto; }}
            .cam-image {{ width: 100%; border-radius: 4px; background: #000;
                          min-height: 150px; }}
        </style>
    </head>
    <body>
        <h1>Smart Dartboard - Camera Status</h1>
        <div class="grid">
            {rows}
        </div>
    </body>
    </html>
    """
    return Response(html, mimetype="text/html")


def process_throw():
    """All 3 cameras have a new frame - diff each against its baseline,
    look up the board position from the calibration table, score it,
    accumulate into the current turn, then promote each latest frame
    to be the new baseline for next time.

    KNOWN LIMITATION: this waits for exactly 3 darts before reporting
    a turn to the ESP32. Real darts allows finishing on dart 1 or 2 of
    a turn (an early checkout) - this MVP doesn't detect that, since
    there's currently no signal distinguishing "turn over early" from
    "still waiting for dart 3". Two practical fixes once you're at this
    stage: (a) send an early-checkout total as soon as a dart's own
    running total reaches exactly the player's remaining score (this
    needs the Pi to know the player's current remaining score, which
    it doesn't yet - would need syncing that from the ESP32), or (b)
    keep a manual "end turn now" trigger (e.g. the SmartDartboard's own
    keypad '#') as a fallback for early checkouts even in camera mode.
    """
    with state_lock:
        points = {}
        for cam_id in CAMERA_IDS:
            cam = cameras[cam_id]
            centroid = find_change_centroid(cam["baseline"], cam["latest"])
            points[cam_id] = centroid
            last_centroids[cam_id] = centroid
            log.info(f"camera {cam_id}: change centroid = {centroid}")

        if any(p is None for p in points.values()):
            log.warning("no significant change detected on at least one camera - "
                        "skipping this throw (maybe a miss, or lighting/threshold issue)")
        else:
            result = triangulate(points)
            if result is None:
                log.warning("triangulate() had no calibration match close enough - "
                            "run calibrate.py to build up the calibration table")
            else:
                board_x, board_y, segment = result
                score = segment_to_score(segment)
                current_turn_darts.append((score, segment))
                log.info(f"Dart {len(current_turn_darts)}/3 this turn: "
                         f"{segment} = {score} points (board {board_x:.1f}, {board_y:.1f})")

                if len(current_turn_darts) >= 3:
                    turn_total = sum(s for s, _ in current_turn_darts)
                    last_segment = current_turn_darts[-1][1]
                    last_was_double = is_double_segment(last_segment)
                    send_to_dartboard(turn_total, last_was_double)
                    current_turn_darts.clear()

        # This frame is now the baseline for detecting the *next* throw.
        for cam_id in CAMERA_IDS:
            cameras[cam_id]["baseline"] = cameras[cam_id]["latest"]
            cameras[cam_id]["latest"] = None


if __name__ == "__main__":
    # Debug server is fine here - this is low-traffic, turn-based
    # (a handful of requests per throw, not continuous load), so the
    # weak Pi B+ doesn't need a production WSGI server for this.
    app.run(host="0.0.0.0", port=5000)
