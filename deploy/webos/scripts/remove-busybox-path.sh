#!/bin/sh

PROFILE="/home/root/.profile"

sed '/# Added by com\.adb\.webos/{N;d;}' "$PROFILE" > "$PROFILE.tmp" &&
mv "$PROFILE.tmp" "$PROFILE"