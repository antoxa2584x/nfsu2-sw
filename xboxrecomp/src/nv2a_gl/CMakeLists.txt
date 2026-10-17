cmake_minimum_required(VERSION 3.20)
project(nv2a_gl C)

# OpenGL 3.3 core (desktop) / OpenGL ES 3.x (Android) renderer for the pushbuffer executor. SDL2 provides the
# window, the context and the entry points (gl_api.c resolves them itself),
# so there is no GL loader dependency and the same code builds for the
# Switch's Mesa.
add_library(xbox_nv2a_gl STATIC
    nv2a_gl.c
    gl_api.c
    gl_vsh.c
    gl_psh.c
)
target_include_directories(xbox_nv2a_gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NINTENDO_SWITCH OR ANDROID)
    # Android: SDL2 is built in-tree by the top-level project and the context
    # is OpenGL ES 3 (nv2a_gl.c picks it up at run time).
    if(NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    endif()
    target_link_libraries(xbox_nv2a_gl PUBLIC SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(xbox_nv2a_gl PUBLIC PkgConfig::SDL2)
endif()
if(NOT MSVC)
    target_compile_options(xbox_nv2a_gl PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
endif()
