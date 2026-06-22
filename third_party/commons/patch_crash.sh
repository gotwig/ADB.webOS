#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# We must use SDL_LockMutex properly around state changes to prevent crashes
sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to connect to scrcpy TCP socket");/session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);\n        streaming_error(session, GS_WRONG_STATE, "Failed to connect to scrcpy TCP socket");/g' "$FILE"

sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy-server.jar to device (is phone connected?)");/session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);\n        streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy-server.jar to device (is phone connected?)");/g' "$FILE"

sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port");/session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);\n        streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port");/g' "$FILE"

sed -i 's/streaming_error(session, GS_WRONG_STATE, "Failed to open SS4S video decoder");/session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);\n        streaming_error(session, GS_WRONG_STATE, "Failed to open SS4S video decoder");/g' "$FILE"
