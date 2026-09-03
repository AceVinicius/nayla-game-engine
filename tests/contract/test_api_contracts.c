#include <stdio.h>
#include "src/core/engine.h"

/* Test: engine API contracts work as specified.
 * Verifies that engine lifecycle (init → tick → shutdown) succeeds.
 */
int main(void) {
    Engine *e = NULL;
    EngineResult err;

    printf("Testing engine API contracts...\n");

    /* Test engine_init() returns ENGINE_RESULT_SUCCESS */
    err = engine_init(&e, NULL, NULL);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_init() failed with error %s\n", engine_result_name(err));
        return 1;
    }
    if (e == NULL) {
        fprintf(stderr, "✗ engine_init() did not allocate engine\n");
        return 1;
    }
    printf("✓ engine_init() succeeds and allocates memory\n");

    /* Test engine_tick() accepts initialized engine */
    err = engine_tick(e);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_tick() failed with error %s\n", engine_result_name(err));
        EngineResult shutdown_err = engine_shutdown(e);
        if (shutdown_err != ENGINE_RESULT_SUCCESS) {
            fprintf(stderr, "✗ engine_shutdown() also failed during error cleanup\n");
        }
        return 1;
    }
    printf("✓ engine_tick() succeeds on initialized engine\n");

    /* Test engine_shutdown() returns ENGINE_RESULT_SUCCESS */
    err = engine_shutdown(e);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_shutdown() failed with error %s\n", engine_result_name(err));
        return 1;
    }
    printf("✓ engine_shutdown() succeeds and frees memory\n");

    /* Test error handling: tick on NULL (RESULT-INV-8) */
    err = engine_tick(NULL);
    if (err != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ engine_tick(NULL) should return ENGINE_RESULT_INVALID_ARGUMENT, got %s\n", engine_result_name(err));
        return 1;
    }
    printf("✓ engine_tick(NULL) returns ENGINE_RESULT_INVALID_ARGUMENT as specified\n");

    printf("\n✅ All API contract tests passed!\n");
    return 0;
}
