# Common CMake utilities for the game engine.
#
# The forbid_* checks are grep-based and run before the target builds. They
# delegate to cmake/forbid-pattern.sh with plain argv instead of an inline
# `bash -c "..."` one-liner: a multi-token shell script embedded in one CMake
# string gets mangled by the generator's own shell quoting. VERBATIM keeps the
# `#` and `<` in the patterns from being read as shell syntax.
#
# Patterns are extended regexes (grep -E). Include checks are case-insensitive;
# the allocation and output checks stay case-sensitive, as before.
set(NAYLA_FORBID_PATTERN_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/forbid-pattern.sh")

# Reads a pattern file (one ERE per line, no comment lines) into `out` as one
# ERE joined with "|". The file is a configure dependency, so editing it re-runs
# configure. The architecture test reads the same files with `grep -f`.
function(_nayla_pattern_file_re out file)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(STRINGS "${file}" _lines)
  list(JOIN _lines "|" _joined)
  set(${out} "${_joined}" PARENT_SCOPE)
endfunction()

# PLAT-INV-3(b): the forbidden-include list (OS and platform Vulkan headers).
_nayla_pattern_file_re(NAYLA_FORBIDDEN_INCLUDE_RE "${CMAKE_SOURCE_DIR}/cmake/forbidden-includes.txt")
# GPU SDK headers, banned from core and renderer.
_nayla_pattern_file_re(NAYLA_GPU_SDK_INCLUDE_RE "${CMAKE_SOURCE_DIR}/cmake/forbidden-gpu-sdk-includes.txt")

# PLAT-INV-3(c): no GPU SDK token on a platform backend's include line (quoted or
# angled, any position), and no @import of a GPU or presentation module
# (submodules included, see ADR-011 Decision 6).
set(NAYLA_PLATFORM_SDK_INCLUDE_RE "#[[:space:]]*(include|import).*(vulkan|d3d1|dxgi|metal/|quartzcore/)")
set(NAYLA_PLATFORM_SDK_IMPORT_RE "@import[[:space:]]+(metal|quartzcore|metalkit|iosurface)\\b")

# Fail the build of `target` if the ERE `pattern` matches under `dir`, except in
# files whose basename matches the ERE `exempt` (pass "" for no exemption).
# `icase` is 1 for a case-insensitive match, "" otherwise.
function(_nayla_forbid target description pattern dir exempt icase)
  add_custom_command(TARGET ${target} PRE_BUILD
    COMMAND bash "${NAYLA_FORBID_PATTERN_SCRIPT}" "${target}" "${description}" "${pattern}" "${dir}" "${exempt}" "${icase}"
    VERBATIM
  )
endfunction()

# Turns a literal basename (e.g. "allocator.c") into an anchored ERE that matches
# only that basename. "." is escaped; the other characters in engine file names
# are ERE-neutral.
function(_nayla_literal_basename_re out name)
  if("${name}" STREQUAL "")
    set(${out} "" PARENT_SCOPE)
  else()
    string(REPLACE "." "\\." _re "${name}")
    set(${out} "^${_re}$" PARENT_SCOPE)
  endif()
endfunction()

# No OS or GPU SDK headers in core and renderer, with no exemption.
# Usage: forbid_os_gpu_includes(target dir)
# Custom commands cannot attach to INTERFACE targets, so this is for compiled
# libraries only. The INTERFACE headers (platform api.h, native.h, gpu api.h)
# are checked by the rows in tests/architecture/test_core_invariants.c instead.
function(forbid_os_gpu_includes target dir)
  _nayla_forbid(${target} "forbidden include (PLAT-INV-3(b): OS or platform Vulkan header)" "${NAYLA_FORBIDDEN_INCLUDE_RE}" "${dir}" "" 1)
  _nayla_forbid(${target} "GPU SDK include (vulkan, d3d12, metal)" "${NAYLA_GPU_SDK_INCLUDE_RE}" "${dir}" "" 1)
endfunction()

# PLAT-INV-3(b) for a GPU backend directory. Only surface_<type>.c|.m files may
# include OS headers (ADR-010); every other file is checked. Call it from the
# backend's CMakeLists.txt once the backend exists.
# Usage: forbid_gpu_backend_includes(target dir)
function(forbid_gpu_backend_includes target dir)
  _nayla_forbid(${target} "forbidden include in GPU backend (PLAT-INV-3(b), ADR-010)" "${NAYLA_FORBIDDEN_INCLUDE_RE}" "${dir}" "^surface_(win32|cocoa|x11|wayland)\\.(c|m)$" 1)
endfunction()

# PLAT-INV-3(c) for a platform backend directory (src/platform/<os>/). Helper
# only: no platform backend target exists yet (#30 adds them and calls this).
# Usage: forbid_platform_os_sdk(target dir)
function(forbid_platform_os_sdk target dir)
  _nayla_forbid(${target} "GPU SDK token on an include line in platform backend (PLAT-INV-3(c))" "${NAYLA_PLATFORM_SDK_INCLUDE_RE}" "${dir}" "" 1)
  _nayla_forbid(${target} "GPU or presentation @import in platform backend (PLAT-INV-3(c), ADR-011)" "${NAYLA_PLATFORM_SDK_IMPORT_RE}" "${dir}" "" 1)
endfunction()

# No GPU SDK branching (#ifdef VULKAN, ...). Case-sensitive.
# Usage: forbid_gpu_branching(target dir)
function(forbid_gpu_branching target dir)
  foreach(sdk IN ITEMS VULKAN D3D12 METAL)
    _nayla_forbid(${target} "GPU branching #ifdef ${sdk}" "#ifdef.*${sdk}" "${dir}" "" "")
  endforeach()
endfunction()

# No direct allocation calls; engine code goes through EngineAllocator. Case-sensitive.
# Usage: forbid_direct_allocation(target dir [exempt_basename])
function(forbid_direct_allocation target dir)
  _nayla_literal_basename_re(_exempt "${ARGV2}")
  foreach(fn IN ITEMS malloc free calloc realloc aligned_alloc)
    _nayla_forbid(${target} "direct ${fn} call" "\\b${fn}[[:space:]]*\\(" "${dir}" "${_exempt}" "")
  endforeach()
endfunction()

# No direct stream output; engine code goes through the logger. Case-sensitive.
# Usage: forbid_direct_output(target dir [exempt_basename])
function(forbid_direct_output target dir)
  _nayla_literal_basename_re(_exempt "${ARGV2}")
  foreach(fn IN ITEMS printf fprintf puts fputs fwrite)
    _nayla_forbid(${target} "direct ${fn} call" "\\b${fn}[[:space:]]*\\(" "${dir}" "${_exempt}" "")
  endforeach()
  foreach(id IN ITEMS stdout stderr)
    _nayla_forbid(${target} "${id} identifier" "\\b${id}\\b" "${dir}" "${_exempt}" "")
  endforeach()
endfunction()

# Configure-time assertion that `target` does not link `forbidden_target`,
# directly or transitively. `invariant_id` (e.g. PLAT-INV-4) names the rule in
# the error. A grep over a CMakeLists.txt only sees a literal link line; this
# walks the link graph CMake resolved.
# Limitation: a snapshot at call time. Call it after every target it should
# cover has declared its target_link_libraries(). Target names are compared as
# strings, so an ALIAS must be listed by its alias name, and a name that is not
# a target yet is fine as `forbidden_target`.
# Usage: forbid_transitive_link(target forbidden_target invariant_id)
function(forbid_transitive_link target forbidden_target invariant_id)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "forbid_transitive_link: '${target}' is not a target")
  endif()

  set(_visited "")
  set(_queue "${target}")

  while(_queue)
    list(POP_FRONT _queue _current)
    if(_current STREQUAL forbidden_target)
      message(FATAL_ERROR "${invariant_id}: '${target}' links '${forbidden_target}' (directly or transitively) — forbidden")
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
