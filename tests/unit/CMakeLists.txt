# Unit tests: test core functionality in isolation

add_executable(test_core_isolation test_core_isolation.c)
nayla_target_warnings(test_core_isolation)
target_link_libraries(test_core_isolation PRIVATE engine_core)
add_test(NAME unit/core_isolation COMMAND test_core_isolation)

# Unit tests for allocator (ALLOC-INV-3/4/5/6)
add_executable(test_allocator_unit test_allocator_unit.c)
nayla_target_warnings(test_allocator_unit)
target_link_libraries(test_allocator_unit PRIVATE engine_foundation)
add_test(NAME unit/allocator COMMAND test_allocator_unit)

# Unit tests for result (RESULT-INV-7)
add_executable(test_result_unit test_result_unit.c)
nayla_target_warnings(test_result_unit)
target_link_libraries(test_result_unit PRIVATE engine_foundation)
add_test(NAME unit/result COMMAND test_result_unit)

# Unit tests for logger (LOG-INV-3/4/6/7/11/12/14)
# Requirement 1: link engine_foundation only, never engine_core
add_executable(test_logger_unit test_logger_unit.c)
nayla_target_warnings(test_logger_unit)
target_link_libraries(test_logger_unit PRIVATE engine_foundation)
add_test(NAME unit/logger COMMAND test_logger_unit)
