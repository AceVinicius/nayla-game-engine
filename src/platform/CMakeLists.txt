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
elseif(NAYLA_PLATFORM_BACKEND STREQUAL "linux")
  # ADR-012: selection and dispatch, with one file per window-system path.
  list(APPEND _nayla_backend_sources
    ${_nayla_backend_dir}/platform_linux.c
    ${_nayla_backend_dir}/x11.c
    ${_nayla_backend_dir}/wayland.c)
else()
  # win32 has no window system yet: it uses the stub lifecycle until #34.
  list(APPEND _nayla_backend_sources ${_nayla_common_dir}/platform_stub.c)
endif()

# ADR-012 follow-up 2: the Linux backend's client libraries and the wayland-scanner
# glue. Only the linux configure reaches this block.
if(NAYLA_PLATFORM_BACKEND STREQUAL "linux")
  find_package(PkgConfig REQUIRED)
  # x11 >= 1.7: per-Display XSetIOErrorExitHandler (ADR-012 Decision 6).
  pkg_check_modules(NAYLA_X11 REQUIRED IMPORTED_TARGET x11>=1.7)
  pkg_check_modules(NAYLA_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(NAYLA_LIBDECOR REQUIRED IMPORTED_TARGET libdecor-0)
  # fractional-scale-v1 is a staging protocol; it first shipped in wayland-protocols 1.31.
  pkg_check_modules(NAYLA_WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.31)
  pkg_get_variable(_nayla_wl_scanner_pc wayland-scanner wayland_scanner)
  get_filename_component(_nayla_wl_scanner_hint "${_nayla_wl_scanner_pc}" DIRECTORY)
  find_program(_nayla_wl_scanner wayland-scanner REQUIRED HINTS "${_nayla_wl_scanner_hint}")
  pkg_get_variable(_nayla_wl_protocols_dir wayland-protocols pkgdatadir)

  set(_nayla_wl_protocols
    ${_nayla_wl_protocols_dir}/stable/xdg-shell/xdg-shell.xml
    ${_nayla_wl_protocols_dir}/stable/viewporter/viewporter.xml
    ${_nayla_wl_protocols_dir}/staging/fractional-scale/fractional-scale-v1.xml
  )
  foreach(_nayla_xml IN LISTS _nayla_wl_protocols)
    if(NOT EXISTS "${_nayla_xml}")
      message(FATAL_ERROR "wayland-protocols file not found: ${_nayla_xml}")
    endif()
  endforeach()

  # Generated into the BUILD tree, never under src/: the forbid_* greps above
  # and below scan only src/, so they never see this glue. Whether generated
  # sources count as src/platform/linux for PLATFORM-094 is the open question in
  # issue #59; this block does not decide it.
  set(_nayla_wl_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/wayland)
  file(MAKE_DIRECTORY "${_nayla_wl_gen_dir}")
  set(_nayla_linux_generated "")
  foreach(_nayla_xml IN LISTS _nayla_wl_protocols)
    get_filename_component(_nayla_proto "${_nayla_xml}" NAME_WE)
    set(_nayla_hdr "${_nayla_wl_gen_dir}/${_nayla_proto}-client-protocol.h")
    set(_nayla_src "${_nayla_wl_gen_dir}/${_nayla_proto}-protocol.c")
    add_custom_command(
      OUTPUT "${_nayla_hdr}"
      COMMAND "${_nayla_wl_scanner}" client-header "${_nayla_xml}" "${_nayla_hdr}"
      DEPENDS "${_nayla_xml}" "${_nayla_wl_scanner}"
      VERBATIM
    )
    add_custom_command(
      OUTPUT "${_nayla_src}"
      COMMAND "${_nayla_wl_scanner}" private-code "${_nayla_xml}" "${_nayla_src}"
      DEPENDS "${_nayla_xml}" "${_nayla_wl_scanner}"
      VERBATIM
    )
    list(APPEND _nayla_linux_generated "${_nayla_hdr}" "${_nayla_src}")
  endforeach()
  list(APPEND _nayla_backend_sources ${_nayla_linux_generated})
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

if(NAYLA_PLATFORM_BACKEND STREQUAL "linux")
  # PRIVATE: the backend compiles against these headers, and the static library
  # carries the libraries to its only consumers, the executables, at link time.
  # Nothing above the backend includes X11 or Wayland headers.
  target_include_directories(${_nayla_backend_target} PRIVATE ${_nayla_wl_gen_dir})
  target_link_libraries(${_nayla_backend_target} PRIVATE
    PkgConfig::NAYLA_X11
    PkgConfig::NAYLA_WAYLAND_CLIENT
    PkgConfig::NAYLA_LIBDECOR
  )
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
