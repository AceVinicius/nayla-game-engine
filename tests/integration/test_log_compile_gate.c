#include <stdint.h>
#include <stdio.h>
#include "src/core/logging.h"

/* LOG-INV-13: Compile-time gate removes call sites entirely, not just filters at runtime.
 *
 * This test demonstrates that ENGINE_LOG_COMPILE_MIN_LEVEL produces two observably
 * different binaries from the same source:
 * - At LEVEL=4 (ERROR): ENGINE_LOG_TRACE expands to ((void)0), so the side-effecting
 *   argument is never evaluated, and counter remains 0.
 * - At LEVEL=0 (TRACE): ENGINE_LOG_TRACE expands to the full call, argument is
 *   evaluated, counter increments to 1.
 *
 * CORE-LOG-062 forbids side-effecting log arguments in normal engine code.
 * This test deliberately violates that rule to prove the compile-time gate works.
 * This file is an intentional exception to CORE-LOG-062, as the point of the test
 * is state-based verification of compile-time elimination.
 */

static uint32_t counter = 0;

/* Function with a side effect: increments counter and returns it.
 * Used to prove that the log macro argument is evaluated or not.
 */
static uint32_t counter_that_increments(void) {
    return ++counter;
}

int main(void) {
    /* Create a logger (NULL is valid per CORE-LOG-005) */
    EngineLogger logger;
    EngineResult err = engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    /* Call ENGINE_LOG_TRACE with a side-effecting argument.
     * If compiled with ENGINE_LOG_COMPILE_MIN_LEVEL=0 (TRACE):
     *   The macro expands to a full call, counter_that_increments() is evaluated,
     *   counter becomes 1, and the logging call proceeds.
     * If compiled with ENGINE_LOG_COMPILE_MIN_LEVEL=4 (ERROR):
     *   The macro expands to ((void)0), counter_that_increments() is NOT evaluated,
     *   counter remains 0, and no logging occurs.
     */
    ENGINE_LOG_TRACE(&logger, "Core", "Counter: %u", counter_that_increments());

    /* Verify the expected state */
    #if ENGINE_LOG_COMPILE_MIN_LEVEL <= 0
        /* At compile level TRACE or DEBUG: counter should be 1 (call was made) */
        if (counter != 1) {
            fprintf(stderr, "ERROR: At COMPILE_MIN_LEVEL=%d, expected counter=1 but got %u\n",
                    ENGINE_LOG_COMPILE_MIN_LEVEL, counter);
            engine_log_shutdown(&logger);
            return 1;
        }
        printf("✓ Compile level %d: ENGINE_LOG_TRACE was compiled in, counter incremented to %u\n",
               ENGINE_LOG_COMPILE_MIN_LEVEL, counter);
    #else
        /* At compile level higher than TRACE: counter should be 0 (call was optimized out) */
        if (counter != 0) {
            fprintf(stderr, "ERROR: At COMPILE_MIN_LEVEL=%d, expected counter=0 but got %u\n",
                    ENGINE_LOG_COMPILE_MIN_LEVEL, counter);
            engine_log_shutdown(&logger);
            return 1;
        }
        printf("✓ Compile level %d: ENGINE_LOG_TRACE was optimized out, counter remains %u\n",
               ENGINE_LOG_COMPILE_MIN_LEVEL, counter);
    #endif

    engine_log_shutdown(&logger);
    printf("✅ Compile-time gate test passed (LOG-INV-13)\n");
    return 0;
}
