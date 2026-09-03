# Contract tests: verify API contracts are satisfied

add_executable(test_api_contracts test_api_contracts.c)
target_link_libraries(test_api_contracts PRIVATE engine_core)
add_test(NAME contract/api_contracts COMMAND test_api_contracts)

# Contract tests for allocator (ALLOC-INV-13: failing allocator)
add_executable(test_allocator_contracts test_allocator_contracts.c)
target_link_libraries(test_allocator_contracts PRIVATE engine_core)
add_test(NAME contract/allocator COMMAND test_allocator_contracts)

# Contract tests for logger (LOG-INV-8: add_sink/remove_sink contracts)
# Requirement 1: link engine_foundation only, never engine_core
add_executable(test_logger_contracts test_logger_contracts.c)
target_link_libraries(test_logger_contracts PRIVATE engine_foundation)
add_test(NAME contract/logger COMMAND test_logger_contracts)
