#!/usr/bin/env bash
# glslang for Android (arm64-v8a): the Vulkan renderer compiles the NV2A
# shaders it generates (GLSL) to SPIR-V at run time. Installs to
# android/deps/glslang-android (the Gradle build finds it there).
#
#   ANDROID_NDK_HOME=/path/to/ndk android/tools/build_glslang.sh
#
# The SPIR-V optimizer is left out (ENABLE_OPT=OFF): the renderer only needs
# the front end, and it saves building SPIRV-Tools. Use GLSLANG_OPT=ON for it.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}"
TAG="${GLSLANG_TAG:-15.4.0}"
SRC="$HERE/deps/glslang-src"
PREFIX="${PREFIX:-$HERE/deps/glslang-android}"
OPT="${GLSLANG_OPT:-OFF}"

mkdir -p "$HERE/deps"
[ -d "$SRC/.git" ] || git clone --depth 1 --branch "$TAG" https://github.com/KhronosGroup/glslang "$SRC"
if [ "$OPT" = "ON" ] && [ ! -d "$SRC/External/spirv-tools" ]; then
    (cd "$SRC" && python3 update_glslang_sources.py)
fi

cmake -S "$SRC" -B "$SRC/build-android" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_EXTERNAL=$OPT -DENABLE_OPT=$OPT \
    -DENABLE_HLSL=OFF -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_CTEST=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$SRC/build-android"
cmake --install "$SRC/build-android"
echo "glslang installed in $PREFIX"
