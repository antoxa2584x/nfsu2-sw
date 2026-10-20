#!/usr/bin/env bash
# Build the NFSU2 (Xbox) recompilation for the Steam Deck and stage a folder
# to copy onto it:
#
#   nfsu2x/nfsu2x.sh          launcher (add it to Steam as a non-Steam game)
#   nfsu2x/nfsu2_recomp       the game (Vulkan renderer, x86-64-v3)
#   nfsu2x/game/              put the extracted disc here: default.xbe, ...
#   nfsu2x/nfsu2x_env.txt     optional KEY=VALUE runtime switches
#   nfsu2x/nfsu2x_log.txt     written by every run (the previous one is kept
#                             as nfsu2x_log.prev.txt)
#
# The compile runs in Valve's Steam Runtime 3 (sniper) SDK container, so the
# binary needs only glibc 2.31 and runs on SteamOS as is, or under the
# "Steam Linux Runtime 3.0 (sniper)" compatibility tool. It links the
# system's libSDL2-2.0.so.0 and libvulkan.so.1 (both on SteamOS);
# libstdc++/libgcc, glslang and FFmpeg are linked in.
#
# Environment:
#   XBOXRECOMP_DIR   toolkit (default: the vendored xboxrecomp/)
#   NFSU2_GEN_DIR    lifted C from tools/regen.sh (default /root/nfsu2x/gen)
#   FFMPEG_SRC       FFmpeg source for the VP6 decoder (default
#                    /root/nfsu2x/ref/ffmpeg-7.1); built once into FFMPEG_DIR
#   FFMPEG_DIR       default /root/nfsu2x/ffmpeg-vp6-deck
#   BUILD_DIR        default /root/nfsu2x/build-deck
#   OUT_DIR          staging root (default <repo>/deck_out); a tarball
#                    nfsu2x-deck-<version>.tar.gz is written next to it
#   IMAGE            SDK image (default registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest)
#   DECK_ARCH        target flags (default -march=x86-64-v3 -mtune=znver2;
#                    the Deck is Zen 2. FMA contraction stays off: the native
#                    rewrites match the lifted code bit for bit)
#   JOBS             parallel compile jobs (default: nproc, capped by memory)
#   NFSU2_GAME_SRC   if set, the extracted disc is copied into the stage too
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TK="${XBOXRECOMP_DIR:-$REPO/xboxrecomp}"
GEN="${NFSU2_GEN_DIR:-/root/nfsu2x/gen}"
FFMPEG_SRC="${FFMPEG_SRC:-/root/nfsu2x/ref/ffmpeg-7.1}"
FFMPEG_DIR="${FFMPEG_DIR:-/root/nfsu2x/ffmpeg-vp6-deck}"
BUILD="${BUILD_DIR:-/root/nfsu2x/build-deck}"
OUT="${OUT_DIR:-$REPO/deck_out}"
IMAGE="${IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}"
DECK_ARCH="${DECK_ARCH:--march=x86-64-v3 -mtune=znver2}"

fail() { echo "error: $*" >&2; exit 1; }
command -v docker >/dev/null || fail "docker is needed (the build runs in the Steam Runtime SDK)"
[ -f "$GEN/recomp_funcs.h" ] || fail "no generated code in $GEN (run tools/regen.sh)"
[ -f "$TK/CMakeLists.txt" ] || fail "no toolkit in $TK"

