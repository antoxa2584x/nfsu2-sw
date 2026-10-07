#!/usr/bin/env bash
# Build the NFSU2 (Xbox) recompilation for macOS (Apple Silicon or Intel),
# with the Vulkan renderer on MoltenVK -- the combination that draws a window
# there (platform/macos/build.sh).
#
#   cmake configure + build  ->  $BUILD_DIR/nfsu2_recomp
#
# Needs (Homebrew):
#   brew install cmake pkg-config molten-vk vulkan-loader sdl2 libepoxy \
#                openssl@3 glslang spirv-tools
# (Xcode Command Line Tools: xcode-select --install. libepoxy and pkg-config
# are required even in the Vulkan build: the D3D8 shim links them.)
# Optional, for the movies instead of the slow lifted VP6 decoder:
#   tools/build_ffmpeg_vp6.sh mac   (LGPL, VP6 only)
#
# Environment:
#   BUILD_DIR       default <repo>/build
#   JOBS            parallel compile jobs (default: hw.ncpu)
#   NFSU2_GEN_DIR   lifted C from tools/regen.sh (default <repo>/xboxrecomp/gen)
#   VULKAN          1 (default) Vulkan on MoltenVK. 0 builds the GL renderer,
#                   which has no window path on macOS yet: it would hit the
#                   same Cocoa main-thread rule that used to leave the Vulkan
#                   build with sound and no picture.
#   FFMPEG_DIR      VP6-only FFmpeg (default <repo>/../ffmpeg-vp6-mac)
#
# At run time the disc is $NFSU2_GAME_DIR, else a data/ directory next to
# the executable (default.xbe, NFSUNDER/, B3/), else ./game.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$REPO/build}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
VK="${VULKAN:-1}"
GEN="${NFSU2_GEN_DIR:-$REPO/xboxrecomp/gen}"

fail() { echo "error: $*" >&2; exit 1; }

# Homebrew's .pc files (epoxy, sdl2) are outside pkg-config's default path on
# Apple Silicon, and a clean environment then fails to find epoxy. Before the
# checks below, so that "pkg-config --exists epoxy" sees them too.
if [ "$(uname -s)" = "Darwin" ]; then
    HPREFIX="${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
    export PKG_CONFIG_PATH="$HPREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

command -v cmake >/dev/null || fail "cmake not found (brew install cmake)"
command -v pkg-config >/dev/null || fail "pkg-config not found (brew install pkg-config)"
pkg-config --exists epoxy || fail "epoxy not found (brew install libepoxy)"
[ -f "$GEN/recomp_funcs.h" ] || fail "no generated code in $GEN (run tools/regen.sh, or set NFSU2_GEN_DIR)"

VK_ARGS=(-DNFSU2_VULKAN=ON)
if [ "$VK" != "1" ]; then
    echo "warning: the GL renderer has no window path on macOS -- you will get" >&2
    echo "         sound and no picture. Set VULKAN=1 (the default)." >&2
    VK_ARGS=(-DNFSU2_VULKAN=OFF)
fi

FFMPEG_DIR="${FFMPEG_DIR:-$REPO/../ffmpeg-vp6-mac}"
FF_ARGS=(-DNFSU2_FFMPEG_DIR=)
if [ -f "$FFMPEG_DIR/lib/libavcodec.a" ]; then
    FF_ARGS=(-DNFSU2_FFMPEG_DIR="$FFMPEG_DIR")
else
    echo "warning: no FFmpeg in $FFMPEG_DIR -- movies use the lifted VP6 decoder" >&2
    echo "         (tools/build_ffmpeg_vp6.sh mac)" >&2
fi

cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DNFSU2_GEN_DIR="$GEN" "${VK_ARGS[@]}" "${FF_ARGS[@]}"
cmake --build "$BUILD" -j"$JOBS"

echo
echo "built $BUILD/nfsu2_recomp"
echo "  NFSU2_GAME_DIR=/path/to/disc $BUILD/nfsu2_recomp"
echo "  or drop a data/ directory (default.xbe, NFSUNDER/, B3/) next to the"
echo "  executable and run it with no environment at all."
