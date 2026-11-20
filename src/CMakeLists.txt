# Core engine library targets

add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(gpu)
add_subdirectory(renderer)

# Configure-time link-graph assertions (ALLOC-INV-11, PLAT-INV-4). They run here,
# after every target exists, because forbid_transitive_link is a call-time
# snapshot. The alias engine_platform is listed by name because the walk does
# not resolve ALIAS targets.
include(${CMAKE_SOURCE_DIR}/cmake/Common.cmake)
forbid_transitive_link(engine_renderer engine_core ALLOC-INV-11)

set(_nayla_platform_backends engine_platform engine_platform_win32 engine_platform_macos engine_platform_linux)
foreach(_forbidden IN ITEMS engine_platform_api ${_nayla_platform_backends})
  forbid_transitive_link(engine_renderer ${_forbidden} PLAT-INV-4)
endforeach()
foreach(_forbidden IN LISTS _nayla_platform_backends)
  forbid_transitive_link(engine_core ${_forbidden} PLAT-INV-4)
endforeach()
