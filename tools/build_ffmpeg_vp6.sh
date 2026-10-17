#!/usr/bin/env bash
# Build the minimal FFmpeg the movie decoder links (src/movie_vp6.c):
# libavcodec + libavutil with only the vp6 decoder. LGPL 2.1 -- no GPL parts
# (devkitPro's switch-ffmpeg is built with --enable-gpl; don't link that).
#
#   tools/build_ffmpeg_vp6.sh switch|linux|android
#
# Environment:
#   FFMPEG_SRC   FFmpeg source (default /root/nfsu2x/ref/ffmpeg-7.1,
#                git clone -b release/7.1 https://github.com/FFmpeg/FFmpeg.git)
#   PREFIX       install prefix (default /root/nfsu2x/ffmpeg-vp6-<target>)
#   DEVKITPRO    default /opt/devkitpro
#   ANDROID_NDK_HOME  (android) NDK root; ANDROID_API default 29
set -euo pipefail

TARGET="${1:-switch}"
SRC="${FFMPEG_SRC:-/root/nfsu2x/ref/ffmpeg-7.1}"
PREFIX="${PREFIX:-/root/nfsu2x/ffmpeg-vp6-$TARGET}"
BUILD="$PREFIX-build"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

COMMON=(--prefix="$PREFIX" --disable-everything --enable-decoder=vp6
        --disable-avformat --disable-avdevice --disable-avfilter --disable-swscale
        --disable-swresample --disable-programs --disable-doc --disable-network
        --disable-pthreads --disable-debug --disable-autodetect
        --enable-static --disable-shared --enable-pic)

mkdir -p "$BUILD"
cd "$BUILD"
case "$TARGET" in
switch)
    export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
    "$SRC/configure" "${COMMON[@]}" --enable-cross-compile --cross-prefix=aarch64-none-elf- \
        --arch=aarch64 --cpu=cortex-a57 --target-os=none --enable-neon \
        --disable-runtime-cpudetect \
        --extra-cflags="-D__SWITCH__ -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -I$DEVKITPRO/libnx/include" \
        --extra-ldflags="-specs=$DEVKITPRO/libnx/switch.specs -fPIE -L$DEVKITPRO/libnx/lib" \
        --extra-libs=-lnx >configure.log
    ;;
linux)
    "$SRC/configure" "${COMMON[@]}" --disable-x86asm >configure.log
    ;;
android)
    NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}"
    API="${ANDROID_API:-29}"
    TC="$NDK/toolchains/llvm/prebuilt/$(uname -s | tr A-Z a-z)-x86_64"
    "$SRC/configure" "${COMMON[@]}" --enable-cross-compile --target-os=android \
        --arch=aarch64 --cpu=armv8-a --enable-neon \
        --cc="$TC/bin/aarch64-linux-android$API-clang" \
        --cross-prefix="$TC/bin/llvm-" --ar="$TC/bin/llvm-ar" --nm="$TC/bin/llvm-nm" \
        --ranlib="$TC/bin/llvm-ranlib" --strip="$TC/bin/llvm-strip" \
        --extra-cflags="-O2 -fPIC" >configure.log
    ;;
*)
    echo "usage: $0 switch|linux|android" >&2; exit 1 ;;
esac
grep -E '^License' configure.log
make -j"$(nproc)" >make.log
make install >install.log
echo "installed $PREFIX (pass -DNFSU2_FFMPEG_DIR=$PREFIX)"
