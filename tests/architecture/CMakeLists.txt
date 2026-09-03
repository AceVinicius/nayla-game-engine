# Architecture tests: verify invariants

add_executable(test_core_invariants test_core_invariants.c)
target_link_libraries(test_core_invariants PRIVATE engine_core)
target_compile_definitions(test_core_invariants PRIVATE
  NAYLA_CORE_SRC_DIR="${CMAKE_SOURCE_DIR}/src/core"
  NAYLA_RENDERER_SRC_DIR="${CMAKE_SOURCE_DIR}/src/renderer"
)

add_test(NAME architecture/core_invariants COMMAND test_core_invariants)
