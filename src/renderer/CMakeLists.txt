# Renderer library: high-level rendering above GPU API

add_library(engine_renderer STATIC renderer.c api.h)

# ALLOC-INV-11: Renderer must NOT link core (only foundation).
# Core owns engine lifecycle; renderer is independent.
# Do NOT add engine_core to target_link_libraries.
# Renderer uses GPU API only; enforced by dependency check
target_link_libraries(engine_renderer
  PUBLIC engine_foundation
  PRIVATE engine_gpu_api
)

# Forbid direct GPU SDK includes, OS headers, direct allocation, and direct output in renderer
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Common.cmake)
forbid_os_gpu_includes(engine_renderer ${CMAKE_CURRENT_SOURCE_DIR})
forbid_direct_allocation(engine_renderer ${CMAKE_CURRENT_SOURCE_DIR})
forbid_direct_output(engine_renderer ${CMAKE_CURRENT_SOURCE_DIR})

# ALLOC-INV-11 as a real configure-time assertion, not just the comment above:
# fails CMake configuration if engine_renderer ever links engine_core, even
# transitively through another target's own link libraries.
forbid_transitive_link(engine_renderer engine_core)

# Strict warnings
target_compile_options(engine_renderer PRIVATE -Wall -Wextra -Wpedantic)
