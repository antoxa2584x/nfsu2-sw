cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Need for Speed: Underground 2 (Xbox, NTSC-U) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL), Nintendo Switch
# homebrew (devkitA64 + libnx, built with switch/build.sh) and Android
# (NDK + SDL2 + OpenGL ES 3 or Vulkan, built by Gradle from android/).
# ============================================================================

project(nfsu2_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# The lifted code is ~100 MB of C generated from the retail XBE. It is never
# committed; point this at wherever tools.recomp --gen-dir wrote it.
set(NFSU2_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen" CACHE PATH
    "Directory holding the tools.recomp output (recomp_*.c, recomp_funcs.h)")

if(NOT EXISTS "${NFSU2_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${NFSU2_GEN_DIR}. Run tools.recomp "
                        "with --gen-dir, or pass -DNFSU2_GEN_DIR=<dir>.")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds nfsu2x-vulkan.nro. On
# Android it is built in addition to OpenGL ES and chosen at run time.
option(NFSU2_VULKAN "Render with Vulkan instead of OpenGL" OFF)

# ── Android: SDL2 is built in-tree; Vulkan only (OpenGL ES removed) ──
if(ANDROID)
    set(NFSU2_VULKAN ON CACHE BOOL "" FORCE)
    set(NFSU2_SDL2_DIR "" CACHE PATH "SDL2 source tree (android/tools/fetch_deps.sh)")
    if(NOT EXISTS "${NFSU2_SDL2_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "NFSU2_SDL2_DIR must point at an SDL2 source tree")
    endif()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${NFSU2_SDL2_DIR} ${CMAKE_BINARY_DIR}/SDL2)
endif()

set(XBOXRECOMP_VULKAN ${NFSU2_VULKAN} CACHE BOOL "" FORCE)

# libadrenotools: loads a custom Vulkan driver (Turnip .zip) for the Vulkan
# renderer. Its hook libraries are shared libraries of this project, so they
# end up in the APK (which must then be packaged with useLegacyPackaging).
if(ANDROID)
    set(NFSU2_ADRENOTOOLS_DIR "" CACHE PATH "libadrenotools source tree (android/tools/fetch_deps.sh)")
    if(NFSU2_ADRENOTOOLS_DIR AND EXISTS "${NFSU2_ADRENOTOOLS_DIR}/CMakeLists.txt")
        add_subdirectory(${NFSU2_ADRENOTOOLS_DIR} ${CMAKE_BINARY_DIR}/adrenotools)
    endif()
endif()

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${NFSU2_GEN_DIR}/*.c")

set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/switch_nx.c
    src/switch_saltynx.c
    src/text_patch.c
    src/movie_crop.c
src/nic.c
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
elseif(ANDROID)
    # SDLActivity loads libmain.so and calls its SDL_main (src/main.c).
    add_library(${PROJECT_NAME} SHARED ${GAME_SOURCES} src/android_host.c ${RECOMP_GEN_SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME main)
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# Movies: VP6 decoded by FFmpeg instead of the lifted On2 decoder
# (src/movie_vp6.c). NFSU2_FFMPEG_DIR is an install of the minimal LGPL
# build from tools/build_ffmpeg_vp6.sh (libavcodec with only the vp6
# decoder); without it the lifted decoder plays the movies.
set(NFSU2_FFMPEG_DIR "" CACHE PATH "FFmpeg (vp6 decoder) install prefix")
if(NFSU2_FFMPEG_DIR)
    if(NOT EXISTS "${NFSU2_FFMPEG_DIR}/lib/libavcodec.a")
        message(FATAL_ERROR "no libavcodec.a in ${NFSU2_FFMPEG_DIR}/lib (tools/build_ffmpeg_vp6.sh)")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE src/movie_vp6.c)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_NATIVE_VP6=1)
    # BEFORE: the Switch portlibs include dir has devkitPro's (GPL) FFmpeg headers.
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE "${NFSU2_FFMPEG_DIR}/include")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${NFSU2_FFMPEG_DIR}/lib/libavcodec.a" "${NFSU2_FFMPEG_DIR}/lib/libavutil.a")
endif()

if(NFSU2_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_VULKAN=1)
endif()
if(ANDROID AND TARGET adrenotools)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_ADRENOTOOLS=1)
endif()
if(ANDROID AND (NFSU2_VULKAN OR TARGET adrenotools))
    # glslang and libadrenotools are C++: link with the C++ driver so libc++
    # (static, ANDROID_STL=c++_static) comes along.
    enable_language(CXX)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${NFSU2_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11 dxgi dxguid xinput winmm dbghelp)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
elseif(ANDROID)
    # Clang (NDK). Same reasoning as the other hosts: the lifted chunks are
    # huge straight-line functions; -O2 as on the Switch (the ISA is the same),
    # NFSU2_GEN_OPT=-O1 if clang runs out of memory on a chunk.
    # aarch64 is the Switch's ISA, so the unaligned-atomic fallbacks apply.
    set(NFSU2_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    set(NFSU2_ANDROID_TLS_MODEL "" CACHE STRING
        "-ftls-model for the lifted code (guest registers are thread-local); try initial-exec")
    set(NFSU2_GEN_FLAGS "${NFSU2_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
    if(NFSU2_ANDROID_TLS_MODEL)
        list(APPEND NFSU2_GEN_FLAGS "-ftls-model=${NFSU2_ANDROID_TLS_MODEL}")
    endif()
    option(NFSU2_LTO "ThinLTO of the lifted code" OFF)
    if(NFSU2_LTO)
        list(APPEND NFSU2_GEN_FLAGS "-flto=thin")
        target_link_options(${PROJECT_NAME} PRIVATE "-flto=thin")
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${NFSU2_GEN_FLAGS}")
    # Devices with 16 KB pages (Android 15+) need 16 KB aligned segments.
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 log android dl m)
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch's CPU is the bottleneck there, so it gets -O2.
    if(NINTENDO_SWITCH)
        set(NFSU2_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    else()
        set(NFSU2_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
    endif()
    # NFSU2_LTO: link-time optimisation of the lifted code and its manual
    # overrides, so lifted callers inline each other and the native leaves
    # (_ftol2, ...). The runtime and renderers are compiled as usual.
    # NFSU2_LTO_JOBS parallel LTRANS jobs (memory: ~1-2 GB each).
    option(NFSU2_LTO "Link-time optimisation of the lifted code" OFF)
    set(NFSU2_LTO_JOBS "4" CACHE STRING "Parallel LTO link jobs")
    set(NFSU2_GEN_FLAGS "${NFSU2_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
    if(NFSU2_LTO)
        list(APPEND NFSU2_GEN_FLAGS "-flto=auto")
        set_property(SOURCE src/recomp_manual.c APPEND PROPERTY
            COMPILE_OPTIONS "-flto=auto;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE
            "-flto=${NFSU2_LTO_JOBS}" "-flto-partition=balanced" ${NFSU2_GEN_OPT})
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${NFSU2_GEN_FLAGS}")
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── Nintendo Switch homebrew (devkitA64 + libnx) ────────────────
#
# Build with switch/build.sh. The NRO reads the extracted disc from
# sdmc:/switch/nfsu2x/game/ (never an ISO); logs and nfsu2x_env.txt live in
# sdmc:/switch/nfsu2x/.
if(NINTENDO_SWITCH)
    set(NFSU2_SWITCH_APP_NAME "NFS Underground 2 (Xbox recomp)" CACHE STRING
        "Title shown in the homebrew menu")
    set(NFSU2_SWITCH_AUTHOR "nfsu2-xbox" CACHE STRING "Author shown in the homebrew menu")
    # The release lives here, not in the cache: a cached default stays at
    # whatever the build directory was first configured with (the 0.4.5 NRO
    # said 0.4.3). -DNFSU2_SWITCH_VERSION_OVERRIDE=x sets another one.
    set(NFSU2_SWITCH_VERSION "0.5.10")
    if(NFSU2_SWITCH_VERSION_OVERRIDE)
        set(NFSU2_SWITCH_VERSION "${NFSU2_SWITCH_VERSION_OVERRIDE}")
    endif()
    set(NFSU2_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg" CACHE FILEPATH
        "256x256 JPEG icon (libnx default when empty)")

    if(NFSU2_VULKAN)
        # No GL: SDL is only the audio output; NVK comes with nv2a_vk.
        target_link_libraries(${PROJECT_NAME} PRIVATE nx m)
    else()
        # switch-sdl2's GL path is EGL on switch-mesa.
        target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau nx m)
    endif()
    # SDL's audio thread gets a real-time priority and audren gaps are
    # counted (switch_nx.c, "SDL's audio thread").
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--wrap=SDL_SYS_SetThreadPriority"
        "LINKER:--wrap=audrvVoiceAddWaveBuf")
    set_property(SOURCE src/switch_nx.c APPEND PROPERTY
        INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    # switch_nx.c's loading screen asks EGL for the on-screen size.
    target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{DEVKITPRO}/portlibs/switch/include")

    set(NFSU2_NACP "${CMAKE_CURRENT_BINARY_DIR}/nfsu2x.nacp")
    nx_generate_nacp(OUTPUT "${NFSU2_NACP}"
        NAME "${NFSU2_SWITCH_APP_NAME}"
        AUTHOR "${NFSU2_SWITCH_AUTHOR}"
        VERSION "${NFSU2_SWITCH_VERSION}")
    if(NFSU2_SWITCH_ICON)
        nx_create_nro(${PROJECT_NAME} NACP "${NFSU2_NACP}" ICON "${NFSU2_SWITCH_ICON}")
    else()
        nx_create_nro(${PROJECT_NAME} NACP "${NFSU2_NACP}")
    endif()
endif()
