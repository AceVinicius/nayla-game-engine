# Foundation library: allocator and result types
# Portable C code: no OS or GPU SDK headers, no engine dependencies

add_library(engine_foundation STATIC
  allocator.c
  result.c
  logging.c
  log_console.c
)

target_include_directories(engine_foundation PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Enforce: no OS/GPU includes in foundation, no direct allocation except in allocator.c,
# and no direct output except in log_console.c (LOG-INV-9)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Common.cmake)
forbid_os_gpu_includes(engine_foundation ${CMAKE_CURRENT_SOURCE_DIR})
forbid_direct_allocation(engine_foundation ${CMAKE_CURRENT_SOURCE_DIR} "allocator.c")
forbid_direct_output(engine_foundation ${CMAKE_CURRENT_SOURCE_DIR} "log_console.c")

# Set strict warnings for foundation
nayla_target_warnings(engine_foundation)

# Core engine library
# Portable C code: no OS or GPU SDK headers
# Depends on foundation for allocator and result types

add_library(engine_core STATIC
  engine.c
)

target_link_libraries(engine_core PUBLIC engine_foundation)

target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Enforce: no OS/GPU includes in core, no direct allocation except allocator.c,
# and no direct output except in log_console.c (LOG-INV-9)
forbid_os_gpu_includes(engine_core ${CMAKE_CURRENT_SOURCE_DIR})
forbid_direct_allocation(engine_core ${CMAKE_CURRENT_SOURCE_DIR} "allocator.c")
forbid_direct_output(engine_core ${CMAKE_CURRENT_SOURCE_DIR} "log_console.c")

# Set strict warnings for core
nayla_target_warnings(engine_core)
