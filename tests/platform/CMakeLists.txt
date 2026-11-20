# Platform header checks: each public platform header compiles on its own,
# as C17 and as C23 (PLATFORM-094). tests/platform/ is the only test directory
# allowed to include native.h (PLAT-INV-2), which is why these live here.
foreach(_std IN ITEMS 17 23)
  foreach(_hdr IN ITEMS api native)
    set(_name test_platform_header_${_hdr}_c${_std})
    add_executable(${_name} header_${_hdr}.c)
    set_target_properties(${_name} PROPERTIES C_STANDARD ${_std} C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(${_name} PRIVATE engine_platform_api)
    target_compile_options(${_name} PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors)
    add_test(NAME architecture/platform_header_${_hdr}_c${_std} COMMAND ${_name})
  endforeach()
endforeach()

# Smoke test: links the selected backend (PLATFORM-092) so it is compiled and
# exercised. The full conformance suite is a separate task.
add_executable(test_platform_smoke test_platform_smoke.c)
target_link_libraries(test_platform_smoke PRIVATE engine_platform)
target_compile_options(test_platform_smoke PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit/platform_smoke COMMAND test_platform_smoke)

# Stateless conformance (PLATFORM-100..102; PLAT-INV-6, -7, -8, -13). Never
# skips (PLATFORM-101). The helper thread needs pthreads on POSIX hosts.
find_package(Threads REQUIRED)
add_executable(test_platform_stateless test_platform_stateless.c)
target_link_libraries(test_platform_stateless PRIVATE engine_platform Threads::Threads)
target_compile_options(test_platform_stateless PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME contract/platform_stateless COMMAND test_platform_stateless)
