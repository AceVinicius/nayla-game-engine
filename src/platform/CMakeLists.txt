# Platform API library (abstract, will be implemented by OS backends)

add_library(engine_platform_api INTERFACE)

target_include_directories(engine_platform_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Placeholder: no implementation yet
