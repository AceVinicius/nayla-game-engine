# Unit tests: test core functionality in isolation

add_executable(test_core_isolation test_core_isolation.c)
target_link_libraries(test_core_isolation PRIVATE engine_core)
add_test(NAME unit/core_isolation COMMAND test_core_isolation)

# Unit tests for allocator (ALLOC-INV-3/4/5/6)
add_executable(test_allocator_unit test_allocator_unit.c)
target_link_libraries(test_allocator_unit PRIVATE engine_foundation)
add_test(NAME unit/allocator COMMAND test_allocator_unit)

# Unit tests for result (RESULT-INV-7)
add_executable(test_result_unit test_result_unit.c)
target_link_libraries(test_result_unit PRIVATE engine_foundation)
add_test(NAME unit/result COMMAND test_result_unit)
