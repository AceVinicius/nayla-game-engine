# Integration tests: verify subsystems work together

# Integration test for allocator leak tracking (ALLOC-INV-12)
add_executable(test_allocator_leaktrack test_allocator_leaktrack.c)
target_link_libraries(test_allocator_leaktrack PRIVATE engine_core)
add_test(NAME integration/allocator_leaktrack COMMAND test_allocator_leaktrack)

# Integration test for compile-time log gate (LOG-INV-13)
# Two executables from the same source, with different ENGINE_LOG_COMPILE_MIN_LEVEL values.
# This proves the compile-time gate *removes* call sites rather than filtering at runtime.

# Build 1: COMPILE_MIN_LEVEL=4 (ERROR level) — ENGINE_LOG_TRACE is compiled out entirely
add_executable(test_log_compile_gate_level4 test_log_compile_gate.c)
target_link_libraries(test_log_compile_gate_level4 PRIVATE engine_core)
target_compile_definitions(test_log_compile_gate_level4 PRIVATE ENGINE_LOG_COMPILE_MIN_LEVEL=4)
add_test(NAME integration/log_compile_gate_level4 COMMAND test_log_compile_gate_level4)

# Build 2: COMPILE_MIN_LEVEL=0 (TRACE level) — ENGINE_LOG_TRACE is compiled in
add_executable(test_log_compile_gate_level0 test_log_compile_gate.c)
target_link_libraries(test_log_compile_gate_level0 PRIVATE engine_core)
target_compile_definitions(test_log_compile_gate_level0 PRIVATE ENGINE_LOG_COMPILE_MIN_LEVEL=0)
add_test(NAME integration/log_compile_gate_level0 COMMAND test_log_compile_gate_level0)
