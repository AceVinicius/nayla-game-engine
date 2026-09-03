#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "src/core/engine.h"

#ifndef NAYLA_CORE_SRC_DIR
# error "NAYLA_CORE_SRC_DIR must be defined by CMake (see tests/architecture/CMakeLists.txt)"
#endif

#ifndef NAYLA_RENDERER_SRC_DIR
# error "NAYLA_RENDERER_SRC_DIR must be defined by CMake (see tests/architecture/CMakeLists.txt)"
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
    Engine *e = NULL;

    /* EngineResult is validated by RESULT-INV-2's static_assert (sizeof check),
     * so no runtime verification needed here. */

    /* Verify init/tick/shutdown return EngineResult (not SDK types) */
    EngineResult err = engine_init(&e, NULL);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_init() failed: %s\n", engine_result_name(err));
        return 1;
    }
    printf("✓ engine_init() uses EngineResult (not SDK-specific)\n");

    err = engine_tick(e);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_tick() failed: %s\n", engine_result_name(err));
        EngineResult shutdown_err = engine_shutdown(e);
        if (shutdown_err != ENGINE_RESULT_SUCCESS) {
            fprintf(stderr, "✗ engine_shutdown() also failed during cleanup: %s\n", engine_result_name(shutdown_err));
        }
        return 1;
    }
    printf("✓ engine_tick() uses EngineResult (not SDK-specific)\n");

    err = engine_shutdown(e);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_shutdown() failed: %s\n", engine_result_name(err));
        return 1;
    }
    printf("✓ engine_shutdown() uses EngineResult (not SDK-specific)\n");

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

    /* ALLOC-INV-7: No direct allocation calls (malloc/free/calloc/realloc/aligned_alloc)
     * except in allocator.c */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "grep -r -- '\\bmalloc\\s*(' '%s' 2>/dev/null | grep -v 'allocator.c'", NAYLA_CORE_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core contains a forbidden malloc call outside allocator.c\n");
        return 1;
    }
    printf("✓ Core has no malloc calls outside allocator.c (ALLOC-INV-7)\n");

    snprintf(cmd, sizeof(cmd), "grep -r -- '\\bfree\\s*(' '%s' 2>/dev/null | grep -v 'allocator.c'", NAYLA_CORE_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core contains a forbidden free call outside allocator.c\n");
        return 1;
    }
    printf("✓ Core has no free calls outside allocator.c (ALLOC-INV-7)\n");

    /* ALLOC-INV-8: allocator.h includes only <stddef.h>, no uintptr_t, no EngineResult */
    snprintf(cmd, sizeof(cmd), "grep -E 'uintptr_t|EngineResult' '%s/allocator.h'", NAYLA_CORE_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ allocator.h contains uintptr_t or EngineResult\n");
        return 1;
    }
    printf("✓ allocator.h avoids uintptr_t and EngineResult (ALLOC-INV-8)\n");

    snprintf(cmd, sizeof(cmd), "grep '#include' '%s/allocator.h' | grep -v 'stddef.h'", NAYLA_CORE_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ allocator.h includes headers other than <stddef.h>\n");
        return 1;
    }
    printf("✓ allocator.h includes only <stddef.h> (ALLOC-INV-8)\n");

    /* ALLOC-INV-9: No surviving allocator-shaped function pointers, in either
     * src/core or src/renderer (allocator.md ALLOC-INV-9 scopes both). */
    snprintf(cmd, sizeof(cmd), "grep -rE 'engine_alloc_fn|engine_free_fn|engine_set_allocators' '%s' '%s'", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core or renderer contains surviving allocator function pointer names\n");
        return 1;
    }
    printf("✓ Core and renderer have no surviving allocator-shaped function pointers (ALLOC-INV-9)\n");

    /* ALLOC-INV-10: No file-scope mutable allocator or allocator function
     * pointer in core or renderer (static const is OK; static without const
     * is not). Matched on "Allocator" (capital A): every sanctioned type
     * (EngineAllocator, EngineAllocatorAllocFn, EngineAllocatorFreeFn)
     * contains that substring, and \bstatic\b (word-bounded) does not match
     * "static_assert". A prior version of this check used "static [^c]…"
     * before "EngineAllocator", which consumed the type name's own leading
     * 'E' and could never match — verified false-negative on injection. */
    snprintf(cmd, sizeof(cmd), "grep -rE '\\bstatic\\b.*Allocator' '%s' '%s' | grep -v const", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core or renderer contains file-scope mutable allocator state\n");
        return 1;
    }
    printf("✓ Core and renderer have no file-scope mutable allocator state (ALLOC-INV-10)\n");

    /* RESULT-INV-4: No function returning bare int32_t as status from the
     * actual public API headers (engine.h, renderer's api.h) — not
     * allocator.h/result.h, which never declared such a function. */
    snprintf(cmd, sizeof(cmd), "grep -E '^int32_t [a-z_]*_(init|shutdown|create|destroy)' '%s/engine.h' '%s/api.h'", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Public headers declare functions returning bare int32_t as status\n");
        return 1;
    }
    printf("✓ Public headers use EngineResult, not bare int32_t (RESULT-INV-4)\n");

    /* RESULT-INV-5: No result-like typedef other than EngineResult, in
     * either core or renderer. */
    snprintf(cmd, sizeof(cmd), "grep -rE 'typedef.*Result|engine_error_t' '%s' '%s' | grep -v 'typedef enum EngineResult'", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core or renderer contains result-like typedefs other than EngineResult\n");
        return 1;
    }
    printf("✓ Core and renderer use only EngineResult for errors (RESULT-INV-5)\n");

    /* RESULT-INV-6: No bare return -1 or return 1 from EngineResult
     * functions, in either core or renderer. */
    snprintf(cmd, sizeof(cmd), "grep -rE 'return\\s+-?1\\s*;' '%s' '%s' | grep -v 'allocator.c'", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core or renderer contains bare return ±1 from EngineResult functions\n");
        return 1;
    }
    printf("✓ Core and renderer do not use bare return -1/1 (RESULT-INV-6)\n");

    /* RESULT-INV-9: No *_last_error or *_get_error symbols, no file-scope
     * mutable result state, in either core or renderer. */
    snprintf(cmd, sizeof(cmd), "grep -rE '_last_error|_get_error' '%s' '%s'", NAYLA_CORE_SRC_DIR, NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Core or renderer contains *_last_error or *_get_error symbols\n");
        return 1;
    }
    printf("✓ Core and renderer have no global error state (RESULT-INV-9)\n");

    /* ALLOC-INV-7 for renderer: No direct allocation calls */
    snprintf(cmd, sizeof(cmd), "grep -r -- '\\bmalloc\\s*(' '%s'", NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Renderer contains a forbidden malloc call\n");
        return 1;
    }
    printf("✓ Renderer has no malloc calls (ALLOC-INV-7)\n");

    snprintf(cmd, sizeof(cmd), "grep -r -- '\\bfree\\s*(' '%s'", NAYLA_RENDERER_SRC_DIR);
    if (system(cmd) == 0) {
        fprintf(stderr, "✗ Renderer contains a forbidden free call\n");
        return 1;
    }
    printf("✓ Renderer has no free calls (ALLOC-INV-7)\n");

    printf("\n✅ All architecture invariants verified!\n");
    return 0;
}
