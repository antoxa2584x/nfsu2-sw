#!/usr/bin/env bash
# Build the NFSU2 (Xbox) recompilation for Linux (SDL2, Vulkan; platform/linux/build.sh).
#
#   cmake configure + build  ->  $BUILD_DIR/nfsu2_recomp
#
# Needs: cmake, an SDL2 dev package, a Vulkan loader (lavapipe is enough --
# there is no GPU required), and optionally a VP6-only LGPL FFmpeg for the
# movies (tools/build_ffmpeg_vp6.sh linux).
#
# Usage:
#   platform/linux/build.sh [GAME_DIR]
#
#   GAME_DIR   the extracted disc (a directory with default.xbe): build, then
#              start the game with NFSU2_GAME_DIR=GAME_DIR. Everything.
#   no argument  build only, then print the command to run.
#
# Environment:
#   BUILD_DIR       default <repo>/build
#   JOBS            parallel compile jobs (default: nproc)
#   NFSU2_GEN_DIR   lifted C from tools/regen.sh (default <repo>/xboxrecomp/gen)
#   VULKAN          1 (default) Vulkan; 0 for the GL renderer (nv2a_gl)
#   FFMPEG_DIR      VP6-only FFmpeg (default <repo>/../ffmpeg-vp6-linux)
#
# At run time the disc is $NFSU2_GAME_DIR, else a game/ directory next to
# the executable, else ./game.
#
# Headless (no GPU, no window), the way the Vulkan renderer is tested:
#   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
#   RECOMP_VK_HEADLESS=1 RECOMP_VK_VALIDATION=1 NFSU2_GAME_DIR=/path/to/disc \
#   build/nfsu2_recomp
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$REPO/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
VK="${VULKAN:-1}"
GEN="${NFSU2_GEN_DIR:-$REPO/xboxrecomp/gen}"

fail() { echo "error: $*" >&2; exit 1; }

usage() {
    echo "usage: ${0##*/} [GAME_DIR]"
    echo "  GAME_DIR    the extracted disc (a directory with default.xbe):"
    echo "              build, then start the game with NFSU2_GAME_DIR pointing at it"
    echo "  no argument build only, then print how to run"
    exit "${1:-0}"
}
case "${1:-}" in
    -h|--help) usage 0 ;;
    -*) echo "error: unknown option $1" >&2; usage 1 ;;
esac
GAME="${1:-}"
if [ -n "$GAME" ]; then
    [ -f "$GAME/default.xbe" ] || fail "$GAME has no default.xbe (point at the extracted disc)"
fi

command -v cmake >/dev/null || fail "cmake not found"
[ -f "$GEN/recomp_funcs.h" ] || fail "no generated code in $GEN (run tools/regen.sh, or set NFSU2_GEN_DIR)"

VK_ARGS=(-DNFSU2_VULKAN=ON)
if [ "$VK" != "1" ]; then
    VK_ARGS=(-DNFSU2_VULKAN=OFF)
fi

FFMPEG_DIR="${FFMPEG_DIR:-$REPO/../ffmpeg-vp6-linux}"
FF_ARGS=(-DNFSU2_FFMPEG_DIR=)
if [ -f "$FFMPEG_DIR/lib/libavcodec.a" ]; then
    FF_ARGS=(-DNFSU2_FFMPEG_DIR="$FFMPEG_DIR")
else
    echo "warning: no FFmpeg in $FFMPEG_DIR -- movies use the lifted VP6 decoder" >&2
    echo "         (tools/build_ffmpeg_vp6.sh linux)" >&2
fi

cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DNFSU2_GEN_DIR="$GEN" "${VK_ARGS[@]}" "${FF_ARGS[@]}"
cmake --build "$BUILD" -j"$JOBS"

echo
echo "built $BUILD/nfsu2_recomp"

if [ -n "$GAME" ]; then
    echo "starting it with NFSU2_GAME_DIR=$GAME"
    NFSU2_GAME_DIR="$GAME" exec "$BUILD/nfsu2_recomp"
fi

# Build only: say how to run it, with the disc it would find on its own
# (the same lookup the binary does: $NFSU2_GAME_DIR, game/ next to it,
# then ./game in the working directory).
found=""
for d in "$BUILD/game" "$PWD/game"; do
    if [ -f "$d/default.xbe" ]; then found="$d"; break; fi
done
if [ -n "${NFSU2_GAME_DIR:-}" ]; then
    echo "run:  NFSU2_GAME_DIR=$NFSU2_GAME_DIR $BUILD/nfsu2_recomp"
elif [ -n "$found" ]; then
    echo "run:  $BUILD/nfsu2_recomp     (it picks up $found)"
else
    echo "run:  ${0##*/} /path/to/game   (builds and starts)"
    echo "      or put the extracted disc in a game/ directory next to the binary"
fi
