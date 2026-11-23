/* LOG-INV-13: the compile-time gate REMOVES log call sites; it does not merely
 * filter them at runtime. Built twice from this one source with different
 * ENGINE_LOG_COMPILE_MIN_LEVEL values (tests/integration/CMakeLists.txt):
 *   level 4: ENGINE_LOG_TRACE expands to ((void)0), so its argument is never
 *            evaluated and the counter stays 0.
 *   level 0: the full call is compiled in; the argument is evaluated once.
 *
 * The side-effecting log argument below is deliberate. CORE-LOG-062 forbids it
 * in engine code; here it is the only way to observe elimination, and this
 * file is the one sanctioned exception. */

#include <stdint.h>
#include "src/core/logging.h"
#include "tests/check.h"

static uint32_t counter;

static uint32_t counter_that_increments(void) {
    return ++counter;
}

static int test_compile_gate(void) {
    EngineLogger logger;
    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));

    /* Naming the helper outside the macro keeps it referenced at level 4, where
     * ENGINE_LOG_TRACE expands to nothing. Naming it does not call it, so the
     * counter still reflects only what the gate lets through. */
    (void)counter_that_increments;

    ENGINE_LOG_TRACE(&logger, "Core", "Counter: %u", counter_that_increments());

    const uint32_t expected = ENGINE_LOG_COMPILE_MIN_LEVEL <= 0 ? 1 : 0;
    CHECK(counter == expected, "at COMPILE_MIN_LEVEL=%d expected counter=%u, got %u",
          ENGINE_LOG_COMPILE_MIN_LEVEL, expected, counter);

    engine_log_shutdown(&logger);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"LOG-INV-13 compile-time gate", test_compile_gate},
    };
    return RUN_TESTS(tests);
}
