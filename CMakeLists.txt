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

set(COMMON_SOURCES
  src/main.cpp
  src/app/app.cpp
  src/audio/mixer.cpp
  src/audio/synth.cpp
  src/core/crash.cpp
  src/core/fsprobe.cpp
  src/core/log.cpp
  src/core/netlog.cpp
  src/core/thread.cpp
  src/render/batch.cpp
  src/render/font.cpp
  src/render/procedural.cpp
  third_party/font8x8/font8x8.c
)

add_executable(rhythm-ps5 ${COMMON_SOURCES} src/platform/platform_sdl2.cpp)
target_include_directories(rhythm-ps5 PRIVATE src third_party/font8x8)
target_compile_definitions(rhythm-ps5 PRIVATE RHYTHM_REFRESH_HZ=60)
target_compile_options(rhythm-ps5 PRIVATE -Wall -Wextra -fno-omit-frame-pointer)
target_link_libraries(rhythm-ps5 PRIVATE PkgConfig::SDL2 OpenGL::GL Threads::Threads)
