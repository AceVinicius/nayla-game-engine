# Unit tests: test core functionality in isolation

add_executable(test_core_isolation test_core_isolation.c)
target_link_libraries(test_core_isolation PRIVATE engine_core)

add_test(NAME unit/core_isolation COMMAND test_core_isolation)
