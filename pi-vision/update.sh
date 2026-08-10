#!/bin/bash
# update.sh - pulls the latest code from git and restarts the service.
# Run manually, or on a schedule via cron (see README.md for how to
# set that up so this behaves like the ESP32's OTA updates - checking
# periodically rather than needing you to SSH in every time).

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
