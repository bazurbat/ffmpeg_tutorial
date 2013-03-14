cmake_minimum_required(VERSION 2.8)

project(ffmpeg_tutorial)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS "-std=c99 -Wall -fdiagnostics-show-option")

find_package(PkgConfig REQUIRED)
#pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec)
find_package(SDL REQUIRED)

include_directories(
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL_INCLUDE_DIR}
)

foreach(num RANGE 1 4)
    add_executable(tutorial0${num} tutorial0${num}.c packet_queue.c)
    target_link_libraries(tutorial0${num}
        ${FFMPEG_LDFLAGS}
        ${SDL_LIBRARY})
endforeach(num)
