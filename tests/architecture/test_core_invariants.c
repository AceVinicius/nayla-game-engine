/* Architecture invariants, checked by grep independently of the build-time
 * checks in cmake/Common.cmake, so a regression there (quoting, a dropped
 * command) cannot silently leave an invariant untested.
 *
 * Each row is a shell command that prints matches and exits 0 when it finds
 * a VIOLATION. A row passes when the command finds nothing. */

#include <stdio.h>
#include <stdlib.h>
#include "src/core/engine.h"

#if !defined(NAYLA_CORE_SRC_DIR) || !defined(NAYLA_RENDERER_SRC_DIR) || !defined(NAYLA_REPO_ROOT) || \
    !defined(NAYLA_PLATFORM_SRC_DIR) || !defined(NAYLA_GPU_SRC_DIR) || !defined(NAYLA_FORBIDDEN_INCLUDES) || \
    !defined(NAYLA_FORBIDDEN_GPU_SDK_INCLUDES)
# error "NAYLA_*_DIR / NAYLA_REPO_ROOT / NAYLA_FORBIDDEN_*INCLUDES must be defined by CMake (tests/architecture/CMakeLists.txt)"
#endif

#define Q(path) "'" path "'"
#define CORE Q(NAYLA_CORE_SRC_DIR)
#define REND Q(NAYLA_RENDERER_SRC_DIR)
#define BOTH CORE " " REND
#define API_H Q(NAYLA_PLATFORM_SRC_DIR "/api.h")
#define NATIVE_H Q(NAYLA_PLATFORM_SRC_DIR "/native.h")
#define GPU_API_H Q(NAYLA_GPU_SRC_DIR "/api.h")
#define PLAT_DIR Q(NAYLA_PLATFORM_SRC_DIR)

/* The shared PLAT-INV-3(b) list, the same file cmake/Common.cmake reads.
 * grep -i: the match is case-insensitive. */
#define FORBIDDEN_LIST Q(NAYLA_FORBIDDEN_INCLUDES)
#define GPU_SDK_LIST Q(NAYLA_FORBIDDEN_GPU_SDK_INCLUDES)
#define NO_FORBIDDEN(where, paths) \
    {"PLAT-INV-3(b): no forbidden includes in " where, \
     "grep -rniE -f " FORBIDDEN_LIST " " paths}
#define NO_GPU_SDK(where, paths) \
    {"PLAT-INV-3(b): no GPU SDK includes in " where, \
     "grep -rniE -f " GPU_SDK_LIST " " paths}
/* A grep error (missing file, invalid regex) exits 2, and a row counts exit 0
 * as a violation, so an error would pass silently. This row fails when the
 * list is empty or missing, or when grep cannot compile it (exit 2). */
#define LIST_GUARD(name, list) \
    {"guard: " name " list is present and compiles", \
     "test ! -s " list " || { grep -E -f " list " /dev/null; test $? -eq 2; }"}
/* PLAT-INV-14 patterns, shared by the row and its guard.
 * VAR: a column-0 declaration, with an optional array suffix, ending in ";" or ",".
 * FP: a column-0 function-pointer declarator "(*name". */
