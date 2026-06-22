#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Let's capture exactly what ADB outputs when it fails
sed -i 's/snprintf(cmd, sizeof(cmd), "%s push \/media\/developer\/apps\/usr\/palm\/applications\/com.limelight.webos\/scrcpy-server.jar \/data\/local\/tmp\/scrcpy-server.jar", adb);/snprintf(cmd, sizeof(cmd), "%s push \/media\/developer\/apps\/usr\/palm\/applications\/com.limelight.webos\/scrcpy-server.jar \/data\/local\/tmp\/scrcpy-server.jar 2>\&1 | tee \/tmp\/adb_push.log", adb);/g' "$FILE"

sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy-server.jar to device (is phone connected?)");/streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy-server.jar. Check \/tmp\/adb_push.log via SSH.");/g' "$FILE"

