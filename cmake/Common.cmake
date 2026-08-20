# Common CMake utilities for the game engine

# Macro to forbid OS/GPU includes in source files
# Usage: forbid_os_gpu_includes(target_name source_dir)
macro(forbid_os_gpu_includes target_name source_dir)
  # Grep-based validation: fail build if forbidden headers found
  add_custom_command(TARGET ${target_name} PRE_BUILD
    COMMAND bash -c "! grep -r '#include.*<windows\\.h>' '${source_dir}' 2>/dev/null || (echo 'ERROR: OS header windows.h found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#include.*<unistd\\.h>' '${source_dir}' 2>/dev/null || (echo 'ERROR: OS header unistd.h found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#include.*<vulkan' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU header vulkan found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#include.*<d3d12' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU header d3d12 found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#include.*<metal' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU header metal found in ${target_name}' && false)"
  )
endmacro()

# Macro to forbid GPU SDK branching in source files
# Usage: forbid_gpu_branching(target_name source_dir)
macro(forbid_gpu_branching target_name source_dir)
  add_custom_command(TARGET ${target_name} PRE_BUILD
    COMMAND bash -c "! grep -r '#ifdef.*VULKAN' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU branching #ifdef VULKAN found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#ifdef.*D3D12' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU branching #ifdef D3D12 found in ${target_name}' && false)"
    COMMAND bash -c "! grep -r '#ifdef.*METAL' '${source_dir}' 2>/dev/null || (echo 'ERROR: GPU branching #ifdef METAL found in ${target_name}' && false)"
  )
endmacro()
