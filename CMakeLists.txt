cmake_minimum_required(VERSION 4.3)
project(nayla_game_engine C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Include common CMake utilities
include(cmake/Common.cmake)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# PLATFORM-092: the backend is chosen once per configure, here, because the
# Objective-C language must be enabled at the root before add_subdirectory(src)
# (ADR-011 Decision 6). The default is the host OS; an unknown host leaves the
# default empty and fails the check below.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_nayla_platform_default win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_nayla_platform_default macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_nayla_platform_default linux)
else()
  set(_nayla_platform_default "")
endif()

set(NAYLA_PLATFORM_BACKEND "${_nayla_platform_default}" CACHE STRING
    "Platform backend: win32, macos or linux (default: the host OS)")
set_property(CACHE NAYLA_PLATFORM_BACKEND PROPERTY STRINGS win32 macos linux)

set(_nayla_platform_known win32 macos linux)
if(NOT NAYLA_PLATFORM_BACKEND IN_LIST _nayla_platform_known)
  message(FATAL_ERROR "NAYLA_PLATFORM_BACKEND='${NAYLA_PLATFORM_BACKEND}' is not one of: win32, macos, linux")
endif()

# ADR-011 Decision 6: the macOS backend is Objective-C. Non-Apple configures
# never need an Objective-C compiler.
if(NAYLA_PLATFORM_BACKEND STREQUAL "macos")
  enable_language(OBJC)
endif()

# Must be called at the root (before add_subdirectory) so ctest can find the
# root CTestTestfile.cmake that recurses into tests/.
enable_testing()

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Main executable
add_executable(nayla_game_engine main.c)
target_link_libraries(nayla_game_engine PRIVATE engine_core)
