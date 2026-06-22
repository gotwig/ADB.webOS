#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Cleanup the duplicate system(cmd) call
sed -i '/\/\/ Ensure ADB server is started/{n;N;s/system(cmd);\n    snprintf(cmd, sizeof(cmd), "%s start-server", adb);/snprintf(cmd, sizeof(cmd), "%s start-server", adb);/}' "$FILE"

