"""
triangulate.py - turns 3 cameras' pixel coordinates into a board
position, using a calibration table instead of computed camera
geometry.

Why a lookup table rather than real triangulation math: true geometric
triangulation needs each camera's exact position, angle, and field of
view relative to the board, measured precisely - genuinely fiddly to
get right without proper calibration equipment, and small errors
compound into large position errors. A calibration table sidesteps
this: throw darts at known positions (see calibrate.py), record what
each camera saw for each one, then for a new throw find the closest
match(es) in that table and interpolate. This is a well-established
DIY approach for exactly this kind of multi-camera hobbyist setup.

The table starts empty - nothing will be detected until you've run
calibrate.py with real hardware to build it up. The more calibration
points you record (and the more evenly spread across the board), the
more accurate this gets.
"""

import json
import math
from pathlib import Path

CALIBRATION_FILE = Path(__file__).parent / "calibration_table.json"

# How many nearest calibration points to blend together for the
# final estimate. 3-5 is a reasonable starting point - more smooths
# out noise but blurs precision near segment boundaries.
K_NEAREST = 4

# If even the closest calibration point is farther than this (in the
# same pixel-distance units used below), treat it as "no confident
# match" rather than guessing - tune once you have real data to see
# what a reasonable throw-to-throw variance looks like.
MAX_MATCH_DISTANCE = 150


def _load_table():
    if not CALIBRATION_FILE.exists():
        return []
    with open(CALIBRATION_FILE) as f:
        return json.load(f)


def _signature_distance(a, b):
    """Euclidean distance across all 3 cameras' (x,y) pixel coordinates
    combined - treats the 3 cameras' readings as one 6-dimensional
    fingerprint for a given board position."""
    total = 0.0
    for cam_id in ("1", "2", "3"):
        ax, ay = a[cam_id]
        bx, by = b[cam_id]
        total += (ax - bx) ** 2 + (ay - by) ** 2
    return math.sqrt(total)


def triangulate(observed_points: dict):
    """
    observed_points: {"1": (x,y), "2": (x,y), "3": (x,y)} - the change
    centroid each camera detected for this throw.

    Returns (board_x, board_y, segment_label) or None if no calibration
    match is close enough to trust.
    """
    table = _load_table()
    if not table:
        return None

    scored = []
    for entry in table:
        dist = _signature_distance(observed_points, entry["cameras"])
        scored.append((dist, entry))
    scored.sort(key=lambda pair: pair[0])

    best_dist, best_entry = scored[0]
    if best_dist > MAX_MATCH_DISTANCE:
        return None

    # Inverse-distance-weighted average of the K nearest calibration
    # points, so the estimate isn't just "snap to the single closest
    # point you ever calibrated" - smooths things out a bit.
    neighbors = scored[:K_NEAREST]
    weight_sum = 0.0
    x_sum = 0.0
    y_sum = 0.0
    for dist, entry in neighbors:
        weight = 1.0 / (dist + 1e-6)  # avoid divide-by-zero for an exact match
        weight_sum += weight
        x_sum += entry["board_x"] * weight
        y_sum += entry["board_y"] * weight

    board_x = x_sum / weight_sum
    board_y = y_sum / weight_sum
    segment = best_entry.get("segment", "?")  # label from the single closest point

    return (board_x, board_y, segment)
