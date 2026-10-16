#!/usr/bin/env bash
# Exercise the actual UIKit view on an Apple-silicon Mac through Catalyst.
# Optional argument: an existing directory for menu/drive PNG previews.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK="$(xcrun --sdk macosx --show-sdk-path)"
SDL_INCLUDE="${SDL_INCLUDE:-$(brew --prefix sdl2)/include/SDL2}"
BUILD="${TOUCH_TEST_BUILD:-$ROOT/work/touch-view-test}"
mkdir -p "$BUILD"
xcrun clang -fobjc-arc -Wall -Wextra -Wno-unused-parameter \
    -target arm64-apple-ios16.0-macabi -isysroot "$SDK" \
    -isystem "$SDK/System/iOSSupport/usr/include" \
    -iframework "$SDK/System/iOSSupport/System/Library/Frameworks" \
    ${TOUCH_REGION_FLAGS:-} -DSDL_VIDEO_DRIVER_UIKIT=1 -I"$SDL_INCLUDE" \
    -framework UIKit -framework Foundation -framework QuartzCore -framework CoreGraphics -framework UniformTypeIdentifiers -framework AVFoundation \
    "$ROOT/tests/apple_touch/test_view.m" "$ROOT/src/ios/ios_xiso.c" \
    "$ROOT/src/ios/ios_touch_layout.m" "$ROOT/src/ios/ios_touch_settings.m" "$ROOT/src/ios/ios_music.m" -o "$BUILD/test_view"
"$BUILD/test_view" "$@"
