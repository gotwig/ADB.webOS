#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Force ADB to use the connected USB device using the '-d' flag for all commands
sed -i 's/%s push/%s -d push/g' "$FILE"
sed -i 's/%s forward/%s -d forward/g' "$FILE"
sed -i 's/%s shell/%s -d shell/g' "$FILE"

