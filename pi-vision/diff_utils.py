"""
diff_utils.py - finds the pixel location where a new dart appeared,
by comparing a "before" and "after" image from the same camera.

Deliberately uses PIL + numpy rather than OpenCV: numpy installs from
a pre-built package on Raspberry Pi OS (`sudo apt install
python3-numpy`), no compilation needed, which matters a lot on a
single-core ARM11 Pi B+. OpenCV would work too if you ever want fancier
detection, but isn't needed for this and is much heavier to install.
"""

import numpy as np
from PIL import Image, ImageChops

# How different a pixel needs to be (0-255 grayscale) to count as
# "changed". Start here and tune based on your actual lighting/camera
# noise once you have real hardware - too low picks up sensor noise as
# false positives, too high misses genuine dart changes.
DIFF_THRESHOLD = 30

# A changed region smaller than this many pixels is treated as noise,
# not a real dart - tune once you know your cameras' resolution and
# how large a dart appears in-frame.
MIN_CHANGED_PIXELS = 40


def find_change_centroid(baseline: Image.Image, current: Image.Image):
    """
    Returns (x, y) pixel coordinates of the centroid of the largest
    changed region between two grayscale images, or None if nothing
    that looks like a real change was found.
    """
    if baseline.size != current.size:
        current = current.resize(baseline.size)

    diff = ImageChops.difference(baseline, current)
    arr = np.array(diff)

    changed_mask = arr > DIFF_THRESHOLD
    changed_count = np.count_nonzero(changed_mask)

    if changed_count < MIN_CHANGED_PIXELS:
        return None

    ys, xs = np.nonzero(changed_mask)
    centroid_x = float(np.mean(xs))
    centroid_y = float(np.mean(ys))
    return (centroid_x, centroid_y)