#define PLAT14_VAR "^[A-Za-z_][A-Za-z0-9_ *]*[ *][*]?[A-Za-z_][A-Za-z0-9_]*(\\[[^]]*\\])*[[:space:]]*(=[^;]*)?[;,]"
#define PLAT14_FP "^[^(#]*\\([[:space:]]*\\*[[:space:]]*[A-Za-z_]"

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
    LIST_GUARD("forbidden-includes", FORBIDDEN_LIST),
    LIST_GUARD("GPU SDK", GPU_SDK_LIST),
    NO_FORBIDDEN("core and renderer", BOTH),
    NO_FORBIDDEN("platform api.h", API_H),
    NO_FORBIDDEN("platform native.h", NATIVE_H),
    NO_FORBIDDEN("gpu api.h", GPU_API_H),
    NO_GPU_SDK("core and renderer", BOTH),

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

    /* PLAT-INV-2: native handles stay in platform/GPU backends and tests/platform/.
     * This file names them to check for them, so it is excluded. */
    {"PLAT-INV-2: no native-handle names outside backends and tests/platform",
     "grep -rE 'platform/native\\.h|PlatformNative(Window|Object)|platform_window_get_native' "
     CORE " " REND " " API_H " " Q(NAYLA_GPU_SRC_DIR "/api.h") " " Q(NAYLA_REPO_ROOT "/tests")
     " | grep -v '/tests/platform/' | grep -v '/test_core_invariants.c:'"},

    /* PLAT-INV-3(a): exact include sets (PLATFORM-094), sorted so order does not matter.
     * Spaces are stripped so "#include <x>" and "#include<x>" compare equal. */
    {"PLAT-INV-3(a): api.h includes exactly PLATFORM-094's set",
     "test \"$(grep -E '^#[[:space:]]*include' " API_H " | sed 's/[[:space:]]//g' | sort)\""
     " != \"$(printf '%s\\n' '#include<stdbool.h>' '#include<stddef.h>' '#include<stdint.h>'"
     " '#include\"src/core/result.h\"' '#include\"src/core/allocator.h\"'"
     " '#include\"src/core/logging.h\"' | sort)\""},
    {"PLAT-INV-3(a): native.h includes exactly PLATFORM-094's set",
     "test \"$(grep -E '^#[[:space:]]*include' " NATIVE_H " | sed 's/[[:space:]]//g' | sort)\""
     " != \"$(printf '%s\\n' '#include<stdint.h>' '#include\"src/platform/api.h\"' | sort)\""},
    {"PLAT-INV-3(a): platform headers have no void * outside user_data",
     "grep -nE 'void[[:space:]]*\\*' " API_H " " NATIVE_H " | grep -v user_data"},
    {"PLAT-INV-3(a): platform headers have no bool struct field",
     "grep -nE '^[[:space:]]+bool[[:space:]]' " API_H " " NATIVE_H},
    {"PLAT-INV-3(a): platform headers have no u32-style shorthand",
     "grep -nwE 'u(8|16|32|64)' " API_H " " NATIVE_H},
    {"PLAT-INV-3(a): platform headers have no _t-suffixed engine type",
     "grep -howE '[A-Za-z_][A-Za-z0-9_]*_t' " API_H " " NATIVE_H
     " | grep -vE '^(u?int(8|16|32|64)|size|ptrdiff|max_align|intptr|uintptr)_t$'"},

    /* PLAT-INV-5: the only Platform API declarations without a Platform or
     * PlatformWindow pointer parameter are the four stateless functions.
     * Column-0 lines are declarations, pointer-returning ones included; sed
     * drops everything up to the "(" so only the parameter list is checked.
     * A multi-line declaration keeps its first parameter on its first line. */
    {"PLAT-INV-5: only the four stateless functions lack a Platform pointer",
     "grep -E '^[A-Za-z_][A-Za-z0-9_ *]*[[:space:]*]platform_[a-z0-9_]+\\(' " API_H
     " | grep -vE 'platform_(time_now_ns|thread_current_id|log_time_source|log_thread_id_source)\\('"
     " | sed 's/^[^(]*(//' | grep -E -v 'Platform(Window)?[[:space:]]*\\*'"},

    /* PLAT-INV-14: a file-scope variable is a column-0 declaration with no "("
     * before the name, ending in ";" or ",", and a function-pointer variable is
     * a column-0 "(*name". Both are scanned in .c and .m. The second grep drops
     * const and typedef lines (static const is allowed) and bare struct/union/
     * enum forward declarations. Heuristic: an indented or multi-line file-scope
     * declaration is not seen. */
    {"guard: PLAT-INV-14 patterns compile",
     "grep -E -e '" PLAT14_VAR "' -e '" PLAT14_FP "' /dev/null; test $? -eq 2"},
    /* The initializer is cut off first (sed), so "const" in a value cannot
     * hide a mutable declarator. A declarator is const when it has no "*" and
     * a "const", or when "*const" follows the last "*" (pointer itself const).
     * "static const char *p" keeps its pointer mutable, so it is flagged. */
    {"PLAT-INV-14: no file-scope non-const variable under src/platform (.c, .m)",
     "grep -rnE --include='*.c' --include='*.m'"
     " -e '" PLAT14_VAR "' -e '" PLAT14_FP "' " PLAT_DIR
     " | sed -E 's/=.*$//'"
     " | grep -vE ':[0-9]+:[^*]*\\bconst\\b[^*]*$|:[0-9]+:.*\\*[[:space:]]*const\\b[^*]*$"
     "|:[0-9]+:.*\\btypedef\\b|:[0-9]+:(struct|union|enum)[[:space:]]+[A-Za-z_0-9]+[[:space:]]*;'"},
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
