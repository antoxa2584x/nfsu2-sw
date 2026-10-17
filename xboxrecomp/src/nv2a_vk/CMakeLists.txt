cmake_minimum_required(VERSION 3.20)
project(nv2a_vk C)

# Vulkan 1.3 renderer for the pushbuffer executor (XBOXRECOMP_VULKAN=ON):
# the Switch's NVK from mesa-switch, linked statically (NVK_SDK), or any
# Vulkan driver on Linux. Shares the shader generators with nv2a_gl, in
# their Vulkan dialect, and compiles them with glslang at run time.
if(ANDROID)
    # Built next to nv2a_gl in one library: the shader generators live there
    # (one copy), and the few names both renderers define are renamed here.
    add_library(xbox_nv2a_vk STATIC nv2a_vk.c)
    target_link_libraries(xbox_nv2a_vk PUBLIC xbox_nv2a_gl)
    target_compile_definitions(xbox_nv2a_vk PRIVATE
        nv2a_gl_texture_bytes=nv2a_vk_texture_bytes
        nv2a_gl_frame_count=nv2a_vk_frame_count
        nv2a_gl_program_count=nv2a_vk_program_count
        nv2a_gl_queue_stats=nv2a_vk_queue_stats)
    # libadrenotools (a custom Turnip driver from a .zip), when the top-level
    # project added it (android/tools/fetch_deps.sh).
    if(TARGET adrenotools)
        target_link_libraries(xbox_nv2a_vk PUBLIC adrenotools)
        target_compile_definitions(xbox_nv2a_vk PRIVATE NFSU2_ADRENOTOOLS=1)
    endif()
else()
    add_library(xbox_nv2a_vk STATIC
        nv2a_vk.c
        ../nv2a_gl/gl_vsh.c
        ../nv2a_gl/gl_psh.c
    )
endif()
target_include_directories(xbox_nv2a_vk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(glslang CONFIG REQUIRED)
target_link_libraries(xbox_nv2a_vk PUBLIC glslang::glslang glslang::glslang-default-resource-limits)

if(NINTENDO_SWITCH)
    set(NVK_SDK "" CACHE PATH "mesa-switch install prefix (holds lib/libvulkan.a, include/vulkan)")
    if(NOT EXISTS "${NVK_SDK}/lib/libvulkan.a")
        message(FATAL_ERROR "NVK_SDK must point at a mesa-switch install (no lib/libvulkan.a in '${NVK_SDK}')")
    endif()
    target_include_directories(xbox_nv2a_vk PUBLIC "${NVK_SDK}/include")
    target_sources(xbox_nv2a_vk PRIVATE switch_elf_stubs.c switch_egl_stubs.c)
    # Static NVK and what it needs; listed twice for archive rescanning.
    target_link_libraries(xbox_nv2a_vk PUBLIC
        "${NVK_SDK}/lib/libvulkan.a" expat z nx
        "${NVK_SDK}/lib/libvulkan.a" expat z nx stdc++ m)
elseif(ANDROID)
    # NDK's libvulkan.so (headers come with the sysroot) and SDL2 built in-tree
    # by the top-level project. glslang/SPIRV-Tools are the Android build from
    # android/tools/build_glslang.sh (-DCMAKE_PREFIX_PATH=<its prefix>).
    find_package(Vulkan REQUIRED)
    if(NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    endif()
    target_link_libraries(xbox_nv2a_vk PUBLIC Vulkan::Vulkan SDL2::SDL2 log)
    # Present when glslang was built with ENABLE_OPT=ON (the default there).
    find_library(SPVT_OPT SPIRV-Tools-opt PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES lib NO_DEFAULT_PATH)
    find_library(SPVT SPIRV-Tools PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES lib NO_DEFAULT_PATH)
    if(SPVT_OPT AND SPVT)
        target_link_libraries(xbox_nv2a_vk PUBLIC ${SPVT_OPT} ${SPVT})
    endif()
    # libc++ comes from -DANDROID_STL=c++_static (android/app/build.gradle).
else()
    find_package(Vulkan REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    # The distribution glslang is built with its optimizer (static SPIRV-Tools).
    target_link_libraries(xbox_nv2a_vk PUBLIC Vulkan::Vulkan PkgConfig::SDL2
        SPIRV-Tools-opt SPIRV-Tools stdc++)
endif()
if(NOT MSVC)
    target_compile_options(xbox_nv2a_vk PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -O2)
endif()
