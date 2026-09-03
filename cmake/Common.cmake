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

# Macro to forbid direct allocation (malloc/free/calloc/realloc/aligned_alloc) in source files
# Usage: forbid_direct_allocation(target_name source_dir [exempt_filename])
# exempt_filename: optional filename to exempt from the check (e.g. "allocator.c")
macro(forbid_direct_allocation target_name source_dir)
  set(exempt_file "${ARGV2}")
  if(exempt_file)
    add_custom_command(TARGET ${target_name} PRE_BUILD
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct malloc call" "\\bmalloc\\s*(" "${source_dir}" "${exempt_file}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct free call" "\\bfree\\s*(" "${source_dir}" "${exempt_file}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct calloc call" "\\bcalloc\\s*(" "${source_dir}" "${exempt_file}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct realloc call" "\\brealloc\\s*(" "${source_dir}" "${exempt_file}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct aligned_alloc call" "\\baligned_alloc\\s*(" "${source_dir}" "${exempt_file}"
      VERBATIM
    )
  else()
    add_custom_command(TARGET ${target_name} PRE_BUILD
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct malloc call" "\\bmalloc\\s*(" "${source_dir}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct free call" "\\bfree\\s*(" "${source_dir}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct calloc call" "\\bcalloc\\s*(" "${source_dir}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct realloc call" "\\brealloc\\s*(" "${source_dir}"
      COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target_name}" "direct aligned_alloc call" "\\baligned_alloc\\s*(" "${source_dir}"
      VERBATIM
    )
  endif()
endmacro()

# Configure-time assertion that `target` does not link `forbidden_target`,
# directly or transitively (ALLOC-INV-11). A text grep over a CMakeLists.txt
# only catches a direct, literal link line; it says nothing about a link
# arriving through an intermediate target's own PUBLIC/INTERFACE libraries.
# This walks the real link-library graph CMake resolves at configure time.
# Limitation: it is a snapshot at the point it is called. A link added to a
# target *after* this call (e.g. appended to a dependency later in the same
# configure pass) is not seen. Call this after every target in the graph it
# should cover has already declared its own target_link_libraries().
# Usage: forbid_transitive_link(target_name forbidden_target_name)
function(forbid_transitive_link target_name forbidden_target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "forbid_transitive_link: '${target_name}' is not a target")
  endif()

  set(_visited "")
  set(_queue "${target_name}")

  while(_queue)
    list(POP_FRONT _queue _current)
    if(_current STREQUAL forbidden_target_name)
      message(FATAL_ERROR "ALLOC-INV-11: '${target_name}' links '${forbidden_target_name}' (directly or transitively) — forbidden")
    endif()
    if(NOT TARGET ${_current})
      continue()
    endif()
    list(FIND _visited "${_current}" _already_visited)
    if(NOT _already_visited EQUAL -1)
      continue()
    endif()
    list(APPEND _visited "${_current}")

    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_libs ${_current} ${_prop})
      if(_libs)
        list(APPEND _queue ${_libs})
      endif()
    endforeach()
  endwhile()
endfunction()
