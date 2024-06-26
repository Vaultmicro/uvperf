cmake_minimum_required(VERSION 3.12)

project(
    uvperf
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
    src/logger.cc
    uvperf_simple.cc
)

# Common settings

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(uvperf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# OS-specific settings
if (WIN32)
    # Windows-specific settings
    set(LIBUSB_DLL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dll)
    set(LIBUSB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(LIBUSB_LIBRARY_DIR ${LIBUSB_DLL_DIR})
    set(LIBUSB_LIBRARIES libusb-1.0)

    include_directories(${LIBUSB_INCLUDE_DIR})
    link_directories(${LIBUSB_LIBRARY_DIR})

    target_link_libraries(uvperf ${LIBUSB_LIBRARIES})

    # Copy the DLL to the output directory after building
    add_custom_command(TARGET uvperf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LIBUSB_DLL_DIR}/libusb-1.0.dll $<TARGET_FILE_DIR:uvperf>
    )

elseif (UNIX AND NOT APPLE)
    # Linux-specific settings
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

    include_directories(${LIBUSB_INCLUDE_DIRS})
    link_directories(${LIBUSB_LIBRARY_DIRS})

    target_link_libraries(uvperf ${LIBUSB_LIBRARIES})

elseif (APPLE)
    # macOS-specific settings
    find_package(PkgConfig REQUIRED)

    # Ensure pkg-config path includes Homebrew installation path
    set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/libusb/lib/pkgconfig:/usr/local/lib/pkgconfig")

    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

    # Manually add include and library directories for libusb
    include_directories(${LIBUSB_INCLUDE_DIRS})
    link_directories(${LIBUSB_LIBRARY_DIRS})

    target_link_libraries(uvperf /usr/local/lib/libusb-1.0.0.dylib)

    # Optionally, additional settings or configurations for macOS can be added here
endif()

message(STATUS "Build configuration")
message(STATUS "  System        : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Project       : ${PROJECT_NAME}")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  C Standard    : ${CMAKE_C_STANDARD}")
message(STATUS "  C++ Standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Source files  : ${SOURCE_FILES}")