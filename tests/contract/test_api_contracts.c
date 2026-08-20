#include <stdio.h>
#include "src/core/engine.h"

/* Test: engine API contracts work as specified.
 * Verifies that engine lifecycle (init → tick → shutdown) succeeds.
 */
int main(void) {
    struct engine *e = NULL;
    engine_error_t err;

    printf("Testing engine API contracts...\n");

    /* Test engine_init() returns 0 (success) */
    err = engine_init(&e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_init() failed with error %d\n", err);
        return 1;
    }
    if (e == NULL) {
        fprintf(stderr, "✗ engine_init() did not allocate engine\n");
        return 1;
    }
    printf("✓ engine_init() succeeds and allocates memory\n");

    /* Test engine_tick() accepts initialized engine */
    err = engine_tick(e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_tick() failed with error %d\n", err);
        engine_shutdown(e);
        return 1;
    }
    printf("✓ engine_tick() succeeds on initialized engine\n");

    /* Test engine_shutdown() returns 0 (success) */
    err = engine_shutdown(e);
    if (err != 0) {
        fprintf(stderr, "✗ engine_shutdown() failed with error %d\n", err);
        return 1;
    }
    printf("✓ engine_shutdown() succeeds and frees memory\n");

    /* Test error handling: tick on NULL */
    err = engine_tick(NULL);
    if (err == 0) {
        fprintf(stderr, "✗ engine_tick(NULL) should return error\n");
        return 1;
    }
    printf("✓ engine_tick(NULL) returns error as specified\n");

    printf("\n✅ All API contract tests passed!\n");
    return 0;
}
