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

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Main executable
add_executable(nayla_game_engine main.c)
target_link_libraries(nayla_game_engine PRIVATE engine_core)