if [ -z "${JOBS:-}" ]; then
    mem_mb=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1024 ))
    JOBS=$(( mem_mb / 1200 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS=$(nproc)
fi
VERSION="$(sed -n 's/^ *set(NFSU2_SWITCH_VERSION "\(.*\)")/\1/p' "$REPO/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION=dev
mkdir -p "$BUILD" "$FFMPEG_DIR"

# Every path the container needs, mounted at the same place.
MOUNTS=(-v "$REPO:$REPO" -v "$TK:$TK" -v "$GEN:$GEN:ro" -v "$BUILD:$BUILD"
        -v "$FFMPEG_DIR:$FFMPEG_DIR")
[ -d "$FFMPEG_SRC" ] && MOUNTS+=(-v "$FFMPEG_SRC:$FFMPEG_SRC:ro")

docker run --rm "${MOUNTS[@]}" -w "$REPO" \
    -e REPO="$REPO" -e TK="$TK" -e GEN="$GEN" -e BUILD="$BUILD" -e JOBS="$JOBS" \
    -e FFMPEG_SRC="$FFMPEG_SRC" -e FFMPEG_DIR="$FFMPEG_DIR" -e DECK_ARCH="$DECK_ARCH" \
    "$IMAGE" bash -euo pipefail -c '
export CC=gcc-14 CXX=g++-14
if [ ! -f "$FFMPEG_DIR/lib/libavcodec.a" ]; then
    if [ -f "$FFMPEG_SRC/configure" ]; then
        echo "== FFmpeg (vp6 only) -> $FFMPEG_DIR"
        # A private build tree: the source mount is read-only.
        PREFIX="$FFMPEG_DIR" FFMPEG_SRC="$FFMPEG_SRC" \
            bash "$REPO/tools/build_ffmpeg_vp6.sh" linux
    else
        echo "warning: no FFmpeg source in $FFMPEG_SRC -- movies use the lifted VP6 decoder" >&2
    fi
fi
FF=-DNFSU2_FFMPEG_DIR=
[ -f "$FFMPEG_DIR/lib/libavcodec.a" ] && FF=-DNFSU2_FFMPEG_DIR="$FFMPEG_DIR"
echo "== configure ($BUILD)"
cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="$DECK_ARCH -ffp-contract=off" \
    -DCMAKE_CXX_FLAGS="$DECK_ARCH -ffp-contract=off" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--as-needed" \
    -DXBOXRECOMP_DIR="$TK" -DNFSU2_GEN_DIR="$GEN" \
    -DNFSU2_VULKAN=ON -DNFSU2_GEN_OPT=-O2 "$FF" >/dev/null
echo "== build (-j$JOBS)"
cmake --build "$BUILD" -j"$JOBS"
'

BIN="$BUILD/nfsu2_recomp"
[ -x "$BIN" ] || fail "no binary at $BIN"

echo "== stage $OUT/nfsu2x"
rm -rf "$OUT/nfsu2x"
mkdir -p "$OUT/nfsu2x/game"
objcopy --strip-debug "$BIN" "$OUT/nfsu2x/nfsu2_recomp"
install -m 755 "$REPO/deck/nfsu2x.sh" "$OUT/nfsu2x/nfsu2x.sh"
install -m 755 "$REPO/deck/nfsu2x-install.sh" "$OUT/nfsu2x/nfsu2x-install.sh"
# The icon Steam shows (through nfsu2x-install.sh's .desktop entry).
# nfsu2x.bmp is the window's own icon (SDL_LoadBMP: no PNG in SDL2).
if command -v convert >/dev/null; then
    convert "$REPO/assets/icon.jpg" "$OUT/nfsu2x/nfsu2x.png"
    convert "$REPO/assets/icon.jpg" -resize 128x128 BMP3:"$OUT/nfsu2x/nfsu2x.bmp"
else
    ffmpeg -loglevel error -y -i "$REPO/assets/icon.jpg" "$OUT/nfsu2x/nfsu2x.png"
    ffmpeg -loglevel error -y -i "$REPO/assets/icon.jpg" -vf scale=128:128 \
        -pix_fmt bgr24 "$OUT/nfsu2x/nfsu2x.bmp"
fi
install -m 644 "$REPO/deck/nfsu2x_env.txt" "$OUT/nfsu2x/nfsu2x_env.txt"
install -m 644 "$REPO/deck/README.txt" "$OUT/nfsu2x/README.txt"
echo "$VERSION" > "$OUT/nfsu2x/VERSION"
if [ -n "${NFSU2_GAME_SRC:-}" ]; then
    [ -f "$NFSU2_GAME_SRC/default.xbe" ] || fail "no extracted disc in $NFSU2_GAME_SRC"
    cp -r "$NFSU2_GAME_SRC/." "$OUT/nfsu2x/game/"
fi

# What the binary still wants from the system, and the newest glibc symbol.
echo "== needs: $(objdump -p "$OUT/nfsu2x/nfsu2_recomp" | awk '/NEEDED/ {printf "%s ", $2}')"
echo "== newest GLIBC: $(objdump -T "$OUT/nfsu2x/nfsu2_recomp" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"

if [ -z "${NFSU2_GAME_SRC:-}" ]; then
    tar -C "$OUT" -czf "$OUT/nfsu2x-deck-$VERSION.tar.gz" nfsu2x
    echo "== $OUT/nfsu2x-deck-$VERSION.tar.gz"
fi
