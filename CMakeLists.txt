# SPDX-License-Identifier: GPL-3.0-or-later
# Build PC (Linux, SDL2 + OpenGL 3.3). La build PS5 passe par ps5/build.sh.
cmake_minimum_required(VERSION 3.16)
project(rhythm_ps5 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER COMMON_SOURCES EXCLUDE REGEX "platform_(ps5|sdl2)\\.cpp$")
file(GLOB MINIZ_SOURCES third_party/miniz/*.c)
list(APPEND COMMON_SOURCES third_party/font8x8/font8x8.c third_party/impl.c ${MINIZ_SOURCES})

add_executable(rhythm-ps5 ${COMMON_SOURCES} src/platform/platform_sdl2.cpp)
target_include_directories(rhythm-ps5 PRIVATE src third_party third_party/font8x8)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
target_compile_options(rhythm-ps5 PRIVATE -Wall -Wextra -fno-omit-frame-pointer)
target_compile_definitions(rhythm-ps5 PRIVATE RHYTHM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/ps5/assets")
target_link_libraries(rhythm-ps5 PRIVATE PkgConfig::SDL2 PkgConfig::CURL OpenGL::GL Threads::Threads)

# Test du lecteur de beatmaps : ./build/pc/beatmap-test fichier.osu...
add_executable(beatmap-test tests/beatmap_test.cpp src/game/beatmap.cpp src/core/fsutil.cpp src/core/log.cpp
  src/platform/platform_sdl2.cpp)
target_include_directories(beatmap-test PRIVATE src third_party)
target_compile_definitions(beatmap-test PRIVATE RHYTHM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/ps5/assets")
target_link_libraries(beatmap-test PRIVATE PkgConfig::SDL2 OpenGL::GL Threads::Threads)
