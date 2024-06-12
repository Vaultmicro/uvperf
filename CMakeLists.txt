cmake_minimum_required(VERSION 3.10)

project(uvperf VERSION 1.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/wd4200 /wd4201 /wd4214)
else()
    add_compile_options(-Wno-unknown-pragmas)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCE_FILES
    src/uvperf.c
    src/log.c
    src/bench.c
    src/transfer.c
    src/utils.c
    src/usb_descriptor.c
    src/chrono_time.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

link_directories(${CMAKE_SOURCE_DIR}/lib)

target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/lib/libusbK.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/lib/libusb0.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/lib/libusb-1.0.lib)

message(STATUS "Build configuration")
message(STATUS "  Project       : ${PROJECT_NAME}")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  C Standard    : ${CMAKE_C_STANDARD}")
message(STATUS "  C++ Standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Source files  : ${SOURCE_FILES}")