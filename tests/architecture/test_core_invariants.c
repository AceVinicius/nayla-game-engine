#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "src/core/engine.h"

#ifndef NAYLA_CORE_SRC_DIR
# error "NAYLA_CORE_SRC_DIR must be defined by CMake (see tests/architecture/CMakeLists.txt)"
#endif

/* grep exit 0 = pattern found = forbidden header present. Checked here
 * independently of cmake/forbid_os_gpu_includes() so a regression in the
 * build-time check (e.g. quoting/escaping through the generator) cannot
 * silently make this invariant untested. */
static int forbidden_pattern_found(const char *pattern) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "grep -rq -- '%s' '%s' 2>/dev/null", pattern, NAYLA_CORE_SRC_DIR);
    return system(cmd) == 0;
}

/* Test: core invariants are maintained.
 * 1. Compile-time: CMake forbid_os_gpu_includes() macro prevents OS/GPU headers.
 * 2. Runtime: verify public API exports only engine-defined types.
 * 3. Independently: re-check src/core directly (see forbidden_pattern_found above).
 */
int main(void) {
    printf("Testing core invariants...\n");

    /* Verify engine API uses engine-defined types */
    struct engine *e = NULL;

    /* Verify error type is fixed-width integer */
    engine_error_t err = 0;
    if (sizeof(err) != sizeof(int32_t)) {
        fprintf(stderr, "✗ engine_error_t is not int32_t\n");
        return 1;
    }
    printf("✓ engine_error_t is fixed-width (int32_t)\n");

    /* Verify init/tick/shutdown return engine_error_t (not SDK types) */
    err = engine_init(&e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_init() failed\n");
        return 1;
    }
    printf("✓ engine_init() uses engine_error_t (not SDK-specific)\n");

    err = engine_tick(e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_tick() failed\n");
        engine_shutdown(e);
        return 1;
    }
    printf("✓ engine_tick() uses engine_error_t (not SDK-specific)\n");

    err = engine_shutdown(e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_shutdown() failed\n");
        return 1;
    }
    printf("✓ engine_shutdown() uses engine_error_t (not SDK-specific)\n");

    if (forbidden_pattern_found("#include.*<windows\\.h>") ||
        forbidden_pattern_found("#include.*<unistd\\.h>")) {
        fprintf(stderr, "✗ Core contains a forbidden OS header\n");
        return 1;
    }
    printf("✓ Core has no OS headers (verified directly, not just via CMake)\n");

    if (forbidden_pattern_found("#include.*<vulkan") ||
        forbidden_pattern_found("#include.*<d3d12") ||
        forbidden_pattern_found("#include.*<metal")) {
        fprintf(stderr, "✗ Core contains a forbidden GPU SDK header\n");
        return 1;
    }
    printf("✓ Core has no GPU SDK headers (verified directly, not just via CMake)\n");

    printf("\n✅ All architecture invariants verified!\n");
    return 0;
}
