# Arbitrarily chosen based on https://pkgs.org/download/cmake
cmake_minimum_required(VERSION 3.7.2)

project(temu LANGUAGES C)

find_package(Freetype 2.0 REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Default build type" FORCE)
endif()

set(BIN ${PROJECT_NAME})
set(SOURCES
    src/fonts.c
    src/gfx_context.c
    src/gfx_renderer.c
    src/main.c
    src/opengl.c
    src/pty.c
    src/term.c
    src/term_fsm.c
    src/term_keyboard.c
    src/term_opcodes.c
    src/term_parser.c
    src/term_ring.c
    src/utf8.c
    src/utils.c
    src/x11_platform.c
)

add_executable(${BIN} ${SOURCES})

set_target_properties(${BIN} PROPERTIES C_EXTENSIONS OFF)
set_target_properties(${BIN} PROPERTIES C_STANDARD 11)

target_compile_definitions(${BIN} PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_definitions(${BIN} PRIVATE _XOPEN_SOURCE=600)
target_compile_definitions(${BIN}
    PRIVATE
    $<$<CONFIG:Debug>:BUILD_DEBUG=1>
    $<$<CONFIG:Release>:BUILD_RELEASE=1>
)
if(DEFINED GLES_VERSION)
    message(STATUS "Overriding default GLES_VERSION to ${GLES_VERSION}")
    target_compile_definitions(${BIN} PRIVATE GLES_VERSION=${GLES_VERSION})
endif()

target_compile_options(${BIN}
    PRIVATE
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-O0>
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Wno-unused-variable>
    $<$<CONFIG:Debug>:-Wno-unused-function>
)

target_include_directories(${BIN}
    PRIVATE
    Freetype::Freetype
    Fontconfig::Fontconfig
    X11::X11
    OpenGL::EGL
)
target_link_libraries(${BIN}
    Freetype::Freetype
    Fontconfig::Fontconfig
    X11::X11
    OpenGL::EGL
    util m
)

