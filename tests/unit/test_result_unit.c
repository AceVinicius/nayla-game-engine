#include <stdio.h>
#include <string.h>
#include "src/core/result.h"

/* Test RESULT-INV-7: engine_result_name() returns non-NULL for every enumerator
 * and for at least one out-of-range value. */
static int test_result_name(void) {
    printf("Testing RESULT-INV-7: result_name()...\n");

    /* Test all defined enumerators */
    struct {
        EngineResult code;
        const char *expected;
    } cases[] = {
        { ENGINE_RESULT_SUCCESS, "ENGINE_RESULT_SUCCESS" },
        { ENGINE_RESULT_INVALID_ARGUMENT, "ENGINE_RESULT_INVALID_ARGUMENT" },
        { ENGINE_RESULT_INVALID_STATE, "ENGINE_RESULT_INVALID_STATE" },
        { ENGINE_RESULT_OUT_OF_MEMORY, "ENGINE_RESULT_OUT_OF_MEMORY" },
        { ENGINE_RESULT_NOT_SUPPORTED, "ENGINE_RESULT_NOT_SUPPORTED" },
    };

    int num_cases = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < num_cases; i++) {
        const char *name = engine_result_name(cases[i].code);
        if (name == NULL) {
            fprintf(stderr, "✗ engine_result_name(%d) returned NULL\n", cases[i].code);
            return 1;
        }
        if (strcmp(name, cases[i].expected) != 0) {
            fprintf(stderr, "✗ engine_result_name(%d) returned '%s', expected '%s'\n",
                    cases[i].code, name, cases[i].expected);
            return 1;
        }
        printf("  ✓ engine_result_name(%d) = '%s'\n", cases[i].code, name);
    }

    /* Test out-of-range value: should return non-NULL (CORE-RESULT-011) */
    const char *unknown = engine_result_name(999);
    if (unknown == NULL) {
        fprintf(stderr, "✗ engine_result_name(999) returned NULL for out-of-range value\n");
        return 1;
    }
    printf("  ✓ engine_result_name(999) = '%s' (out-of-range returns non-NULL)\n", unknown);

    /* Verify it says "UNKNOWN" or similar for out-of-range */
    if (strstr(unknown, "UNKNOWN") == NULL && strstr(unknown, "unknown") == NULL) {
        fprintf(stderr, "  (Note: out-of-range name is '%s', expected to contain 'UNKNOWN')\n", unknown);
        /* This is a note, not a failure — the spec just says non-NULL */
    }

    printf("✓ engine_result_name() verified for all codes and out-of-range (RESULT-INV-7)\n");
    return 0;
}

int main(void) {
    printf("Running result unit tests...\n\n");

    if (test_result_name()) {
        printf("\n✗ Result unit tests failed\n");
        return 1;
    }

    printf("\n✅ All result unit tests passed!\n");
    return 0;
}
