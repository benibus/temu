cmake_minimum_required(VERSION 3.7)

project(temu LANGUAGES C)

set(SOURCES
    src/fonts.c
    src/fsm.c
    src/keys.c
    src/main.c
    src/opengl.c
    src/pty.c
    src/renderer.c
    src/ring.c
    src/terminal.c
    src/utf8.c
    src/utils.c
    src/window.c
    src/cells.h
    src/config.h
    src/defs.h
    src/fonts.h
    src/fsm.h
    src/keymap.h
    src/opengl.h
    src/pty.h
    src/renderer.h
    src/ring.h
    src/terminal.h
    src/window.h
    src/utf8.h
    src/utils.h
)

# if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.11.0")
#     file(GLOB SOURCES CONFIGURE_DEPENDS src/*.c src/*.h)
# else()
#     file(GLOB SOURCES src/*.c src/*.h)
# endif()

add_executable(${PROJECT_NAME} ${SOURCES})

find_package(Freetype 2.0 REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL)
# find_package(X11 REQUIRED COMPONENTS Xrender)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Defaulting to debug build")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Default build type" FORCE)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES C_EXTENSIONS OFF)
set_target_properties(${PROJECT_NAME} PROPERTIES C_STANDARD 11)

target_compile_definitions(${PROJECT_NAME} PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_definitions(${PROJECT_NAME} PRIVATE _XOPEN_SOURCE=600)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    $<$<CONFIG:Debug>:BUILD_DEBUG=1>
    $<$<CONFIG:Release>:BUILD_RELEASE=1>
)
target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-no-pie>
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Wno-unused-variable>
    $<$<CONFIG:Debug>:-Wno-unused-function>
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    Freetype::Freetype
    Fontconfig::Fontconfig
    X11::X11
    OpenGL::EGL
    # X11::Xrender
)
target_link_libraries(${PROJECT_NAME}
    Freetype::Freetype
    Fontconfig::Fontconfig
    X11::X11
    OpenGL::EGL
    util m
    # X11::Xrender
)

