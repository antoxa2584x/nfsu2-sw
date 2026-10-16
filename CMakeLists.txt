cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Need for Speed: Underground 2 (Xbox, NTSC-U) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL) and Nintendo
# Switch homebrew (devkitA64 + libnx, built with switch/build.sh).
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
# links mesa-switch's static NVK (NVK_SDK) and builds nfsu2x-vulkan.nro.
option(NFSU2_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${NFSU2_VULKAN} CACHE BOOL "" FORCE)
# Apple: the native Metal renderer too, the default at run time;
# NFSU2_RENDERER=vulkan picks MoltenVK instead (src/apple/apple_main.c).
if(APPLE AND NFSU2_VULKAN)
    option(NFSU2_METAL "Also build the Metal renderer" ON)
else()
    set(NFSU2_METAL OFF)
endif()
set(XBOXRECOMP_METAL ${NFSU2_METAL} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${NFSU2_GEN_DIR}/*.c")

set(NFSU2_MANUAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp_manual.c" CACHE FILEPATH
    "Manual overrides matching the translated executable")
set(NFSU2_REGION "NTSC-U" CACHE STRING "Translated executable profile: NTSC-U or PAL")
if(NFSU2_REGION STREQUAL "PAL")
    if(NFSU2_MANUAL_SOURCE STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp_manual.c")
        message(FATAL_ERROR "PAL requires tools/regen_pal.sh and its NFSU2_MANUAL_SOURCE output")
    endif()
    add_compile_definitions(NFSU2_REGION_PAL=1)
elseif(NOT NFSU2_REGION STREQUAL "NTSC-U")
    message(FATAL_ERROR "Unsupported executable profile ${NFSU2_REGION}")
endif()

set(GAME_SOURCES
    src/main.c
    ${NFSU2_MANUAL_SOURCE}
    src/switch_nx.c
    src/switch_saltynx.c
    src/text_patch.c
    src/movie_crop.c
src/nic.c
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# AppKit owns the main thread: window and events there, the title on its own
# thread (src/apple/apple_main.c).
if(APPLE)
    set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY COMPILE_OPTIONS "-ffp-contract=off")
    enable_language(OBJC)
    target_sources(${PROJECT_NAME} PRIVATE src/apple/apple_main.c src/apple/apple_game_state.c src/apple/apple_perf.m
                                           src/apple/apple_keyboard.c)
    set_source_files_properties(src/apple/apple_perf.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
                                INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Foundation")
endif()

# iOS app: SDL's UIKit glue runs apple_main.c's main as SDL_main; Vulkan via
# MoltenVK; dependencies from tools/build_ios_deps.sh on CMAKE_PREFIX_PATH.
if(IOS)
    set(_nfsu2_bundle_name "NFSU2")
    set(NFSU2_IOS_BUNDLE_ID "com.example.nfsu2x" CACHE STRING "iOS bundle identifier")
    find_package(SDL2 REQUIRED)
    enable_language(OBJC)
    target_sources(${PROJECT_NAME} PRIVATE src/ios/ios_host.c src/ios/ios_scene.m src/ios/ios_touch.m
                                           src/ios/ios_import.m src/ios/ios_xiso.c src/ios/ios_touch_layout.m
                                           src/ios/ios_touch_settings.m src/ios/ios_music.m)
    set_source_files_properties(src/ios/ios_scene.m src/ios/ios_touch.m src/ios/ios_import.m
                                src/ios/ios_touch_layout.m src/ios/ios_touch_settings.m src/ios/ios_music.m
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main "-framework UniformTypeIdentifiers" "-framework AVFoundation")
    target_sources(${PROJECT_NAME} PRIVATE ios/Assets.xcassets)
    set_source_files_properties(ios/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${NFSU2_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${NFSU2_IOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "${_nfsu2_bundle_name}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "1")
endif()

# Movies: VP6 decoded by FFmpeg instead of the lifted On2 decoder
# (src/movie_vp6.c). NFSU2_FFMPEG_DIR is an install of the minimal LGPL
# build from tools/build_ffmpeg_vp6.sh (libavcodec with only the vp6
# decoder); without it the lifted decoder plays the movies.
set(NFSU2_FFMPEG_DIR "" CACHE PATH "FFmpeg (vp6 decoder) install prefix")
if(APPLE AND NFSU2_FFMPEG_DIR AND EXISTS "${NFSU2_FFMPEG_DIR}/Frameworks/avcodec.framework")
    include(cmake/apple_ffmpeg.cmake)   # dynamic frameworks (tools/build_ffmpeg_apple.sh)
elseif(NFSU2_FFMPEG_DIR)
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
if(NFSU2_METAL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_METAL=1)
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
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch and Apple arm64 use -O2; override NFSU2_GEN_OPT to trade build
    # time and memory for optimisation on a particular machine.
    if(NINTENDO_SWITCH OR APPLE)
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
    if(NFSU2_LTO AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND NFSU2_GEN_FLAGS "-flto=thin")
        set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY
            COMPILE_OPTIONS "-flto=thin;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE "-flto=thin" ${NFSU2_GEN_OPT})
    elseif(NFSU2_LTO)
        list(APPEND NFSU2_GEN_FLAGS "-flto=auto")
        set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY
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
