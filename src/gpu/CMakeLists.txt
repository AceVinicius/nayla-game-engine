# GPU API library (abstract, implementable by Vulkan/D3D12/Metal)

add_library(engine_gpu_api INTERFACE)

target_include_directories(engine_gpu_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# No cmake/Common.cmake forbid_* check is attached here: a custom command cannot
# attach to an INTERFACE target. api.h is enforced by the PLAT-INV-3(b) row in
# tests/architecture/test_core_invariants.c. GPU backend directories call
# forbid_gpu_backend_includes() from their own CMakeLists.txt.

# Placeholder: no implementation yet
