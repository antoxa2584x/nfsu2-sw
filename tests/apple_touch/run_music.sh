#!/usr/bin/env bash
# Uses a generated tone, never a supplied game/music asset.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK="$(xcrun --sdk macosx --show-sdk-path)"
BUILD="${MUSIC_TEST_BUILD:-$ROOT/work/music-test}"
mkdir -p "$BUILD"
ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=2' -codec:a libmp3lame -y "$BUILD/track.mp3"
xcrun clang -fobjc-arc -Wall -Wextra -Werror -Wno-unused-parameter -Wno-nonnull \
    -target arm64-apple-ios16.0-macabi -isysroot "$SDK" \
    -isystem "$SDK/System/iOSSupport/usr/include" \
    -iframework "$SDK/System/iOSSupport/System/Library/Frameworks" \
    -framework UIKit -framework Foundation -framework AVFoundation -framework UniformTypeIdentifiers \
    "$ROOT/tests/apple_touch/test_music.m" -o "$BUILD/test_music"
"$BUILD/test_music" "$BUILD/track.mp3"
