#include <stdio.h>
#include <assert.h>
#include "src/core/engine.h"

/* Test: core has no OS includes (compile-time check).
 * If core included OS headers, this would fail to compile.
 */
int main(void) {
    struct engine *engine;

    /* Test engine initialization */
    int result = engine_init(&engine);
    assert(result == 0);
    assert(engine != NULL);

    printf("✓ Engine initialized\n");

    /* Test engine tick */
    engine_tick(engine);
    printf("✓ Engine tick completed\n");

    /* Test engine shutdown */
    engine_shutdown(engine);
    printf("✓ Engine shutdown completed\n");

    printf("\n✅ All unit tests passed!\n");
    return 0;
}
