cmake_minimum_required(VERSION 3.20)
project(metal_bindings C OBJC)
set(CMAKE_C_STANDARD 11)
add_executable(metal_bindings test_main.m
    ../../xboxrecomp/src/nv2a_metal/nv2a_metal_msl.c
    ../../xboxrecomp/src/nv2a_gl/gl_psh.c ../../xboxrecomp/src/nv2a_gl/gl_vsh.c)
set_source_files_properties(test_main.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_compile_options(metal_bindings PRIVATE -UNDEBUG -Wall -Wextra -Wno-unused-parameter
    -Wno-missing-field-initializers -Wno-unguarded-availability-new -Wno-unguarded-availability)
target_link_libraries(metal_bindings PRIVATE "-framework Metal" "-framework Foundation" "-framework QuartzCore")
enable_testing()
add_test(NAME metal_bindings COMMAND metal_bindings)
