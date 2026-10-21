cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Need for Speed: Underground 2 (Xbox, US/PAL-EU) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL) and Nintendo
# Switch homebrew (devkitA64 + libnx, built with switch/build.sh).
# ============================================================================

project(nfsu2_recomp C)

# Both regions use the same runtime, renderers, platform layer and decoder.
set(NFSU2_REGION "US" CACHE STRING "Translated Xbox executable region: US or EU")
set_property(CACHE NFSU2_REGION PROPERTY STRINGS US EU)
if(NOT NFSU2_REGION MATCHES "^(US|EU)$")
    message(FATAL_ERROR "NFSU2_REGION must be US or EU; one translation is linked per NRO")
endif()

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
if(NFSU2_REGION STREQUAL "EU")
    set(NFSU2_EXPECTED_ENTRY "0021B77E")
else()
    set(NFSU2_EXPECTED_ENTRY "0021B1CE")
endif()
file(STRINGS "${NFSU2_GEN_DIR}/recomp_dispatch.c" NFSU2_ENTRY_BINDING
     REGEX "0x${NFSU2_EXPECTED_ENTRY}[uU]?,.*xbe_entry_point")
if(NOT NFSU2_ENTRY_BINDING)
    message(FATAL_ERROR "Translation does not match NFSU2_REGION=${NFSU2_REGION}; regenerate the matching XBE")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds nfsu2x-vulkan.nro.
option(NFSU2_PACKED_REGISTERS "Use compatible generated scalar TLS storage" OFF)
if(NFSU2_PACKED_REGISTERS)
    if(NOT EXISTS "${NFSU2_GEN_DIR}/recomp_registers.h")
        message(FATAL_ERROR "Packed storage requires a matching generated register header")
    endif()
    file(READ "${NFSU2_GEN_DIR}/recomp_registers.h" generated_register_header)
    file(READ "${XBOXRECOMP_DIR}/src/kernel/recomp_registers.h" runtime_register_header)
    string(REPLACE "\r\n" "\n" generated_register_header "${generated_register_header}")
    string(REPLACE "\r\n" "\n" runtime_register_header "${runtime_register_header}")
    if(NOT generated_register_header STREQUAL runtime_register_header)
        message(FATAL_ERROR "Generated and runtime packed-register headers differ; keep this option OFF")
    endif()
    add_compile_definitions(NFSU2_PACKED_REGISTERS=1)
endif()
option(NFSU2_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${NFSU2_VULKAN} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${NFSU2_GEN_DIR}/*.c")

set(GAME_SOURCES
    src/main.c
    src/game_region.c
    src/frame_rate.c
    src/switch_nx.c
    src/switch_heap.c
    src/switch_saltynx.c
    src/switch_diagnostics.c
    src/text_patch.c
    src/movie_crop.c
src/nic.c
)

if(NFSU2_REGION STREQUAL "EU")
    set(NFSU2_REGION_SOURCES
        src/regions/eu/recomp_manual.c
        src/regions/eu/eu_debug.c
        src/regions/eu/eu_native_matrix.c
        src/regions/eu/eu_options.c
        src/regions/eu/eu_launch_guard.c
        src/regions/eu/eu_port_io.c
        src/regions/eu/eu_cache.c
        src/regions/eu/eu_interrupt.c
    )
    # Only the old private test translation needs this pass-through bridge.
    file(GLOB NFSU2_TRANSLATION_CHUNKS "${NFSU2_GEN_DIR}/recomp_*.c")
    set(NFSU2_EU_NATIVE_ROW OFF)
    foreach(chunk IN LISTS NFSU2_TRANSLATION_CHUNKS)
        file(STRINGS "${chunk}" chooser_definition REGEX "^void sub_000DCFD0_gen[(]void[)]")
        if(chooser_definition)
            list(APPEND NFSU2_REGION_SOURCES src/regions/eu/eu_generation_bridge.c)
        endif()
        file(STRINGS "${chunk}" row_definition REGEX "^void sub_0025F254_gen[(]void[)]")
        if(row_definition)
            list(APPEND NFSU2_REGION_SOURCES src/regions/eu/eu_movie_row.c)
            set(NFSU2_EU_NATIVE_ROW ON)
        endif()
    endforeach()
else()
    set(NFSU2_REGION_SOURCES src/recomp_manual.c)
endif()
list(APPEND GAME_SOURCES ${NFSU2_REGION_SOURCES})

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
if(NFSU2_REGION STREQUAL "EU")
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_BUILD_EU=1 NFSU2_EU_DEBUG=1)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/regions/eu)
    if(NFSU2_EU_NATIVE_ROW)
        target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_NATIVE_MOVIE_ROW=1)
    endif()
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/regions/eu/eu_debug.h")
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/regions/eu/eu_debug.h")
    endif()
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_BUILD_US=1)
endif()

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
    if(NFSU2_REGION STREQUAL "EU")
        target_sources(${PROJECT_NAME} PRIVATE src/regions/eu/eu_movie_vp6.c)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_NATIVE_VP6=1)
    # BEFORE: the Switch portlibs include dir has devkitPro's (GPL) FFmpeg headers.
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE "${NFSU2_FFMPEG_DIR}/include")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${NFSU2_FFMPEG_DIR}/lib/libavcodec.a" "${NFSU2_FFMPEG_DIR}/lib/libavutil.a")
endif()

if(NFSU2_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_VULKAN=1)
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
    set(NFSU2_SWITCH_APP_NAME "Need for Speed Underground 2" CACHE STRING
        "Title shown in the homebrew menu")
    set(NFSU2_SWITCH_AUTHOR "nfsu2-xbox" CACHE STRING "Author shown in the homebrew menu")
    # The release lives here, not in the cache: a cached default stays at
    # whatever the build directory was first configured with (the 0.4.5 NRO
    # said 0.4.3). -DNFSU2_SWITCH_VERSION_OVERRIDE=x sets another one.
    set(NFSU2_SWITCH_VERSION "0.5.13")
    if(NFSU2_SWITCH_VERSION_OVERRIDE)
        set(NFSU2_SWITCH_VERSION "${NFSU2_SWITCH_VERSION_OVERRIDE}")
    elseif(NFSU2_REGION STREQUAL "EU")
        set(NFSU2_SWITCH_VERSION "EU v${NFSU2_SWITCH_VERSION}")
    else()
        string(APPEND NFSU2_SWITCH_VERSION " ${NFSU2_REGION}")
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
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--wrap=abort" "LINKER:--wrap=exit"
        "LINKER:--wrap=_exit" "LINKER:--wrap=__libnx_exit"
        "LINKER:--wrap=fatalThrow")
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
