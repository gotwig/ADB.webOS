#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Revert the kill-server command since we only want ONE adb instance running across the entire TV.
# Let's rely strictly on `adb start-server` which does nothing if it's already running.
sed -i 's/snprintf(cmd, sizeof(cmd), "%s kill-server", adb);\n    system(cmd);\n    snprintf(cmd, sizeof(cmd), "%s start-server", adb);/snprintf(cmd, sizeof(cmd), "%s start-server", adb);/g' "$FILE"

# Make sure we didn't miss it via formatting differences
sed -i '/kill-server/d' "$FILE"

