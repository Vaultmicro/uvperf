cmake_minimum_required(VERSION 3.12)


# project name, version, language
project(
    uvperf
    VERSION 1.1.1
    LANGUAGES C
)


# C standard C++ standard setting
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# C and C++ compiler setting
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")


set(SOURCE_FILES
	${CMAKE_SOURCE_DIR}/src/uvperf.c
    	${CMAKE_SOURCE_DIR}/src/log.c
    	${CMAKE_SOURCE_DIR}/src/usb_descriptor.c
    	${CMAKE_SOURCE_DIR}/src/fileio.c
    	${CMAKE_SOURCE_DIR}/src/param.c
    	${CMAKE_SOURCE_DIR}/src/transfer_p.c
    	${CMAKE_SOURCE_DIR}/src/benchmark.c
    	${CMAKE_SOURCE_DIR}/src/setting.c

)


add_executable(
	uvperf
	${SOURCE_FILES}
)


find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

include_directories(
	${LIBUSB_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/include
)


#Library Link
target_link_libraries(
	${PROJECT_NAME}
	PRIVATE 
		${CMAKE_SOURCE_DIR}/lib/libusbK.lib
		${LIBUSB_LIBRARIES}
)


