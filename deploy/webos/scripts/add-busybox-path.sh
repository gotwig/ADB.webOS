#!/bin/sh

PROFILE='/home/root/.profile'

if ! grep -q 'com.adb.webos/global-syspath' "$PROFILE" 2>/dev/null; then
    {
        echo ''
        echo '# Added by com.adb.webos'
        echo 'export PATH="/media/developer/apps/usr/palm/applications/com.adb.webos/global-syspath:$PATH"'
    } >> "$PROFILE"
fi