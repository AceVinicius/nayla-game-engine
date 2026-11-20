# Platform API library (abstract, will be implemented by OS backends)

add_library(engine_platform_api INTERFACE)

target_include_directories(engine_platform_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# PLATFORM-092: the API headers use the foundation's allocator, logger and result types.
target_link_libraries(engine_platform_api INTERFACE engine_foundation)

# Placeholder: no implementation yet
