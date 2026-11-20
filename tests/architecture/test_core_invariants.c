/* Architecture invariants, checked by grep independently of the build-time
 * checks in cmake/Common.cmake, so a regression there (quoting, a dropped
 * command) cannot silently leave an invariant untested.
 *
 * Each row is a shell command that prints matches and exits 0 when it finds
 * a VIOLATION. A row passes when the command finds nothing. */

#include <stdio.h>
#include <stdlib.h>
#include "src/core/engine.h"

#if !defined(NAYLA_CORE_SRC_DIR) || !defined(NAYLA_RENDERER_SRC_DIR) || !defined(NAYLA_REPO_ROOT)
# error "NAYLA_*_DIR / NAYLA_REPO_ROOT must be defined by CMake (tests/architecture/CMakeLists.txt)"
#endif

#define Q(path) "'" path "'"
#define CORE Q(NAYLA_CORE_SRC_DIR)
#define REND Q(NAYLA_RENDERER_SRC_DIR)
#define BOTH CORE " " REND

#define ALLOC_CALLS "\\b(malloc|calloc|realloc|aligned_alloc|free)\\s*\\("
/* grep -v matches the file path prefix of each "path:line:text" hit. */
#define NOT_IN(file) " | grep -v " file

/* LOG-INV-9: one row per name so a failure names which one fired. */
#define NO_STREAM(name, pat) \
    {"LOG-INV-9: no " name " in core or renderer", \
     "grep -rE '" pat "' " BOTH NOT_IN("/log_console.c:")}

typedef struct {
    const char *label;
    const char *violation_cmd;
} Check;

static const Check checks[] = {
    {"Core has no OS headers",
     "grep -rE '#include.*<(windows|unistd)\\.h>' " CORE},
    {"Core has no GPU SDK headers",
     "grep -rE '#include.*<(vulkan|d3d12|metal)' " CORE},

    {"ALLOC-INV-7: no direct allocation in core outside allocator.c",
     "grep -rE '" ALLOC_CALLS "' " CORE NOT_IN("allocator.c")},
    {"ALLOC-INV-7: no direct allocation in renderer",
     "grep -rE '" ALLOC_CALLS "' " REND},
    {"ALLOC-INV-8: allocator.h avoids uintptr_t and EngineResult",
     "grep -E 'uintptr_t|EngineResult' '" NAYLA_CORE_SRC_DIR "/allocator.h'"},
    {"ALLOC-INV-8: allocator.h includes only <stddef.h>",
     "grep '#include' '" NAYLA_CORE_SRC_DIR "/allocator.h'" NOT_IN("stddef.h")},
    {"ALLOC-INV-9: no surviving allocator function-pointer names",
     "grep -rE 'engine_alloc_fn|engine_free_fn|engine_set_allocators' " BOTH},
    /* \bstatic\b does not match static_assert; `grep -v const` allows the
     * sanctioned static const singletons. Matched on "Allocator" because
     * every allocator type name contains it. [^(]* requires the type to come
     * before any "(", so static helper functions taking the type as a
     * parameter are not mistaken for mutable globals. */
    {"ALLOC-INV-10: no file-scope mutable allocator state",
     "grep -rE '\\bstatic\\b[^(]*Allocator' " BOTH NOT_IN("const")},

    NO_STREAM("printf", "\\bprintf\\s*\\("),
    NO_STREAM("fprintf", "\\bfprintf\\s*\\("),
    NO_STREAM("puts", "\\bputs\\s*\\("),
    NO_STREAM("fputs", "\\bfputs\\s*\\("),
    NO_STREAM("fwrite", "\\bfwrite\\s*\\("),
    NO_STREAM("stdout", "\\bstdout\\b"),
    NO_STREAM("stderr", "\\bstderr\\b"),

    {"RESULT-INV-4: public headers return EngineResult, not bare int32_t",
     "grep -E '^int32_t [a-z_]*_(init|shutdown|create|destroy)' '" NAYLA_CORE_SRC_DIR "/engine.h' '"
     NAYLA_RENDERER_SRC_DIR "/api.h'"},
    {"RESULT-INV-5: no result type other than EngineResult",
     "grep -rE 'typedef.*Result|engine_error_t' " BOTH NOT_IN("'typedef enum EngineResult'")},
    {"RESULT-INV-6: no bare return -1 / return 1",
     "grep -rE 'return\\s+-?1\\s*;' " BOTH NOT_IN("allocator.c")},
    {"RESULT-INV-9: no global error state",
     "grep -rE '_last_error|_get_error' " BOTH},

    {"LOG-INV-5: no file-scope mutable logger state",
     "grep -rE '\\bstatic\\b[^(]*(EngineLogger|EngineLogSink|EngineLogLevel)' " BOTH NOT_IN("const")},
    /* Repo-wide, but C sources only: docs and tasks discuss these names in
     * prose, and this file names them to check for them. */
    {"LOG-INV-5: no global logger symbols anywhere in C source",
     "grep -rE --include='*.c' --include='*.h' --exclude='test_core_invariants.c' "
     "--exclude-dir='cmake-build-*' --exclude-dir='.git' "
     "'default_logger|global_log|log_set_global' " Q(NAYLA_REPO_ROOT)},
    {"LOG-INV-10: logging.h includes exactly five headers",
     "test \"$(grep -cE '^#[[:space:]]*include' '" NAYLA_CORE_SRC_DIR "/logging.h')\" -ne 5"},
    {"LOG-INV-10: logging.h avoids allocator.h, stdio.h, malloc, _Atomic",
     "grep -E 'allocator\\.h|<stdio\\.h>|malloc|_Atomic' '" NAYLA_CORE_SRC_DIR "/logging.h'"},
};

int main(void) {
    int failures = 0;

    /* Lifecycle: the public API returns EngineResult end to end. */
    Engine *e = NULL;
    if (engine_init(&e, NULL, NULL) != ENGINE_RESULT_SUCCESS ||
        engine_tick(e) != ENGINE_RESULT_SUCCESS ||
        engine_shutdown(e) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_init/tick/shutdown lifecycle failed\n");
        failures++;
    } else {
        printf("✓ engine_init/tick/shutdown lifecycle\n");
    }

    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        if (system(checks[i].violation_cmd) == 0) {
            fprintf(stderr, "✗ %s\n", checks[i].label);
            failures++;
        } else {
            printf("✓ %s\n", checks[i].label);
        }
    }

    if (failures != 0) {
        fprintf(stderr, "\n%d architecture invariant(s) violated\n", failures);
        return 1;
    }
    printf("\n✅ All architecture invariants verified!\n");
    return 0;
}
