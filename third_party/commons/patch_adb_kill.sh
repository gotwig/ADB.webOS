#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# We should kill any existing adb server before starting ours to avoid conflicts
# We will run `adb kill-server` before `adb start-server`
sed -i 's/snprintf(cmd, sizeof(cmd), "%s start-server", adb);/snprintf(cmd, sizeof(cmd), "%s kill-server", adb);\n    system(cmd);\n    snprintf(cmd, sizeof(cmd), "%s start-server", adb);/g' "$FILE"

