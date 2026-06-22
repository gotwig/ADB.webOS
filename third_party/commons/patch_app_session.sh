#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/app_session.c"
sed -i '/#include "logging.h"/a #include "stream/session_priv.h"\n#include "stream/session_worker.h"' "$FILE"
