# Architecture tests: verify invariants

add_executable(test_core_invariants test_core_invariants.c)
target_link_libraries(test_core_invariants PRIVATE engine_core)
target_compile_definitions(test_core_invariants PRIVATE
  NAYLA_CORE_SRC_DIR="${CMAKE_SOURCE_DIR}/src/core"
  NAYLA_RENDERER_SRC_DIR="${CMAKE_SOURCE_DIR}/src/renderer"
  NAYLA_PLATFORM_SRC_DIR="${CMAKE_SOURCE_DIR}/src/platform"
  NAYLA_GPU_SRC_DIR="${CMAKE_SOURCE_DIR}/src/gpu"
  NAYLA_REPO_ROOT="${CMAKE_SOURCE_DIR}"
  NAYLA_FORBIDDEN_INCLUDES="${CMAKE_SOURCE_DIR}/cmake/forbidden-includes.txt"
  NAYLA_FORBIDDEN_GPU_SDK_INCLUDES="${CMAKE_SOURCE_DIR}/cmake/forbidden-gpu-sdk-includes.txt"
)

add_test(NAME architecture/core_invariants COMMAND test_core_invariants)

# C17 build check: recompile every engine library source as C17. The source
# list is read from the targets, so a new file is covered automatically.
set(_c17_sources test_c17_build.c)
foreach(_lib IN ITEMS engine_foundation engine_core engine_renderer)
  get_target_property(_dir ${_lib} SOURCE_DIR)
  get_target_property(_srcs ${_lib} SOURCES)
  foreach(_src IN LISTS _srcs)
    if(_src MATCHES "\\.c$")
      cmake_path(ABSOLUTE_PATH _src BASE_DIRECTORY "${_dir}")
      list(APPEND _c17_sources "${_src}")
    endif()
  endforeach()
endforeach()

add_executable(test_c17_build ${_c17_sources})
set_target_properties(test_c17_build PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(test_c17_build PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_c17_build PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors)
add_test(NAME architecture/c17_build COMMAND test_c17_build)
