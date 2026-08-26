# Core engine library
# Portable C code: no OS or GPU SDK headers

add_library(engine_core STATIC
  engine.c
)

target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Enforce: no OS/GPU includes in core
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Common.cmake)
forbid_os_gpu_includes(engine_core ${CMAKE_CURRENT_SOURCE_DIR})

# Set strict warnings
target_compile_options(engine_core PRIVATE
  -Wall -Wextra -Wpedantic
)
