"""
scoring.py - converts a calibration segment label (as typed into
calibrate.py, e.g. "T20", "D16", "25", "bull") into a point value and
whether it counts as a double for checkout purposes.

Label format expected (case-insensitive):
  "T<n>"  - treble, value = 3n         e.g. "T20" -> 60
  "D<n>"  - double, value = 2n         e.g. "D16" -> 32
  "<n>"   - single, value = n          e.g. "5"   -> 5
  "25"    - outer bull, value = 25 (NOT a double for checkout purposes)
  "bull"  - inner bull, value = 50 (DOES count as a double for checkout,
            same as real darts rules - it's effectively "double 25")
  "miss" / "0" / "" - value 0
"""


def segment_to_score(segment: str) -> int:
    s = segment.strip().lower()
    if s in ("miss", "0", ""):
        return 0
    if s == "bull":
        return 50
    if s == "25":
        return 25
    if s.startswith("t") and s[1:].isdigit():
        return 3 * int(s[1:])
    if s.startswith("d") and s[1:].isdigit():
        return 2 * int(s[1:])
    if s.isdigit():
        return int(s)
    return 0  # unrecognized label - treat as no score rather than crash


def is_double_segment(segment: str) -> bool:
    s = segment.strip().lower()
    if s == "bull":
        return True  # inner bull counts as a double for checkout, per standard rules
    return s.startswith("d") and s[1:].isdigit()
