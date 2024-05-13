cmake_minimum_required(VERSION 3.12)

project(
    uvperf
    VERSION 1.0.5
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(uvperf uvperf.c log.c)

target_link_libraries(uvperf "${CMAKE_SOURCE_DIR}/lib/libusbK.dll")
