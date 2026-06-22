#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# We must reference the files by their correct absolute path inside the app bundle on webOS
sed -i 's/const char \*adb = ".\/adb";/const char \*adb = "\/media\/developer\/apps\/usr\/palm\/applications\/com.limelight.webos\/adb";/g' "$FILE"

sed -i 's/scrcpy-server.jar \/data\/local\/tmp\/scrcpy-server.jar/\/media\/developer\/apps\/usr\/palm\/applications\/com.limelight.webos\/scrcpy-server.jar \/data\/local\/tmp\/scrcpy-server.jar/g' "$FILE"

