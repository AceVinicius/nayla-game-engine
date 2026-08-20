# Architecture tests: verify invariants

add_executable(test_core_invariants test_core_invariants.c)
target_link_libraries(test_core_invariants PRIVATE engine_core)

add_test(NAME architecture/core_invariants COMMAND test_core_invariants)
