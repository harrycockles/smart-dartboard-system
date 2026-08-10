#!/usr/bin/env python3
"""
calibrate.py - run this once your 3 ESP32-CAMs and the Pi server are
both up and running, to build the calibration table triangulate.py
needs. Without running this at least a few dozen times across
different board positions, nothing will ever be detected.

How to use:
  1. Make sure server.py is running (in another terminal, or as the
     systemd service - see README.md).
  2. Run this script: python3 calibrate.py
  3. It resets all 3 cameras' baselines (clean "before" shot).
  4. Throw one dart at a KNOWN position and tell this script what you
     aimed for (e.g. "T20", "D16", "25", or just x,y in mm from center
     - whatever labeling scheme you want to use later for scoring).
  5. It fetches each camera's detected change centroid from the server
     and appends a calibration point.
  6. Repeat for as many positions as you're willing to throw at -
     more points, and points spread across the whole board (not just
     bullseye), give triangulate.py much better accuracy. Covering
     each of the 20 numbers plus a few doubles/trebles/bulls is a
     reasonable starting target.

This talks to the Pi server over HTTP, so it can be run from the Pi
itself or from another machine on the same network - just set
SERVER_URL below if not running it locally.
"""

import json
import time
from pathlib import Path

import requests

SERVER_URL = "http://localhost:5000"
CALIBRATION_FILE = Path(__file__).parent / "calibration_table.json"


def load_table():
    if CALIBRATION_FILE.exists():
        with open(CALIBRATION_FILE) as f:
            return json.load(f)
    return []


def save_table(table):
    with open(CALIBRATION_FILE, "w") as f:
        json.dump(table, f, indent=2)


def reset_baselines():
    for cam_id in ("1", "2", "3"):
        r = requests.post(f"{SERVER_URL}/baseline/{cam_id}")
        r.raise_for_status()
    print("Baselines reset. Make sure each camera sends a fresh baseline "
          "frame now (however your ESP32-CAMs are triggered to capture) "
          "before you throw.")


def get_latest_centroids():
    """Fetches the most recent per-camera change centroids from the
    server - populated automatically once all 3 cameras have posted a
    frame for this throw."""
    r = requests.get(f"{SERVER_URL}/last_centroids")
    r.raise_for_status()
    data = r.json()

    points = {}
    for cam_id in ("1", "2", "3"):
        val = data.get(cam_id)
        if val is None:
            print(f"  Warning: camera {cam_id} has no centroid yet - did all "
                  f"3 cameras post their after-throw frame? Skipping this point.")
            return None
        points[cam_id] = tuple(val)
    return points


def main():
    table = load_table()
    print(f"Loaded {len(table)} existing calibration points.")

    while True:
        cmd = input("\n[t]hrow a calibration dart, [q]uit: ").strip().lower()
        if cmd == "q":
            break
        if cmd != "t":
            continue

        reset_baselines()
        input("Throw your dart now, then press Enter once it's landed "
              "and the cameras have posted their after-throw frames...")

        segment = input("What did you aim for/hit (e.g. T20, D16, 25, bull)? ").strip()
        board_x = float(input("Board X position (mm from center, your convention): "))
        board_y = float(input("Board Y position (mm from center, your convention): "))

        points = get_latest_centroids()
        if points is None:
            print("Skipping this calibration point - try the throw again.")
            continue

        table.append({
            "segment": segment,
            "board_x": board_x,
            "board_y": board_y,
            "cameras": {cam_id: list(pt) for cam_id, pt in points.items()},
        })
        save_table(table)
        print(f"Saved. {len(table)} calibration points total.")


if __name__ == "__main__":
    main()
