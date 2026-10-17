#!/usr/bin/env bash
# Copy the game ISO to the phone over adb; the app extracts it on the next
# start and deletes it afterwards. Android 11+ hides Android/data from file
# managers, so adb (or a PC over USB in file-transfer mode) is the way in --
# or use the app's "Choose ISO file" button, which needs no copy at all.
#
#   android/tools/push_game.sh /path/to/game.iso        (PACKAGE=com.nfsu2x.nfsu2)
#
# An already extracted disc (default.xbe, NFSUNDER/, ...) can still be pushed:
#   android/tools/push_game.sh /path/to/extracted/dir
set -euo pipefail
SRC="${1:?usage: push_game.sh <game.iso | extracted disc dir>}"
PKG="${PACKAGE:-com.nfsu2x.nfsu2}"
BASE="/sdcard/Android/data/$PKG/files"
if [ -d "$SRC" ]; then
    [ -f "$SRC/default.xbe" ] || { echo "no default.xbe in $SRC" >&2; exit 1; }
    adb shell mkdir -p "$BASE/game"
    adb push "$SRC/." "$BASE/game/"
else
    adb shell mkdir -p "$BASE/iso"
    adb push "$SRC" "$BASE/iso/"
fi
echo "done; open the app"
