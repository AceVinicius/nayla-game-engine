# Renderer library: high-level rendering above GPU API

add_library(engine_renderer STATIC api.h)

# Renderer uses GPU API only; enforced by dependency check
target_link_libraries(engine_renderer PRIVATE engine_gpu_api)

# Forbid direct GPU SDK includes in renderer
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Common.cmake)
forbid_os_gpu_includes(engine_renderer ${CMAKE_CURRENT_SOURCE_DIR})

# Strict warnings
target_compile_options(engine_renderer PRIVATE -Wall -Wextra -Wpedantic)
