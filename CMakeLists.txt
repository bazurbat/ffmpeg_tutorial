cmake_minimum_required(VERSION 2.8)

project(ffmpeg_tutorial)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS "-std=c99 -Wall -fdiagnostics-show-option")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec)

include_directories(
    ${GLIB_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

add_executable(tutorial01 tutorial01.c)

target_link_libraries(tutorial01
    ${GLIB_LDFLAGS}
    ${FFMPEG_LDFLAGS}
)
