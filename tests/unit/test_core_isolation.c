/* Core stands alone: it compiles and runs linked against engine_core only.
 * (An OS or GPU include in core would break that build.) Explicit checks, not
 * assert(): Release defines NDEBUG, which would compile assert away. */

#include "src/core/engine.h"
#include "tests/check.h"

static int test_core_runs_standalone(void) {
    Engine *engine = NULL;

    EXPECT(engine_init(&engine, NULL, NULL), ENGINE_RESULT_SUCCESS);
    CHECK(engine != NULL, "engine_init returned no engine");
    EXPECT(engine_tick(engine), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_shutdown(engine), ENGINE_RESULT_SUCCESS);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"core runs standalone", test_core_runs_standalone},
    };
    return RUN_TESTS(tests);
}
