#!/bin/bash
# update.sh - pulls the latest code from git and restarts the service.
# Run manually, or on a schedule via cron (see README.md for how to
# set that up so this behaves like the ESP32's OTA updates - checking
# periodically rather than needing you to SSH in every time).
#
# This lives inside a subfolder of the shared smart-dartboard-system
# repo (also used by the ESP32 firmware) - git pull still operates on
# the whole repo even run from a subfolder, which is fine: it'll also
# pull firmware/ changes, the Pi just doesn't care about those files.

set -e

cd "$(dirname "$0")"

echo "Checking for updates..."
git fetch origin

LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main)

if [ "$LOCAL" == "$REMOTE" ]; then
  echo "Already up to date."
  exit 0
fi

echo "New version available, pulling..."
git pull origin main

echo "Restarting service..."
sudo systemctl restart dartboard-vision

echo "Done. Now running:"
git log -1 --oneline
