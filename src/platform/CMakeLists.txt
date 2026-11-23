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

# PLATFORM-092: the backend itself is selected in the root CMakeLists.txt.
# Only the selected backend exists. Its directory is the only per-OS difference.
set(_nayla_backend_target engine_platform_${NAYLA_PLATFORM_BACKEND})
set(_nayla_backend_dir ${CMAKE_CURRENT_SOURCE_DIR}/${NAYLA_PLATFORM_BACKEND})
set(_nayla_common_dir ${CMAKE_CURRENT_SOURCE_DIR}/common)

# Shared OS-independent code (adapters, tick conversion, allocator resolution)
# is compiled into every backend; each backend supplies its own services.c.
set(_nayla_backend_sources
  ${_nayla_common_dir}/platform_common.c
  ${_nayla_backend_dir}/services.c
)
if(NAYLA_PLATFORM_BACKEND STREQUAL "macos")
  list(APPEND _nayla_backend_sources ${_nayla_backend_dir}/platform_macos.m)
else()
  # win32 and linux have no window system yet: they share the stub lifecycle.
  list(APPEND _nayla_backend_sources ${_nayla_common_dir}/platform_stub.c)
endif()

add_library(${_nayla_backend_target} STATIC ${_nayla_backend_sources})
add_library(engine_platform ALIAS ${_nayla_backend_target})

target_link_libraries(${_nayla_backend_target} PUBLIC engine_platform_api engine_foundation)

nayla_target_warnings(${_nayla_backend_target})

if(NAYLA_PLATFORM_BACKEND STREQUAL "macos")
  # ADR-011 Decision 1: manual retain/release, set explicitly so a generator
  # default (Xcode's CLANG_ENABLE_OBJC_ARC) cannot switch it on.
  target_compile_options(${_nayla_backend_target} PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>)
  target_link_libraries(${_nayla_backend_target} PRIVATE "-framework AppKit")
endif()

# PLAT-INV-3(c) and the foundation rules (no direct allocation, no direct
# output), with no exemption, on EVERY backend directory and the shared code,
# whichever backend is selected: CI builds only Linux and macOS, so this is what
# polices src/platform/win32/.
include(${CMAKE_SOURCE_DIR}/cmake/Common.cmake)
foreach(_nayla_dir IN ITEMS ${_nayla_platform_known} common)
  set(_nayla_dir ${CMAKE_CURRENT_SOURCE_DIR}/${_nayla_dir})
  forbid_platform_os_sdk(${_nayla_backend_target} ${_nayla_dir})
  forbid_direct_allocation(${_nayla_backend_target} ${_nayla_dir})
  forbid_direct_output(${_nayla_backend_target} ${_nayla_dir})
endforeach()

# PLATFORM-094: OS headers belong in src/platform/<os>/ only, never in the shared code.
forbid_os_gpu_includes(${_nayla_backend_target} ${_nayla_common_dir})
