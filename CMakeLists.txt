cmake_minimum_required(VERSION 3.16)

# Project name
project(BonDriver_LinuxFSUSB2N LANGUAGES CXX)

# System Information (for debug)
message(STATUS "System Information")
message(STATUS "System = ${CMAKE_SYSTEM_NAME}")
message(STATUS "C Compiler = ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ Compiler = ${CMAKE_CXX_COMPILER_ID}")

# Build Type Configuration
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Package
include(FetchContent)

FetchContent_Declare(
	plog
	GIT_REPOSITORY https://github.com/SergiusTheBest/plog
	GIT_TAG        master
	GIT_SHALLOW    true
)
FetchContent_MakeAvailable(plog)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Source Files
add_library(
	${PROJECT_NAME}
	SHARED
	src/BonDriver_LinuxFSUSB2N.cpp
	FSUSB2N/IoThread.cpp
	FSUSB2N/em2874-core.cpp
	FSUSB2N/ktv.cpp
	FSUSB2N/usbops.cpp
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
	cxx_std_20
)

set_target_properties(
	${PROJECT_NAME}
	PROPERTIES
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	PREFIX ""
)

# Include Directory
target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	include
	FSUSB2N
)

# Link Library
target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	Threads::Threads
	plog::plog
)
