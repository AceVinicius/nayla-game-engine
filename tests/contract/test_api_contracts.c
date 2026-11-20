/* Engine API contract: init, tick and shutdown succeed in order, and invalid
 * arguments return exactly ENGINE_RESULT_INVALID_ARGUMENT (RESULT-INV-8). */

#include "src/core/engine.h"
#include "tests/check.h"

static int test_lifecycle(void) {
    Engine *e = NULL;

    EXPECT(engine_init(&e, NULL, NULL), ENGINE_RESULT_SUCCESS);
    CHECK(e != NULL, "engine_init succeeded but returned no engine");
    EXPECT(engine_tick(e), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_shutdown(e), ENGINE_RESULT_SUCCESS);
    return 0;
}

static int test_invalid_arguments(void) {
    EXPECT(engine_init(NULL, NULL, NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_tick(NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_shutdown(NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"engine lifecycle", test_lifecycle},
        {"invalid arguments", test_invalid_arguments},
    };
    return RUN_TESTS(tests);
}
