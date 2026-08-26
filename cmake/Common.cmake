# Common CMake utilities for the game engine

# Grep-based validation, delegated to a plain script (cmake/forbid-pattern.sh)
# rather than an inline `bash -c "..."` one-liner: a multi-token shell script
# embedded as a single CMake string argument gets mangled when re-serialized
# through the Ninja/Makefile generator's own shell invocation (semicolons and
# parens end up unescaped at the wrong layer). A script file with plain argv
# has no such nesting to survive.
set(NAYLA_FORBID_PATTERN_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/forbid-pattern.sh")

# Macro to forbid OS/GPU includes in source files
# Usage: forbid_os_gpu_includes(target_name source_dir)
macro(forbid_os_gpu_includes target_name source_dir)
  add_custom_command(TARGET ${target_name} PRE_BUILD
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "OS header windows.h" "#include.*<windows\\.h>" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "OS header unistd.h" "#include.*<unistd\\.h>" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU header vulkan" "#include.*<vulkan" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU header d3d12" "#include.*<d3d12" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU header metal" "#include.*<metal" "${source_dir}"
    VERBATIM
  )
endmacro()

# Macro to forbid GPU SDK branching in source files
# Usage: forbid_gpu_branching(target_name source_dir)
macro(forbid_gpu_branching target_name source_dir)
  add_custom_command(TARGET ${target_name} PRE_BUILD
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU branching #ifdef VULKAN" "#ifdef.*VULKAN" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU branching #ifdef D3D12" "#ifdef.*D3D12" "${source_dir}"
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "GPU branching #ifdef METAL" "#ifdef.*METAL" "${source_dir}"
    VERBATIM
  )
endmacro()
