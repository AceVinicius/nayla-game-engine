# Platform API library and the selected OS backend (PLATFORM-092, PLATFORM-093).

add_library(engine_platform_api INTERFACE)

target_include_directories(engine_platform_api INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# PLATFORM-092: the API headers use the foundation's allocator, logger and result types.
target_link_libraries(engine_platform_api INTERFACE engine_foundation)

# No cmake/Common.cmake forbid_* check is attached here: a custom command cannot
# attach to an INTERFACE target. api.h and native.h are enforced by the
# PLAT-INV-3(a) and PLAT-INV-3(b) rows in tests/architecture/test_core_invariants.c.

# PLATFORM-092: the backend is chosen once per configure. The default is the
# host OS; an unknown host leaves the default empty and fails the check below.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_nayla_platform_default win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_nayla_platform_default macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_nayla_platform_default linux)
else()
  set(_nayla_platform_default "")
endif()

set(NAYLA_PLATFORM_BACKEND "${_nayla_platform_default}" CACHE STRING
    "Platform backend: win32, macos or linux (default: the host OS)")
set_property(CACHE NAYLA_PLATFORM_BACKEND PROPERTY STRINGS win32 macos linux)

set(_nayla_platform_known win32 macos linux)
if(NOT NAYLA_PLATFORM_BACKEND IN_LIST _nayla_platform_known)
  message(FATAL_ERROR "NAYLA_PLATFORM_BACKEND='${NAYLA_PLATFORM_BACKEND}' is not one of: win32, macos, linux")
endif()

# Only the selected backend exists. Its directory is the only per-OS difference.
set(_nayla_backend_target engine_platform_${NAYLA_PLATFORM_BACKEND})
set(_nayla_backend_dir ${CMAKE_CURRENT_SOURCE_DIR}/${NAYLA_PLATFORM_BACKEND})
set(_nayla_common_dir ${CMAKE_CURRENT_SOURCE_DIR}/common)

# Shared OS-independent code (adapters, tick conversion, the stub lifecycle)
# is compiled into every backend; each backend supplies its own services.c.
add_library(${_nayla_backend_target} STATIC
  ${_nayla_common_dir}/platform_common.c
  ${_nayla_backend_dir}/services.c
)
add_library(engine_platform ALIAS ${_nayla_backend_target})

target_link_libraries(${_nayla_backend_target} PUBLIC engine_platform_api engine_foundation)

target_compile_options(${_nayla_backend_target} PRIVATE -Wall -Wextra -Wpedantic)

# PLAT-INV-3(c) and the foundation rules (no direct allocation, no direct output),
# applied to the backend and the shared code, with no exemption.
include(${CMAKE_SOURCE_DIR}/cmake/Common.cmake)
foreach(_nayla_dir IN ITEMS ${_nayla_backend_dir} ${_nayla_common_dir})
  forbid_platform_os_sdk(${_nayla_backend_target} ${_nayla_dir})
  forbid_direct_allocation(${_nayla_backend_target} ${_nayla_dir})
  forbid_direct_output(${_nayla_backend_target} ${_nayla_dir})
endforeach()
