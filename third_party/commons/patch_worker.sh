#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

# Add better error handling logic to session_worker so it returns to UI instead of exiting on failure
sed -i 's/streaming_error(session, 1, "Failed to connect to scrcpy server");/streaming_error(session, GS_WRONG_STATE, "Failed to connect to scrcpy server");/g' "$FILE"
sed -i 's/streaming_error(session, 2, "Failed to open SS4S video decoder");/streaming_error(session, GS_WRONG_STATE, "Failed to open SS4S video decoder");/g' "$FILE"
