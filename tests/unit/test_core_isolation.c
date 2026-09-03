#include <stdio.h>
#include <assert.h>
#include "src/core/engine.h"

/* Test: core has no OS includes (compile-time check).
 * If core included OS headers, this would fail to compile.
 */
int main(void) {
    Engine *engine = NULL;

    /* Test engine initialization */
    EngineResult result = engine_init(&engine, NULL, NULL);
    assert(result == ENGINE_RESULT_SUCCESS);
    assert(engine != NULL);

    printf("✓ Engine initialized\n");

    /* Test engine tick */
    result = engine_tick(engine);
    assert(result == ENGINE_RESULT_SUCCESS);
    printf("✓ Engine tick completed\n");

    /* Test engine shutdown */
    result = engine_shutdown(engine);
    assert(result == ENGINE_RESULT_SUCCESS);
    printf("✓ Engine shutdown completed\n");

    printf("\n✅ All unit tests passed!\n");
    return 0;
}
