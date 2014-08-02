cmake_minimum_required(VERSION 2.8)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_HOME_DIRECTORY}/cmake)

project(ffmpeg_tutorial C)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS "-std=gnu99")

find_package(Chicken REQUIRED)
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL)
find_package(SDL REQUIRED)

add_definitions(${FFMPEG_DEFINITIONS})
include_directories(
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL_INCLUDE_DIR}
)

foreach(num RANGE 1 4)
    add_executable(tutorial0${num} tutorial0${num}.c packet_queue.c)
    target_link_libraries(tutorial0${num}
        ${FFMPEG_LIBRARIES}
        ${SDL_LIBRARY})
endforeach(num)

add_chicken_module(avutil avutil.scm)
target_link_libraries(avutil ${FFMPEG_LIBRARIES})
add_chicken_module(avcodec avcodec.scm)
target_link_libraries(avcodec ${FFMPEG_LIBRARIES})
add_chicken_module(avformat avformat.scm)
target_link_libraries(avformat ${FFMPEG_LIBRARIES})

add_chicken_executable(test01 test01.scm
    DEPENDS avutil avcodec avformat)

install_chicken_modules(${PROJECT_NAME})
