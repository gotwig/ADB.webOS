#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# The original node.js code uses "localabstract:scrcpy" which might fail on some setups. 
# Also, we should capture its output to see WHY it's failing.
sed -i 's/snprintf(cmd, sizeof(cmd), "%s forward tcp:27183 localabstract:scrcpy", adb);/snprintf(cmd, sizeof(cmd), "%s forward tcp:27183 localabstract:scrcpy 2>\&1 | tee \/tmp\/adb_forward.log", adb);/g' "$FILE"

sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port");/streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port. Check \/tmp\/adb_forward.log via SSH.");/g' "$FILE"
