# Common CMake utilities for the game engine.
#
# The forbid_* checks are grep-based and run before the target builds. They
# delegate to cmake/forbid-pattern.sh with plain argv instead of an inline
# `bash -c "..."` one-liner: a multi-token shell script embedded in one CMake
# string gets mangled by the generator's own shell quoting. VERBATIM keeps the
# `#` and `<` in the patterns from being read as shell syntax.
set(NAYLA_FORBID_PATTERN_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/forbid-pattern.sh")

# Fail the build of `target` if `pattern` matches under `dir`.
# `exempt` is an optional file name to ignore (pass "" for none).
function(_nayla_forbid target description pattern dir exempt)
  add_custom_command(TARGET ${target} PRE_BUILD
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target}" "${description}" "${pattern}" "${dir}" "${exempt}"
    VERBATIM
  )
endfunction()

# No OS or GPU SDK headers.
# Usage: forbid_os_gpu_includes(target dir)
function(forbid_os_gpu_includes target dir)
  foreach(h IN ITEMS windows unistd)
    _nayla_forbid(${target} "OS header ${h}.h" "#include.*<${h}\\.h>" "${dir}" "")
  endforeach()
  foreach(sdk IN ITEMS vulkan d3d12 metal)
    _nayla_forbid(${target} "GPU header ${sdk}" "#include.*<${sdk}" "${dir}" "")
  endforeach()
endfunction()

# No GPU SDK branching (#ifdef VULKAN, ...).
# Usage: forbid_gpu_branching(target dir)
function(forbid_gpu_branching target dir)
  foreach(sdk IN ITEMS VULKAN D3D12 METAL)
    _nayla_forbid(${target} "GPU branching #ifdef ${sdk}" "#ifdef.*${sdk}" "${dir}" "")
  endforeach()
endfunction()

# No direct allocation calls; engine code goes through EngineAllocator.
# Usage: forbid_direct_allocation(target dir [exempt_filename])
function(forbid_direct_allocation target dir)
  foreach(fn IN ITEMS malloc free calloc realloc aligned_alloc)
    _nayla_forbid(${target} "direct ${fn} call" "\\b${fn}\\s*(" "${dir}" "${ARGV2}")
  endforeach()
endfunction()

# No direct stream output; engine code goes through the logger.
# Usage: forbid_direct_output(target dir [exempt_filename])
function(forbid_direct_output target dir)
  foreach(fn IN ITEMS printf fprintf puts fputs fwrite)
    _nayla_forbid(${target} "direct ${fn} call" "\\b${fn}\\s*(" "${dir}" "${ARGV2}")
  endforeach()
  foreach(id IN ITEMS stdout stderr)
    _nayla_forbid(${target} "${id} identifier" "\\b${id}\\b" "${dir}" "${ARGV2}")
  endforeach()
endfunction()

# Configure-time assertion that `target` does not link `forbidden_target`,
# directly or transitively (ALLOC-INV-11). A grep over a CMakeLists.txt only
# sees a literal link line; this walks the link graph CMake resolved.
# Limitation: a snapshot at call time. Call it after every target it should
# cover has declared its target_link_libraries().
# Usage: forbid_transitive_link(target forbidden_target)
function(forbid_transitive_link target forbidden_target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "forbid_transitive_link: '${target}' is not a target")
  endif()

  set(_visited "")
  set(_queue "${target}")

  while(_queue)
    list(POP_FRONT _queue _current)
    if(_current STREQUAL forbidden_target)
      message(FATAL_ERROR "ALLOC-INV-11: '${target}' links '${forbidden_target}' (directly or transitively) — forbidden")
    endif()
    if(NOT TARGET ${_current} OR _current IN_LIST _visited)
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
