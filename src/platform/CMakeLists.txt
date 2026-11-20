# Platform API library (abstract, will be implemented by OS backends)

add_library(engine_platform_api INTERFACE)

target_include_directories(engine_platform_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# PLATFORM-092: the API headers use the foundation's allocator, logger and result types.
target_link_libraries(engine_platform_api INTERFACE engine_foundation)

# No cmake/Common.cmake forbid_* check is attached here: a custom command cannot
# attach to an INTERFACE target. api.h and native.h are enforced by the
# PLAT-INV-3(a) and PLAT-INV-3(b) rows in tests/architecture/test_core_invariants.c.

# Placeholder: no implementation yet
