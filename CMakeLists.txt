cmake_minimum_required(VERSION 3.12)

project(
    uvperf
    VERSION 0.1.0
    LANGUAGES C
)

add_executable(uvperf uvperf.c)

target_link_libraries(uvperf "${CMAKE_SOURCE_DIR}/lib/libusbK.dll")
