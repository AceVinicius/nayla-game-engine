# Integration tests: verify subsystems work together

# Integration test for allocator leak tracking (ALLOC-INV-12)
add_executable(test_allocator_leaktrack test_allocator_leaktrack.c)
target_link_libraries(test_allocator_leaktrack PRIVATE engine_core)
add_test(NAME integration/allocator_leaktrack COMMAND test_allocator_leaktrack)
