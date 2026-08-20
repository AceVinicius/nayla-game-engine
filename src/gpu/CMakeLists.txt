# GPU API library (abstract, implementable by Vulkan/D3D12/Metal)

add_library(engine_gpu_api INTERFACE)

target_include_directories(engine_gpu_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Placeholder: no implementation yet
