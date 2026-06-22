#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Update the push command error message to be more explicit about authorization
sed -i 's/Failed to push scrcpy-server.jar. Check \/tmp\/adb_push.log via SSH./Failed to push payload. Make sure USB Debugging is ON and your phone AUTHORIZED the TV. (See \/tmp\/adb_push.log)/g' "$FILE"
