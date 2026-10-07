#!/usr/bin/env bash
# Build the NFSU2 (Xbox) recompilation as a Switch homebrew NRO and stage an
# SD-card layout (platform/switch/build.sh):
#
#   <SD>/switch/nfsu2x/nfsu2x.nro
#   <SD>/switch/nfsu2x/game/        the extracted disc: default.xbe, NFSUNDER/, ...
#   <SD>/switch/nfsu2x/save/        created on first run
#   <SD>/switch/nfsu2x/nfsu2x_env.txt   optional KEY=VALUE runtime switches
#   <SD>/switch/nfsu2x/nfsu2x_log.txt   written when no nxlink host is listening
#
# The game reads the unpacked disc files directly -- never the ISO.
#
# Environment:
#   DEVKITPRO        default /opt/devkitpro (switch-dev, switch-sdl2, switch-mesa)
#   XBOXRECOMP_DIR   toolkit (default: the vendored xboxrecomp/)
#   NFSU2_GEN_DIR    lifted C from tools/regen.sh (default /root/nfsu2x/gen)
#   NFSU2_GAME_SRC   extracted disc to stage (default /root/nfsu2x/game)
#   SD_ROOT          staging SD root (default <repo>/switch_sd)
#   BUILD_DIR        default /root/nfsu2x/build-switch
#   JOBS             parallel compile jobs (default: nproc, capped by memory)
#   VULKAN=1         the Vulkan renderer: stages nfsu2x-vulkan.nro, built in
#                    BUILD_DIR (default /root/nfsu2x/build-switch-vk) against
#                    NVK_SDK (mesa-switch install, default /root/nfsu2x/mesa-sdk/usr/local)
#                    and GLSLANG_DIR (glslang for the Switch, default /root/nfsu2x/glslang-switch)
#   LTO=1            link-time optimisation of the lifted code (NFSU2_LTO):
#                    BUILD_DIR gets a -lto suffix and the NRO is staged as
#                    nfsu2x[-vulkan]-lto.nro next to the normal one, for A/B.
#                    LTO_JOBS parallel link jobs (default 4, ~2 GB each)
#   FFMPEG_DIR       VP6-only LGPL FFmpeg for the movies (tools/build_ffmpeg_vp6.sh
#                    switch; default /root/nfsu2x/ffmpeg-vp6-switch). Without
#                    it the lifted (slow) decoder plays them.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TK="${XBOXRECOMP_DIR:-$REPO/xboxrecomp}"
GEN="${NFSU2_GEN_DIR:-/root/nfsu2x/gen}"
GAME="${NFSU2_GAME_SRC:-/root/nfsu2x/game}"
SD="${SD_ROOT:-$REPO/switch_sd}"
VK="${VULKAN:-0}"
if [ "$VK" = "1" ]; then
    BUILD="${BUILD_DIR:-/root/nfsu2x/build-switch-vk}"
    NRO_NAME=nfsu2x-vulkan.nro
    NVK_SDK="${NVK_SDK:-/root/nfsu2x/mesa-sdk/usr/local}"
    GLSLANG_DIR="${GLSLANG_DIR:-/root/nfsu2x/glslang-switch}"
    VK_ARGS=(-DNFSU2_VULKAN=ON -DNVK_SDK="$NVK_SDK"
             -Dglslang_DIR="$GLSLANG_DIR/lib/cmake/glslang")
else
    BUILD="${BUILD_DIR:-/root/nfsu2x/build-switch}"
    NRO_NAME=nfsu2x.nro
    VK_ARGS=(-DNFSU2_VULKAN=OFF)
fi

LTO_ARGS=(-DNFSU2_LTO=OFF)
if [ "${LTO:-0}" = "1" ]; then
    [ -z "${BUILD_DIR:-}" ] && BUILD="$BUILD-lto"
    NRO_NAME="${NRO_NAME%.nro}-lto.nro"
    LTO_ARGS=(-DNFSU2_LTO=ON -DNFSU2_LTO_JOBS="${LTO_JOBS:-4}")
fi

FFMPEG_DIR="${FFMPEG_DIR:-/root/nfsu2x/ffmpeg-vp6-switch}"
if [ -f "$FFMPEG_DIR/lib/libavcodec.a" ]; then
    FF_ARGS=(-DNFSU2_FFMPEG_DIR="$FFMPEG_DIR")
else
    echo "warning: no FFmpeg in $FFMPEG_DIR -- movies use the lifted VP6 decoder" >&2
    FF_ARGS=(-DNFSU2_FFMPEG_DIR=)
fi

fail() { echo "error: $*" >&2; exit 1; }
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || fail "devkitPro not found at $DEVKITPRO"
[ -f "$DEVKITPRO/portlibs/switch/include/SDL2/SDL.h" ] || fail "switch-sdl2 missing"
[ -f "$GEN/recomp_funcs.h" ] || fail "no generated code in $GEN (run tools/regen.sh)"
[ -f "$GAME/default.xbe" ] || fail "no extracted disc in $GAME"

if [ -z "${JOBS:-}" ]; then
    mem_mb=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1024 ))
    JOBS=$(( mem_mb / 1500 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS=$(nproc)
fi

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXBOXRECOMP_DIR="$TK" -DNFSU2_GEN_DIR="$GEN" \
    -DNFSU2_SWITCH_ICON="$REPO/assets/icon.jpg" "${VK_ARGS[@]}" "${FF_ARGS[@]}" \
    "${LTO_ARGS[@]}" >/dev/null
ninja -C "$BUILD" -j"$JOBS"

DEST="$SD/switch/nfsu2x"
mkdir -p "$DEST/game"
cp "$BUILD/nfsu2_recomp.nro" "$DEST/$NRO_NAME"
# The disc image is 2.6 GB: copy only what changed.
rsync -a --size-only "$GAME/" "$DEST/game/" --exclude '*_analysis.json'
echo
echo "staged $DEST"
echo "  $NRO_NAME $(stat -c %s "$DEST/$NRO_NAME") bytes, game/ $(du -sh "$DEST/game" | cut -f1)"
echo "  copy <SD>/switch/nfsu2x/ to the card (or point Eden's sdmc at $SD),"
echo "  and start it from hbmenu with title takeover (hold R on a game) for full RAM."
